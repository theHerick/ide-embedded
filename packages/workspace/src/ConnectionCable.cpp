#include "ConnectionCable.h"
#include "WorkspaceScene.h"
#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsSceneHoverEvent>
#include <QGraphicsScene>
#include <QCursor>
#include <QMenu>
#include <QColorDialog>
#include <QGraphicsSceneContextMenuEvent>
#include <cmath>
#include <QTime>

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

// Build a strictly orthogonal chain through a list of points.
// Between each consecutive pair we insert a single L-bend corner.
// hFirst alternates starting from startHFirst.
std::vector<QPointF> ConnectionCable::buildOrthoChain(const std::vector<QPointF>& pts, bool startHFirst) {
    if (pts.size() < 2) return pts;

    std::vector<QPointF> out;
    out.push_back(pts[0]);
    bool hFirst = startHFirst;

    for (size_t i = 0; i + 1 < pts.size(); ++i) {
        QPointF a = pts[i];
        QPointF b = pts[i + 1];

        double dx = std::abs(b.x() - a.x());
        double dy = std::abs(b.y() - a.y());

        if (dx < 1.0 || dy < 1.0) {
            // Already axis-aligned — no corner needed
            out.push_back(b);
            // Don't toggle hFirst for already-aligned segments
            continue;
        }

        // Insert L-bend corner
        QPointF corner = hFirst ? QPointF(b.x(), a.y()) : QPointF(a.x(), b.y());
        out.push_back(corner);
        out.push_back(b);
        hFirst = !hFirst; // alternate for next non-aligned segment
    }

    return out;
}

// Apply rounded chamfers to a polyline
std::vector<QPointF> ConnectionCable::chamfer(const std::vector<QPointF>& pts, double radius) {
    if (pts.size() < 3) return pts;

    std::vector<QPointF> out;
    out.push_back(pts[0]);

    for (size_t i = 1; i + 1 < pts.size(); ++i) {
        QPointF prev = pts[i - 1];
        QPointF curr = pts[i];
        QPointF next = pts[i + 1];

        QPointF v_in  = curr - prev;
        QPointF v_out = next - curr;
        double l_in   = std::hypot(v_in.x(), v_in.y());
        double l_out  = std::hypot(v_out.x(), v_out.y());

        // Check collinearity
        double cross = std::abs(v_in.x() * v_out.y() - v_in.y() * v_out.x());
        if (cross < 0.1 || l_in < 1.0 || l_out < 1.0) {
            out.push_back(curr);
            continue;
        }

        double d = std::min(radius, std::min(l_in, l_out) / 2.1);
        if (d < 1.0) {
            out.push_back(curr);
        } else {
            out.push_back(curr - (v_in / l_in) * d);
            out.push_back(curr + (v_out / l_out) * d);
        }
    }

    out.push_back(pts.back());
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// Constructor
// ─────────────────────────────────────────────────────────────────────────────

ConnectionCable::ConnectionCable(ComponentItem* sourceComp, const QString& sourcePin,
                                 ComponentItem* targetComp, const QString& targetPin,
                                 const std::vector<QPointF>& manualWaypoints,
                                 bool startHFirst,
                                 bool srcIsJunc, const QPointF& srcJuncPos,
                                 bool tgtIsJunc, const QPointF& tgtJuncPos,
                                 QGraphicsItem* parent)
    : QGraphicsItem(parent),
      m_sourceComp(sourceComp), m_sourcePin(sourcePin),
      m_targetComp(targetComp), m_targetPin(targetPin),
      m_manualWaypoints(manualWaypoints),
      m_startHFirst(startHFirst),
      m_sourceIsJunction(srcIsJunc),
      m_sourceJunctionPos(srcJuncPos),
      m_targetIsJunction(tgtIsJunc),
      m_junctionPos(tgtJuncPos)
{
    setFlags(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemSendsGeometryChanges);
    setZValue(1);
    setAcceptHoverEvents(true);

    if (m_sourceComp) {
        if (Pin* p1 = m_sourceComp->getPinByName(sourcePin)) {
            m_sourcePin = p1->name;
        }
    }
    if (m_targetComp) {
        if (Pin* p2 = m_targetComp->getPinByName(targetPin)) {
            m_targetPin = p2->name;
        }
    }

    // Assign color by pin type
    if (m_sourcePin.contains("3V3") || m_sourcePin.contains("5V") || m_sourcePin.contains("VCC")) {
        m_color = QColor(239, 68, 68);
    } else if (m_sourcePin.contains("GND")) {
        m_color = QColor(75, 85, 99);
    } else {
        int hash = (m_sourceComp->id().length() + m_targetComp->id().length() + m_sourcePin.length()) % 4;
        if (hash == 0) m_color = QColor(16, 185, 129);
        else if (hash == 1) m_color = QColor(245, 158, 11);
        else if (hash == 2) m_color = QColor(14, 165, 233);
        else m_color = QColor(168, 85, 247);
    }

    updatePath();
}

// ─────────────────────────────────────────────────────────────────────────────
// updatePath — dual strategy:
//   • No waypoints → smart auto-routing (exit-direction stubs + L-bends)
//   • With waypoints → manual buildOrthoChain through user points
// ─────────────────────────────────────────────────────────────────────────────

void ConnectionCable::updatePath() {
    prepareGeometryChange();
    m_path.clear();

    if (!m_sourceComp || !m_targetComp) return;

    Pin* srcPin = m_sourceComp->getPinByName(m_sourcePin);
    Pin* tgtPin = m_targetComp->getPinByName(m_targetPin);
    if (!srcPin || !tgtPin) {
        qDebug() << "[CABLE ERROR] updatePath: pino não encontrado!"
                 << m_sourcePin << "ou" << m_targetPin;
        return;
    }

    QPointF p1 = m_sourceIsJunction ? m_sourceJunctionPos : m_sourceComp->getPinScenePos(*srcPin);
    QPointF p2 = m_targetIsJunction ? m_junctionPos : m_targetComp->getPinScenePos(*tgtPin);

    qDebug() << "[CABLE]" << m_sourcePin << "→" << m_targetPin
             << "| waypoints:" << m_manualWaypoints.size();

    std::vector<QPointF> finalPts;

    // ── MANUAL ROUTING: buildOrthoChain through user waypoints ────────
    std::vector<QPointF> chain;
    chain.push_back(p1);
    for (const auto& wp : m_manualWaypoints) chain.push_back(wp);
    chain.push_back(p2);

    std::vector<QPointF> ortho = buildOrthoChain(chain, m_startHFirst);
    for (const auto& pt : ortho) {
        if (finalPts.empty() ||
            std::hypot(pt.x()-finalPts.back().x(), pt.y()-finalPts.back().y()) > 0.5)
            finalPts.push_back(pt);
    }

    // Chamfer and paint
    std::vector<QPointF> chamfered = chamfer(finalPts, 10.0);
    if (!chamfered.empty()) {
        m_path.moveTo(chamfered[0]);
        for (size_t i = 1; i < chamfered.size(); ++i)
            m_path.lineTo(chamfered[i]);
    }
}


// ─────────────────────────────────────────────────────────────────────────────
// Qt overrides — geometry
// ─────────────────────────────────────────────────────────────────────────────

QRectF ConnectionCable::boundingRect() const {
    return m_path.controlPointRect().adjusted(-15, -15, 15, 15);
}

QPainterPath ConnectionCable::shape() const {
    QPainterPathStroker stroker;
    stroker.setWidth(14.0);
    stroker.setCapStyle(Qt::RoundCap);
    stroker.setJoinStyle(Qt::RoundJoin);
    return stroker.createStroke(m_path);
}

// ─────────────────────────────────────────────────────────────────────────────
// paint
// ─────────────────────────────────────────────────────────────────────────────

void ConnectionCable::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget*) {
    painter->setRenderHint(QPainter::Antialiasing);

    QColor baseColor = m_color;
    
    // Check if source or target is SMD to make track thinner
    bool isSmdTrack = false;
    if (m_sourceComp && (m_sourceComp->property("isSMD").toBool() || m_sourceComp->componentType() == "esp32")) {
        isSmdTrack = true;
    }
    if (m_targetComp && (m_targetComp->property("isSMD").toBool() || m_targetComp->componentType() == "esp32")) {
        isSmdTrack = true;
    }

    double glowWidth = isSmdTrack ? 6.5 : 10.0;
    double lineWidth = isSmdTrack ? 3.5 : 5.0;
    double selectLineWidth = isSmdTrack ? 4.5 : 6.5;

    // Glow
    QColor glowColor = baseColor;
    glowColor.setAlpha(40);
    QPen glowPen(glowColor, glowWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter->setPen(glowPen);
    painter->drawPath(m_path);

    // Core wire
    QPen linePen(baseColor, lineWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    if (option->state & QStyle::State_Selected) {
        linePen.setColor(Qt::white);
        linePen.setWidthF(selectLineWidth);
    }
    painter->setPen(linePen);
    painter->drawPath(m_path);

    // Draw waypoint handles when selected
    if (isSelected()) {
        for (size_t i = 0; i < m_manualWaypoints.size(); ++i) {
            QPointF pt = m_manualWaypoints[i];

            // Shadow
            painter->setPen(Qt::NoPen);
            painter->setBrush(QColor(0, 0, 0, 70));
            painter->drawRect(QRectF(pt + QPointF(-5.5, -4.5), QSizeF(11, 11)));

            // Handle square
            painter->setPen(QPen(Qt::white, 1.5));
            painter->setBrush(m_draggedWaypointIdx == static_cast<int>(i)
                               ? QColor(251, 191, 36)   // bright gold when dragging
                               : QColor(234, 179, 8));  // normal gold
            painter->drawRect(QRectF(pt + QPointF(-5, -5), QSizeF(10, 10)));

            // Centre dot
            painter->setPen(Qt::NoPen);
            painter->setBrush(Qt::white);
            painter->drawEllipse(pt, 1.5, 1.5);
        }
    }

    // Draw junction dot if source is a junction
    if (m_sourceIsJunction) {
        painter->save();
        painter->setPen(Qt::NoPen);
        QColor dotGlow = baseColor;
        dotGlow.setAlpha(60);
        painter->setBrush(dotGlow);
        painter->drawEllipse(m_sourceJunctionPos, isSmdTrack ? 6.0 : 8.0, isSmdTrack ? 6.0 : 8.0);
        painter->setBrush(baseColor);
        painter->drawEllipse(m_sourceJunctionPos, isSmdTrack ? 3.5 : 5.0, isSmdTrack ? 3.5 : 5.0);
        painter->restore();
    }

    // Draw junction dot if target is a junction
    if (m_targetIsJunction) {
        painter->save();
        painter->setPen(Qt::NoPen);
        // Draw glow
        QColor dotGlow = baseColor;
        dotGlow.setAlpha(60);
        painter->setBrush(dotGlow);
        painter->drawEllipse(m_junctionPos, isSmdTrack ? 6.0 : 8.0, isSmdTrack ? 6.0 : 8.0);
        // Draw solid dot
        painter->setBrush(baseColor);
        painter->drawEllipse(m_junctionPos, isSmdTrack ? 3.5 : 5.0, isSmdTrack ? 3.5 : 5.0);
        painter->restore();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Mouse events — waypoint dragging
// ─────────────────────────────────────────────────────────────────────────────

void ConnectionCable::mousePressEvent(QGraphicsSceneMouseEvent* event) {
    if (isSelected() && event->button() == Qt::LeftButton) {
        constexpr double kRadius = 8.0;
        for (size_t i = 0; i < m_manualWaypoints.size(); ++i) {
            double dist = std::hypot(event->pos().x() - m_manualWaypoints[i].x(),
                                     event->pos().y() - m_manualWaypoints[i].y());
            if (dist <= kRadius) {
                m_draggedWaypointIdx  = static_cast<int>(i);
                m_dragStartMousePos   = event->pos();
                m_dragStartWptPos     = m_manualWaypoints[i];
                event->accept();
                return;
            }
        }
    }
    QGraphicsItem::mousePressEvent(event);
}

void ConnectionCable::mouseMoveEvent(QGraphicsSceneMouseEvent* event) {
    if (m_draggedWaypointIdx >= 0) {
        QPointF delta = event->pos() - m_dragStartMousePos;
        QPointF newPos = m_dragStartWptPos + delta;
        // Snap to 10px grid
        newPos.setX(std::round(newPos.x() / 10.0) * 10.0);
        newPos.setY(std::round(newPos.y() / 10.0) * 10.0);
        m_manualWaypoints[static_cast<size_t>(m_draggedWaypointIdx)] = newPos;
        updatePath();
        update();
        event->accept();
        return;
    }
    QGraphicsItem::mouseMoveEvent(event);
}

void ConnectionCable::mouseReleaseEvent(QGraphicsSceneMouseEvent* event) {
    if (m_draggedWaypointIdx >= 0) {
        m_draggedWaypointIdx = -1;
        event->accept();
        return;
    }
    QGraphicsItem::mouseReleaseEvent(event);
}

void ConnectionCable::mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) {
    // Double-click on cable body → remove nearest waypoint (if any), or add one
    QPointF clickPt = event->pos();  // scene pos projected into item coords (item has no pos offset since it's pos (0,0))

    // Find if we're near an existing waypoint
    constexpr double kRadius = 12.0;
    for (size_t i = 0; i < m_manualWaypoints.size(); ++i) {
        double dist = std::hypot(clickPt.x() - m_manualWaypoints[i].x(),
                                 clickPt.y() - m_manualWaypoints[i].y());
        if (dist <= kRadius) {
            // Remove this waypoint
            m_manualWaypoints.erase(m_manualWaypoints.begin() + i);
            updatePath();
            update();
            event->accept();
            return;
        }
    }

    event->accept();
}

void ConnectionCable::hoverMoveEvent(QGraphicsSceneHoverEvent* event) {
    bool onHandle = false;
    if (isSelected()) {
        constexpr double hoverRadius = 8.0;
        for (const auto& wp : m_manualWaypoints) {
            if (std::hypot(event->pos().x() - wp.x(), event->pos().y() - wp.y()) <= hoverRadius) {
                onHandle = true;
                break;
            }
        }
    }
    setCursor(onHandle ? Qt::SizeAllCursor : Qt::PointingHandCursor);
    QGraphicsItem::hoverMoveEvent(event);
}

void ConnectionCable::contextMenuEvent(QGraphicsSceneContextMenuEvent* event) {
    QMenu menu;
    QAction* setColAction = menu.addAction("Alterar Cor da Trilha");
    QAction* deleteAction = menu.addAction("Excluir Trilha");

    QAction* selected = menu.exec(event->screenPos());
    if (selected == setColAction) {
        QColor newColor = QColorDialog::getColor(m_color, nullptr, "Escolher Cor da Trilha");
        if (newColor.isValid()) {
            setColor(newColor);
        }
    } else if (selected == deleteAction) {
        if (auto* sc = dynamic_cast<WorkspaceScene*>(scene())) {
            sc->deleteSelected();
        }
    }
    event->accept();
}

