#pragma once
#include <QGraphicsItem>
#include <QPen>
#include <QPainterPath>
#include "ComponentItem.h"

class ConnectionCable : public QGraphicsItem {
public:
    ConnectionCable(ComponentItem* sourceComp, const QString& sourcePin,
                    ComponentItem* targetComp, const QString& targetPin,
                    const std::vector<QPointF>& manualWaypoints = {},
                    bool startHFirst = true,
                    bool srcIsJunc = false, const QPointF& srcJuncPos = QPointF(),
                    bool tgtIsJunc = false, const QPointF& tgtJuncPos = QPointF(),
                    QGraphicsItem* parent = nullptr);

    ComponentItem* sourceComponent() const { return m_sourceComp; }
    QString sourcePinName() const { return m_sourcePin; }
    ComponentItem* targetComponent() const { return m_targetComp; }
    QString targetPinName() const { return m_targetPin; }

    QPainterPath path() const { return m_path; }

    // Manual waypoints between the two pin endpoints (not including pins themselves)
    const std::vector<QPointF>& manualWaypoints() const { return m_manualWaypoints; }
    bool startHFirst() const { return m_startHFirst; }
    
    bool targetIsJunction() const { return m_targetIsJunction; }
    QPointF junctionPos() const { return m_junctionPos; }
    void setTargetIsJunction(bool isJunction, const QPointF& pos) {
        m_targetIsJunction = isJunction;
        m_junctionPos = pos;
        updatePath();
    }

    bool sourceIsJunction() const { return m_sourceIsJunction; }
    QPointF sourceJunctionPos() const { return m_sourceJunctionPos; }
    void setSourceIsJunction(bool isJunction, const QPointF& pos) {
        m_sourceIsJunction = isJunction;
        m_sourceJunctionPos = pos;
        updatePath();
    }

    void updatePath();

    QColor color() const { return m_color; }
    void setColor(const QColor& color) { m_color = color; update(); }

    // Build a strictly orthogonal chain through ordered points, inserting L-bends
    static std::vector<QPointF> buildOrthoChain(const std::vector<QPointF>& pts, bool startHFirst = true);

    // Chamfer a poly-line
    static std::vector<QPointF> chamfer(const std::vector<QPointF>& pts, double radius = 10.0);

    QRectF boundingRect() const override;
    QPainterPath shape() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

    enum { Type = UserType + 1 };
    int type() const override { return Type; }

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) override;
    void hoverMoveEvent(QGraphicsSceneHoverEvent* event) override;
    void contextMenuEvent(QGraphicsSceneContextMenuEvent* event) override;

private:
    ComponentItem* m_sourceComp;
    QString m_sourcePin;
    ComponentItem* m_targetComp;
    QString m_targetPin;

    QPainterPath m_path;
    QColor m_color;

    // ── Manual routing (KiCad style) ─────────────────────────────────────
    std::vector<QPointF> m_manualWaypoints; // User-defined intermediate points
    bool m_startHFirst;                    // Cable starts horizontal first or vertical first
    
    bool m_sourceIsJunction = false;
    QPointF m_sourceJunctionPos;
    
    bool m_targetIsJunction = false;
    QPointF m_junctionPos;

    // ── Drag-handle editing (post-route adjustments) ─────────────────────
    // The "drag-handle" system is simplified for manual routes:
    // selecting a cable shows square handles at each waypoint; drag to move.
    int m_draggedWaypointIdx = -1;
    QPointF m_dragStartMousePos;
    QPointF m_dragStartWptPos;
};
