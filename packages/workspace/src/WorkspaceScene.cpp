#include "WorkspaceScene.h"
#include "UndoCommands.h"
#include "CustomComponent.h"
#include <QPainter>
#include <QGraphicsSceneMouseEvent>
#include <QKeyEvent>
#include <QDebug>
#include <QPainterPathStroker>
#include <QTransform>
#include <cmath>
#include <QMessageBox>
#include <QGraphicsView>
#include "SmdWizardDialog.h"
#include "BlockEditor.h"

// ─────────────────────────────────────────────────────────────────────────────
// Static helpers
// ─────────────────────────────────────────────────────────────────────────────

static QPointF snapToGrid(const QPointF& p, double grid = 10.0) {
    return QPointF(std::round(p.x() / grid) * grid,
                   std::round(p.y() / grid) * grid);
}

// Returns 0 or 1 L-bend corner(s) between 'from' and 'to'.
// hFirst=true  → go horizontal first, then vertical
// hFirst=false → go vertical first, then horizontal
std::vector<QPointF> WorkspaceScene::lBend(const QPointF& from, const QPointF& to, bool hFirst) {
    double dx = std::abs(to.x() - from.x());
    double dy = std::abs(to.y() - from.y());
    if (dx < 1.0 || dy < 1.0) return {}; // already axis-aligned
    QPointF corner = hFirst ? QPointF(to.x(), from.y())
                            : QPointF(from.x(), to.y());
    return { corner };
}

// Internal: cancel and clean up routing state
void WorkspaceScene::cancelRouting() {
    m_routing = false;
    m_routeStartComp = nullptr;
    m_routeStartPin  = nullptr;
    m_routeStartIsJunction = false;
    m_routeStartJunctionPos = QPointF();
    // m_routeWaypoints holds PURE user-clicked points (no corners baked)
    m_routeWaypoints.clear();
    m_routeHFirst = true;
    if (m_routePreview) {
        removeItem(m_routePreview.get());
        m_routePreview.reset();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Constructor
// ─────────────────────────────────────────────────────────────────────────────
WorkspaceScene::WorkspaceScene(QObject* parent) : QGraphicsScene(parent) {
    m_undoStack = new QUndoStack(this);
    setSceneRect(-2000, -2000, 4000, 4000);
}

// ─────────────────────────────────────────────────────────────────────────────
// addComponent
// ─────────────────────────────────────────────────────────────────────────────
void WorkspaceScene::addComponent(ComponentItem* comp) {
    if (!comp) return;
    if (!m_components.contains(comp)) {
        m_components.append(comp);
        addItem(comp);
    }
}

void WorkspaceScene::removeComponent(ComponentItem* comp) {
    if (!comp) return;
    m_components.removeAll(comp);
    removeItem(comp);
}

void WorkspaceScene::addCable(ConnectionCable* cable) {
    if (!cable) return;
    if (!m_cables.contains(cable)) {
        m_cables.append(cable);
        addItem(cable);
        
        // Logical connection (only for direct non-junction connections)
        if (!cable->sourceIsJunction() && !cable->targetIsJunction()) {
            Pin* p1 = cable->sourceComponent() ? cable->sourceComponent()->getPinByName(cable->sourcePinName()) : nullptr;
            Pin* p2 = cable->targetComponent() ? cable->targetComponent()->getPinByName(cable->targetPinName()) : nullptr;
            if (p1 && p2) {
                p1->connectedToComponent = cable->targetComponent()->id();
                p1->connectedToPin = p2->name;
                p2->connectedToComponent = cable->sourceComponent()->id();
                p2->connectedToPin = p1->name;
            }
        }
        emit cableAdded(cable);
    }
}

void WorkspaceScene::removeCable(ConnectionCable* cable) {
    if (!cable) return;
    m_cables.removeAll(cable);
    removeItem(cable);
    
    // Break logical connection
    Pin* p1 = cable->sourceComponent() ? cable->sourceComponent()->getPinByName(cable->sourcePinName()) : nullptr;
    Pin* p2 = cable->targetComponent() ? cable->targetComponent()->getPinByName(cable->targetPinName()) : nullptr;
    if (p1) { p1->connectedToComponent = ""; p1->connectedToPin = ""; }
    if (p2) { p2->connectedToComponent = ""; p2->connectedToPin = ""; }
}

ComponentItem* WorkspaceScene::addComponent(const QString& type, const QString& name, const QPointF& pos, const QString& forcedId, bool allowOverlap) {

    static int idCounter = 1;
    QString id = forcedId;
    if (id.isEmpty()) {
        QString formattedType = type;
        if (type == "hcsr04") formattedType = "hc-sr04";
        id = QString("%1-%2").arg(formattedType).arg(idCounter++);
    } else {
        int sep = id.lastIndexOf('-');
        if (sep == -1) sep = id.lastIndexOf('_');
        if (sep != -1) {
            bool ok = false;
            int numericSuffix = id.mid(sep + 1).toInt(&ok);
            if (ok && numericSuffix >= idCounter) {
                idCounter = numericSuffix + 1;
            }
        }
    }

    ComponentItem* item = nullptr;
    QString compName = forcedId.isEmpty() ? id : name;
    if (compName.isEmpty()) compName = id;

    if (type.startsWith("mini-")) {
        SmdWizardDialog::ComponentType wizardType;
        if (type == "mini-resistor") wizardType = SmdWizardDialog::RESISTOR;
        else if (type == "mini-capacitor") wizardType = SmdWizardDialog::CAPACITOR;
        else wizardType = SmdWizardDialog::LED;

        SmdWizardDialog dlg(wizardType, this->views().isEmpty() ? nullptr : this->views().first());
        if (dlg.exec() != QDialog::Accepted) return nullptr;
        QJsonObject props = dlg.getProperties();
        QString smdSize = props.value("smdSize").toString();
        
        if (type == "mini-resistor") {
            item = new ResistorItem(id, compName);
            QString resStr = props.value("resistance").toString();
            resStr.remove("Ω");
            double mult = 1.0;
            if (resStr.endsWith("K")) { mult = 1000.0; resStr.chop(1); }
            else if (resStr.endsWith("M")) { mult = 1000000.0; resStr.chop(1); }
            static_cast<ResistorItem*>(item)->setResistance(resStr.toDouble() * mult);
        } else if (type == "mini-capacitor") {
            item = new CapacitorItem(id, compName);
        } else {
            item = new LEDItem(id, compName);
        }
        item->setProperty("isSMD", true);
        item->setProperty("smdSize", smdSize);
        item->setProperty("smdProps", props);
        item->updateLayoutForSMD(smdSize);
    }
    else if (type == "esp32")              item = new ESP32Item(id, compName);
    else if (type == "led")           item = new LEDItem(id, compName);
    else if (type == "rgb_led")       item = new RGBLEDItem(id, compName);
    else if (type == "button")        item = new ButtonItem(id, compName);
    else if (type == "resistor")      item = new ResistorItem(id, compName);
    else if (type == "capacitor")     item = new CapacitorItem(id, compName);
    else if (type == "potentiometer") item = new PotentiometerItem(id, compName);
    else if (type == "ldr")           item = new LdrItem(id, compName);
    else if (type == "buzzer")        item = new BuzzerItem(id, compName);
    else if (type == "motor")         item = new MotorItem(id, compName);
    else if (type == "relay")         item = new RelayItem(id, compName);
    else if (type == "dht22")         item = new DHT22Item(id, compName);
    else if (type == "hcsr04")        item = new HCSR04Item(id, compName);
    else if (type == "gnd")           item = new GndItem(id, compName);
    else if (type == "lamp")          item = new LampItem(id, compName);
    else {
        for (const auto& def : CustomComponentManager::instance().registeredComponents()) {
            if (def.type == type) {
                item = new CustomComponentItem(id, def);
                break;
            }
        }
    }

    if (item) {
        double snapX = std::round(pos.x() / 10.0) * 10.0;
        double snapY = std::round(pos.y() / 10.0) * 10.0;
        QPointF candidatePos(snapX, snapY);

        if (!allowOverlap) {
            bool found = false;
            int attempts = 0;
            while (!found && attempts < 100) {
                bool collides = false;
                item->setPos(candidatePos);
                QRectF myRect = item->mapToScene(item->boundingRect()).boundingRect().adjusted(3, 3, -3, -3);
                for (auto* comp : m_components) {
                    QRectF other = comp->mapToScene(comp->boundingRect()).boundingRect().adjusted(3, 3, -3, -3);
                    if (myRect.intersects(other)) { collides = true; break; }
                }
                if (!collides) found = true;
                else { ++attempts; candidatePos = QPointF(snapX + attempts * 40.0, snapY); }
            }
        }

        item->setPos(candidatePos);
        
        // Se for a placa inicial (esp32), adicionamos diretamente sem colocar no UndoStack
        // Isso evita que o "Desfazer" remova a placa principal do projeto.
        if (type == "esp32") {
            addComponent(item);
        } else {
            m_undoStack->push(new AddComponentCommand(this, item));
        }

        connect(item, &ComponentItem::componentMoved, this, &WorkspaceScene::updateCablePaths);
        connect(item, &ComponentItem::rightClicked, this, [this, item](const QPointF& gPos) {
            emit rightClickedComponent(item, gPos);
        });
        connect(item, &ComponentItem::doubleClicked, this, [this, item](const QPointF& gPos) {
            emit doubleClickedComponent(item, gPos);
        });

        clearSelection();
        item->setSelected(true);
        emit selectionChanged(item);
        emit componentAdded(item);

        if (m_smartConnectionEnabled && type != "esp32" && !m_isApplyingSmartConnection) {
            m_isApplyingSmartConnection = true;
            applySmartConnection(item);
            m_isApplyingSmartConnection = false;
        }
    }
    return item;
}

// ─────────────────────────────────────────────────────────────────────────────
// connectPins
// ─────────────────────────────────────────────────────────────────────────────
ConnectionCable* WorkspaceScene::connectPins(ComponentItem* srcComp, const QString& srcPinName,
                                   ComponentItem* tgtComp, const QString& tgtPinName,
                                   const std::vector<QPointF>& waypoints,
                                   bool startHFirst,
                                   bool srcIsJunction, const QPointF& srcJunctionPos,
                                   bool tgtIsJunction, const QPointF& tgtJunctionPos) {
    if (!srcComp || !tgtComp || srcComp == tgtComp) return nullptr;

    Pin* p1 = srcComp->getPinByName(srcPinName);
    Pin* p2 = tgtComp->getPinByName(tgtPinName);

    if (!p1 || !p2) {
        // qDebug() << "[CABLE ERROR] Pino não encontrado:"
        //          << srcPinName << "em" << srcComp->name()
        //          << "ou" << tgtPinName << "em" << tgtComp->name();
        return nullptr;
    }

    // Only avoid duplicate if it's a direct pin-to-pin connection without junctions
    if (!srcIsJunction && !tgtIsJunction) {
        for (auto* cable : m_cables) {
            if (cable->sourceIsJunction() || cable->targetIsJunction()) continue;
            bool fwd = (cable->sourceComponent() == srcComp && cable->sourcePinName() == p1->name &&
                        cable->targetComponent() == tgtComp && cable->targetPinName() == p2->name);
            bool rev = (cable->sourceComponent() == tgtComp && cable->sourcePinName() == p2->name &&
                        cable->targetComponent() == srcComp && cable->targetPinName() == p1->name);
            if (fwd || rev) return cable;
        }
    }

    // Metadata: only track pin-to-pin connections in Pin objects
    if (!srcIsJunction && !tgtIsJunction) {
        p1->connectedToComponent = tgtComp->id();
        p1->connectedToPin       = p2->name;
        p2->connectedToComponent = srcComp->id();
        p2->connectedToPin       = p1->name;
    }

    // qDebug() << "[CABLE] Conectado:" << srcComp->name() << "::" << p1->name
    //          << "->" << tgtComp->name() << "::" << p2->name
    //          << " | waypoints:" << waypoints.size()
    //          << " | srcJunc:" << srcIsJunction << " | tgtJunc:" << tgtIsJunction;

    ConnectionCable* cable = new ConnectionCable(srcComp, p1->name, tgtComp, p2->name, waypoints, startHFirst,
                                                 srcIsJunction, srcJunctionPos, tgtIsJunction, tgtJunctionPos);

    m_undoStack->push(new AddCableCommand(this, cable));
    return cable;
}
// ─────────────────────────────────────────────────────────────────────────────
// deleteSelected
// ─────────────────────────────────────────────────────────────────────────────
void WorkspaceScene::deleteSelected() {
    if (parent()) {
        auto* editor = parent()->findChild<BlockEditor*>();
        if (editor && editor->isVisible()) {
            return;
        }
    }

    auto selectedList = selectedItems();
    if (selectedList.isEmpty()) return;

    m_undoStack->beginMacro("Deletar Seleção");

    for (auto* item : selectedList) {
        if (item->type() == ConnectionCable::Type) {
            ConnectionCable* cable = static_cast<ConnectionCable*>(item);
            m_undoStack->push(new RemoveCableCommand(this, cable));
            // qDebug() << "[CABLE DELETE]" << cable->sourcePinName() << "->" << cable->targetPinName();
        }
    }
    for (auto* item : selectedList) {
        ComponentItem* comp = dynamic_cast<ComponentItem*>(item);
        if (!comp || comp->componentType() == "esp32") continue;
        
        // Also remove cables attached to this component
        QVector<ConnectionCable*> toDelete;
        for (auto* cable : m_cables)
            if (cable->sourceComponent() == comp || cable->targetComponent() == comp)
                toDelete.append(cable);
        
        for (auto* cable : toDelete) {
            m_undoStack->push(new RemoveCableCommand(this, cable));
        }

        m_undoStack->push(new RemoveComponentCommand(this, comp));
        // qDebug() << "[COMPONENT DELETE]" << comp->name();
    }
    
    m_undoStack->endMacro();
    emit selectionChanged(nullptr);
}

// ─────────────────────────────────────────────────────────────────────────────
// clearWorkspace
// ─────────────────────────────────────────────────────────────────────────────
void WorkspaceScene::clearWorkspace() {
    clearSelection();
    cancelRouting();
    
    // WARNING: We must clear the undo stack before deleting the items,
    // otherwise undo commands might hold dangling pointers to deleted items.
    if (m_undoStack) {
        m_undoStack->clear();
    }
    
    for (auto* cable : m_cables) { removeItem(cable); delete cable; }
    m_cables.clear();
    for (auto* comp : m_components) { removeItem(comp); delete comp; }
    m_components.clear();
    // qDebug() << "[WORKSPACE] Limpo";
    emit selectionChanged(nullptr);
}

// ─────────────────────────────────────────────────────────────────────────────
// updateCablePaths
// ─────────────────────────────────────────────────────────────────────────────
void WorkspaceScene::updateCablePaths() {
    for (auto* cable : m_cables) cable->updatePath();
}

// ─────────────────────────────────────────────────────────────────────────────
// drawBackground
// ─────────────────────────────────────────────────────────────────────────────
void WorkspaceScene::drawBackground(QPainter* painter, const QRectF& rect) {
    painter->fillRect(rect, QColor(250, 252, 255));
    // Subtle 10px grid dots — softer opacity so they don't compete with routes
    painter->setPen(QPen(QColor(203, 213, 225, 80), 1.0));
    double left = std::floor(rect.left() / 10.0) * 10.0;
    double top  = std::floor(rect.top()  / 10.0) * 10.0;
    for (double x = left; x < rect.right(); x += 10.0)
        for (double y = top; y < rect.bottom(); y += 10.0)
            painter->drawPoint(QPointF(x, y));
}

// ─────────────────────────────────────────────────────────────────────────────
// updateRoutingPreview
// Builds a live preview through:  srcPin → [committed pure waypoints] → cursor
// Each segment pair gets one L-bend corner inserted by lBend().
// ─────────────────────────────────────────────────────────────────────────────
void WorkspaceScene::updateRoutingPreview(const QPointF& cursorPos) {
    if (!m_routing || !m_routePreview) return;

    QPointF snappedCursor = snapToGrid(cursorPos, 10.0);

    // m_routeWaypoints[0] = source pin scene pos
    // m_routeWaypoints[1..] = user-clicked intermediate pure points
    // Add the live cursor as temporary last point
    std::vector<QPointF> pts = m_routeWaypoints;
    pts.push_back(snappedCursor);

    // Build the exact same manual path structure as the final cable (using m_routeStartHFirst and chamfering)
    std::vector<QPointF> ortho = ConnectionCable::buildOrthoChain(pts, m_routeStartHFirst);
    std::vector<QPointF> chamfered = ConnectionCable::chamfer(ortho, 10.0);

    QPainterPath path;
    if (!chamfered.empty()) {
        path.moveTo(chamfered[0]);
        for (size_t i = 1; i < chamfered.size(); ++i) {
            path.lineTo(chamfered[i]);
        }
    }

    m_routePreview->setPath(path);
}

// ─────────────────────────────────────────────────────────────────────────────
// mousePressEvent  (KiCad-style routing)
//
// IMPORTANT: m_routeWaypoints stores ONLY pure user-clicked points.
//   - [0] = source pin scene position (anchor, never a waypoint passed to cable)
//   - [1..N] = user-clicked intermediate points (passed as waypoints to cable)
//
// L-bend corners are NOT stored here — they are computed on the fly by
// ConnectionCable::buildOrthoChain and WorkspaceScene::updateRoutingPreview.
// ─────────────────────────────────────────────────────────────────────────────
void WorkspaceScene::mousePressEvent(QGraphicsSceneMouseEvent* event) {
    if (m_isSimulating) {
        // During simulation, allow propagation to components for interaction (buttons/pots)
        // but block routing (clicking on pins or empty space)
        QGraphicsScene::mousePressEvent(event);
        return;
    }
    const QPointF scenePos = event->scenePos();
    m_lastMouseScenePos = scenePos;

    // ── Right click: cancel routing or pass through ───────────────────────
    if (event->button() == Qt::RightButton) {
        if (m_routing) {
            // qDebug() << "[ROUTING] Cancelado por clique direito";
            cancelRouting();
            event->accept();
        } else {
            QGraphicsScene::mousePressEvent(event);
        }
        return;
    }

    if (event->button() != Qt::LeftButton) {
        QGraphicsScene::mousePressEvent(event);
        return;
    }

    // ══ LEFT CLICK ════════════════════════════════════════════════════════

    if (!m_routing) {
        // Try to start routing from a pin
        for (auto* comp : m_components) {
            Pin* pin = comp->findPinAt(scenePos, 12.0);
            if (pin) {
                m_routing        = true;
                m_routeStartComp = comp;
                m_routeStartPin  = pin;
                m_routeHFirst    = true;
                m_routeStartHFirst = true;
                m_routeStartIsJunction = false;

                // Seed with ONLY the source pin position
                m_routeWaypoints.clear();
                m_routeWaypoints.push_back(comp->getPinScenePos(*pin));

                // qDebug() << "[ROUTING] Iniciado em" << comp->name() << "::" << pin->name;

                if (m_routePreview) { 
                    removeItem(m_routePreview.get()); 
                    m_routePreview.reset(); 
                }
                m_routePreview = std::make_unique<QGraphicsPathItem>();
                
                // Determine preview color by pin type
                QColor baseColor;
                if (pin->name.contains("3V3") || pin->name.contains("5V") || pin->name.contains("VCC")) {
                    baseColor = QColor(239, 68, 68);
                } else if (pin->name.contains("GND")) {
                    baseColor = QColor(75, 85, 99);
                } else {
                    int hash = (comp->id().length() + pin->name.length()) % 4;
                    if (hash == 0) baseColor = QColor(16, 185, 129);
                    else if (hash == 1) baseColor = QColor(245, 158, 11);
                    else if (hash == 2) baseColor = QColor(14, 165, 233);
                    else baseColor = QColor(168, 85, 247);
                }

                m_routePreview->setPen(QPen(baseColor, 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                m_routePreview->setZValue(100);
                addItem(m_routePreview.get());

                updateRoutingPreview(scenePos);
                event->accept();
                return;
            }
        }

        // Normal selection click
        QGraphicsScene::mousePressEvent(event);
        auto selected = selectedItems();
        
        m_initialPositions.clear();
        for (auto* item : selected) {
            if (auto* comp = dynamic_cast<ComponentItem*>(item)) {
                m_initialPositions[comp] = comp->pos();
            }
        }

        emit selectionChanged(selected.isEmpty() ? nullptr : dynamic_cast<ComponentItem*>(selected.first()));
        return;
    }

    // ══ ALREADY ROUTING ══════════════════════════════════════════════════

    // 1) Check if clicked on a destination pin → finalise
    for (auto* comp : m_components) {
        if (comp == m_routeStartComp && !m_routeStartIsJunction) continue;
        Pin* pin = comp->findPinAt(scenePos, 14.0);
        if (pin) {
            // Short-circuit protection (5V/3V3 directly to GND)
            if (!m_routeStartIsJunction && m_routeStartPin) {
                QString p1 = m_routeStartPin->name.toUpper();
                QString p2 = pin->name.toUpper();
                auto isPower = [](const QString& p) { return p.contains("5V") || p.contains("3V3") || p.contains("VCC"); };
                auto isGnd = [](const QString& p) { return p.contains("GND"); };
                
                if ((isPower(p1) && isGnd(p2)) || (isGnd(p1) && isPower(p2))) {
                    QMessageBox::warning(nullptr, "Curto-Circuito Detectado!",
                        "Aviso: Você tentou ligar energia (5V/3V3) diretamente ao Terra (GND).\n\n"
                        "Na vida real, isso causaria um curto-circuito e queimaria a placa! A ligação foi impedida.");
                    cancelRouting();
                    event->accept();
                    return;
                }
            }

            // Pass only the intermediate points (if any). 
            // If started from junction, m_routeWaypoints[0] is the junction pos, so waypoints should be m_routeWaypoints[1..]
            std::vector<QPointF> waypoints(m_routeWaypoints.begin() + 1, m_routeWaypoints.end());

            connectPins(m_routeStartComp, m_routeStartPin->name, comp, pin->name, waypoints, m_routeStartHFirst,
                        m_routeStartIsJunction, m_routeStartJunctionPos, false, QPointF());
            cancelRouting();
            event->accept();
            return;
        }
    }

    // 1b) Check if clicked on an existing track (cable) → T-junction finalise
    for (auto* cable : m_cables) {
        if (cable->sourceComponent() == m_routeStartComp && !m_routeStartIsJunction) continue;
        QPainterPath path = cable->path();
        QPainterPathStroker stroker;
        stroker.setWidth(10.0);
        QPainterPath clickArea = stroker.createStroke(path);
        if (clickArea.contains(scenePos)) {
            QPointF snapped = snapToGrid(scenePos, 10.0);
            ComponentItem* targetComp = cable->sourceComponent();
            QString targetPinName = cable->sourcePinName();
            
            std::vector<QPointF> waypoints(m_routeWaypoints.begin() + 1, m_routeWaypoints.end());
            
            connectPins(m_routeStartComp, m_routeStartPin->name, targetComp, targetPinName, waypoints, m_routeStartHFirst,
                        m_routeStartIsJunction, m_routeStartJunctionPos, true, snapped);
            
            cancelRouting();
            event->accept();
            return;
        }
    }

    // 2) Empty space → commit just the raw snapped click point (NO baked corners)
    QPointF snapped = snapToGrid(scenePos, 10.0);
    m_routeWaypoints.push_back(snapped);
    m_routeHFirst = !m_routeHFirst; // toggle bend direction for next segment

    // qDebug() << "[ROUTING] Waypoint adicionado:" << snapped
    //          << "| total wpts=" << m_routeWaypoints.size()
    //          << "| próximo hFirst=" << m_routeHFirst;

    updateRoutingPreview(scenePos);
    event->accept();
}

// ─────────────────────────────────────────────────────────────────────────────
// mouseMoveEvent
// ─────────────────────────────────────────────────────────────────────────────
void WorkspaceScene::mouseMoveEvent(QGraphicsSceneMouseEvent* event) {
    if (m_isSimulating) {
        QGraphicsScene::mouseMoveEvent(event);
        return;
    }
    if (m_routing) {
        m_lastMouseScenePos = event->scenePos();
        QPointF targetPos = event->scenePos();
        bool overCable = false;

        // Visual feedback/snap to cables
        for (auto* cable : m_cables) {
            if (cable->sourceComponent() == m_routeStartComp && !m_routeStartIsJunction) continue;
            QPainterPathStroker stroker;
            stroker.setWidth(12.0);
            QPainterPath area = stroker.createStroke(cable->path());
            if (area.contains(event->scenePos())) {
                targetPos = snapToGrid(event->scenePos(), 10.0);
                overCable = true;
                break;
            }
        }
        
        // Change cursor to indicate connection possibility
        if (overCable) {
            if (this->views().count() > 0)
                this->views().first()->setCursor(Qt::CrossCursor);
        } else {
            if (this->views().count() > 0)
                this->views().first()->setCursor(Qt::ArrowCursor);
        }

        updateRoutingPreview(targetPos);
        event->accept();
        return;
    }
    QGraphicsScene::mouseMoveEvent(event);
}

// ─────────────────────────────────────────────────────────────────────────────
// mouseReleaseEvent
// ─────────────────────────────────────────────────────────────────────────────
void WorkspaceScene::mouseReleaseEvent(QGraphicsSceneMouseEvent* event) {
    if (m_isSimulating) {
        QGraphicsScene::mouseReleaseEvent(event);
        return;
    }
    QGraphicsScene::mouseReleaseEvent(event);
    
    if (!m_routing && event->button() == Qt::LeftButton) {
        QVector<MoveComponentCommand*> moveCommands;
        for (auto it = m_initialPositions.begin(); it != m_initialPositions.end(); ++it) {
            ComponentItem* comp = it.key();
            QPointF oldPos = it.value();
            if (comp->pos() != oldPos) {
                moveCommands.append(new MoveComponentCommand(comp, oldPos, comp->pos()));
            }
        }

        if (!moveCommands.isEmpty()) {
            m_undoStack->beginMacro("Mover Componentes");
            for (auto* cmd : moveCommands) {
                m_undoStack->push(cmd);
            }
            m_undoStack->endMacro();
        }
        m_initialPositions.clear();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// keyPressEvent
// ─────────────────────────────────────────────────────────────────────────────
void WorkspaceScene::keyPressEvent(QKeyEvent* event) {
    if (m_routing && event->key() == Qt::Key_Escape) {
        // qDebug() << "[ROUTING] Cancelado por Esc";
        cancelRouting();
        event->accept();
        return;
    }
    if (m_routing && event->key() == Qt::Key_Space) {
        m_routeStartHFirst = !m_routeStartHFirst;
        // Keep m_routeHFirst in sync for the active segment
        bool activeHFirst = m_routeStartHFirst;
        if ((m_routeWaypoints.size() - 1) % 2 != 0) {
            activeHFirst = !activeHFirst;
        }
        m_routeHFirst = activeHFirst;

        updateRoutingPreview(m_lastMouseScenePos);
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
        if (parent()) {
            auto* editor = parent()->findChild<BlockEditor*>();
            if (editor && editor->isVisible()) {
                event->ignore();
                return;
            }
        }
        if (m_isSimulating) {
            event->ignore();
            return;
        }

        // Only delete if the scene view has focus (not if something in the side panel like Block Editor has focus)
        bool hasFocus = false;
        for (auto* view : views()) {
            if (view->hasFocus() || view->viewport()->hasFocus()) {
                hasFocus = true;
                break;
            }
        }
        
        if (hasFocus) {
            deleteSelected();
            event->accept();
            return;
        }
    }
    QGraphicsScene::keyPressEvent(event);
}

// ─────────────────────────────────────────────────────────────────────────────
// applySmartConnection
// ─────────────────────────────────────────────────────────────────────────────
void WorkspaceScene::applySmartConnection(ComponentItem* newComp) {
    if (!newComp) return;

    ESP32Item* esp32 = nullptr;
    for (auto* comp : m_components) {
        if (comp->componentType() == "esp32") {
            esp32 = static_cast<ESP32Item*>(comp);
            break;
        }
    }
    if (!esp32) return; // Silent abort if no ESP32

    QSet<QString> occupiedPins;
    bool esp32HasGnd = false;
    for (auto* cable : m_cables) {
        if (cable->sourceComponent() == esp32) {
            if (Pin* p = esp32->getPinByName(cable->sourcePinName())) occupiedPins.insert(p->name);
        }
        if (cable->targetComponent() == esp32) {
            if (Pin* p = esp32->getPinByName(cable->targetPinName())) occupiedPins.insert(p->name);
        }
        
        if ((cable->sourceComponent() == esp32 && cable->sourcePinName() == "GND.1" && cable->targetComponent()->componentType() == "gnd") ||
            (cable->targetComponent() == esp32 && cable->targetPinName() == "GND.1" && cable->sourceComponent()->componentType() == "gnd")) {
            esp32HasGnd = true;
        }
    }

    auto getFreeGpio = [&]() -> QString {
        QStringList preferred = {"GPIO4", "GPIO5", "GPIO6", "GPIO7", "GPIO8", "GPIO9", "GPIO10", "GPIO2", "GPIO3", "GPIO1", "GPIO0"}; // Valid ESP32-C3 pins
        for (const QString& pin : preferred) {
            if (!occupiedPins.contains(pin)) {
                occupiedPins.insert(pin); // mark as used for this run
                return pin;
            }
        }
        return "";
    };

    QString type = newComp->componentType();
    
    auto checkGpioAndWarn = [](const QString& pin) -> bool {
        if (pin.isEmpty()) {
            QMessageBox::warning(nullptr, "Conexão Inteligente", "Não há pinos GPIO livres suficientes na placa para conectar este componente automaticamente!");
            return false;
        }
        return true;
    };

    m_undoStack->beginMacro("Conexão Inteligente");
    
    // Add GND to ESP32 if not already present
    if (!esp32HasGnd) {
        QPointF espGndPos = esp32->pos() + QPointF(-60, 0);
        if (Pin* gndPin = esp32->getPinByName("GND.1")) {
            espGndPos = esp32->pos() + gndPin->localPos + QPointF(-40, 0);
        }
        ComponentItem* espGnd = addComponent("gnd", "", espGndPos, "", true);
        if (espGnd) {
            espGnd->setRotation(90);
            connectPins(esp32, "GND.1", espGnd, "GND");
            occupiedPins.insert("GND.1");
        }
    }
    
    auto connectToGnd = [&](ComponentItem* comp, const QString& pinName, const QPointF& offset) {
        QPointF targetPos = comp->pos() + offset;
        if (Pin* p = comp->getPinByName(pinName)) {
            targetPos.setX(comp->pos().x() + p->localPos.x());
        }
        ComponentItem* gndComp = addComponent("gnd", "", targetPos, "", true);
        if (gndComp) {
            connectPins(comp, pinName, gndComp, "GND", {}, false);
        }
    };

    if (type == "led") {
        QString gpio = getFreeGpio();
        if (!checkGpioAndWarn(gpio)) { m_undoStack->endMacro(); return; }

        QPointF rPos = newComp->pos();
        if (Pin* p = esp32->getPinByName(gpio)) {
            rPos.setY(esp32->pos().y() + p->localPos.y());
            rPos.setX(esp32->pos().x() + p->localPos.x() + (p->localPos.x() >= 0 ? 60 : -60));
        }

        ComponentItem* resistor = addComponent("resistor", "", rPos, "", true);
        if (resistor) {
            if (auto* r = dynamic_cast<ResistorItem*>(resistor)) r->setResistance(220.0);
            QString pEsp = "1", pLed = "2";
            if (Pin* p = esp32->getPinByName(gpio)) { if (p->localPos.x() < 0) { pEsp = "2"; pLed = "1"; } }
            connectPins(esp32, gpio, resistor, pEsp);
            connectPins(resistor, pLed, newComp, "Anode");
            connectToGnd(newComp, "Cathode", QPointF(20, 50));
        }
    } else if (type == "rgb_led") {
        QString rG = getFreeGpio();
        QString gG = getFreeGpio();
        QString bG = getFreeGpio();
        if (!checkGpioAndWarn(rG) || !checkGpioAndWarn(gG) || !checkGpioAndWarn(bG)) { m_undoStack->endMacro(); return; }

        QPointF pR = newComp->pos(), pG = newComp->pos(), pB = newComp->pos();

        if (Pin* pinR = esp32->getPinByName(rG)) {
            pR.setY(esp32->pos().y() + pinR->localPos.y());
            pR.setX(esp32->pos().x() + pinR->localPos.x() + (pinR->localPos.x() >= 0 ? 60 : -60));
        }
        if (Pin* pinG = esp32->getPinByName(gG)) {
            pG.setY(esp32->pos().y() + pinG->localPos.y());
            pG.setX(esp32->pos().x() + pinG->localPos.x() + (pinG->localPos.x() >= 0 ? 60 : -60));
        }
        if (Pin* pinB = esp32->getPinByName(bG)) {
            pB.setY(esp32->pos().y() + pinB->localPos.y());
            pB.setX(esp32->pos().x() + pinB->localPos.x() + (pinB->localPos.x() >= 0 ? 60 : -60));
        }

        ComponentItem* rR = addComponent("resistor", "", pR, "", true);
        ComponentItem* rG_res = addComponent("resistor", "", pG, "", true);
        ComponentItem* rB = addComponent("resistor", "", pB, "", true);

        if (rR && rG_res && rB) {
            if (auto* r = dynamic_cast<ResistorItem*>(rR)) r->setResistance(220.0);
            if (auto* r = dynamic_cast<ResistorItem*>(rG_res)) r->setResistance(220.0);
            if (auto* r = dynamic_cast<ResistorItem*>(rB)) r->setResistance(220.0);
            QString pEspR = "1", pLedR = "2", pEspG = "1", pLedG = "2", pEspB = "1", pLedB = "2";
            if (Pin* p = esp32->getPinByName(rG)) { if (p->localPos.x() < 0) { pEspR = "2"; pLedR = "1"; } }
            if (Pin* p = esp32->getPinByName(gG)) { if (p->localPos.x() < 0) { pEspG = "2"; pLedG = "1"; } }
            if (Pin* p = esp32->getPinByName(bG)) { if (p->localPos.x() < 0) { pEspB = "2"; pLedB = "1"; } }

            connectPins(esp32, rG, rR, pEspR);
            connectPins(rR, pLedR, newComp, "R");

            connectPins(esp32, gG, rG_res, pEspG);
            connectPins(rG_res, pLedG, newComp, "G");

            connectPins(esp32, bG, rB, pEspB);
            connectPins(rB, pLedB, newComp, "B");

            connectToGnd(newComp, "GND", QPointF(-5, 60));
        }
    } else if (type == "button") {
        QString gpio = getFreeGpio();
        if (!checkGpioAndWarn(gpio)) { m_undoStack->endMacro(); return; }

        connectPins(newComp, "1", esp32, gpio, {}, false);
        connectToGnd(newComp, "2", QPointF(20, 50));

    } else if (type == "buzzer") {
        QString gpio = getFreeGpio();
        if (!checkGpioAndWarn(gpio)) { m_undoStack->endMacro(); return; }

        connectPins(newComp, "1", esp32, gpio, {}, false);
        connectToGnd(newComp, "2", QPointF(20, 50));

    } else if (type == "potentiometer") {
        QString gpio = getFreeGpio();
        if (!checkGpioAndWarn(gpio)) { m_undoStack->endMacro(); return; }

        connectPins(newComp, "1", esp32, "3V3", {}, false);
        connectPins(newComp, "2", esp32, gpio, {}, false);
        connectToGnd(newComp, "3", QPointF(20, 60));

    } else if (type == "ldr") {
        QString gpio = getFreeGpio();
        if (!checkGpioAndWarn(gpio)) { m_undoStack->endMacro(); return; }

        connectPins(newComp, "1", esp32, "3V3", {}, false);
        connectPins(newComp, "2", esp32, gpio, {}, false);
        
        QPointF rPos = newComp->pos() + QPointF(0, 50);
        ComponentItem* resistor = addComponent("resistor", "", rPos, "", true);
        if (resistor) {
            if (auto* r = dynamic_cast<ResistorItem*>(resistor)) r->setResistance(10000.0);
            connectPins(newComp, "2", resistor, "1", {}, false);
            connectToGnd(resistor, "2", QPointF(0, 50));
        }

    } else if (type == "dht22") {
        QString gpio = getFreeGpio();
        if (!checkGpioAndWarn(gpio)) { m_undoStack->endMacro(); return; }

        connectPins(newComp, "VCC", esp32, "3V3", {}, false);
        connectPins(newComp, "DATA", esp32, gpio, {}, false);
        connectToGnd(newComp, "GND", QPointF(20, 60));
        
    } else if (type == "hcsr04") {
        QString tG = getFreeGpio();
        QString eG = getFreeGpio();
        if (!checkGpioAndWarn(tG) || !checkGpioAndWarn(eG)) { m_undoStack->endMacro(); return; }

        connectPins(newComp, "VCC", esp32, "5V", {}, false);
        connectPins(newComp, "TRIG", esp32, tG, {}, false);
        connectPins(newComp, "ECHO", esp32, eG, {}, false);
        connectToGnd(newComp, "GND", QPointF(30, 60));
    } else if (type == "relay") {
        QString gpio = getFreeGpio();
        if (!checkGpioAndWarn(gpio)) { m_undoStack->endMacro(); return; }

        connectPins(newComp, "IN", esp32, gpio, {}, false);
        connectPins(newComp, "VCC", esp32, "5V", {}, false);
        connectToGnd(newComp, "GND", QPointF(-20, 60));
    }

    m_undoStack->endMacro();
}
