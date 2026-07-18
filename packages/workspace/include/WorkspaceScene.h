#pragma once
#include <QGraphicsScene>
#include <QGraphicsPathItem>
#include <QUndoStack>
#include <QMap>
#include <QVector>
#include "ComponentItem.h"
#include "ConnectionCable.h"

class WorkspaceScene : public QGraphicsScene {
    Q_OBJECT
public:
    explicit WorkspaceScene(QObject* parent = nullptr);
    ~WorkspaceScene() = default;

    QUndoStack* undoStack() const { return m_undoStack; }

    // Internal methods for UndoCommands
    void addComponent(ComponentItem* comp);
    void removeComponent(ComponentItem* comp);
    void addCable(ConnectionCable* cable);
    void removeCable(ConnectionCable* cable);

    ComponentItem* addComponent(const QString& type, const QString& name, const QPointF& pos, const QString& forcedId = QString(), bool allowOverlap = false);
    ConnectionCable* connectPins(ComponentItem* srcComp, const QString& srcPin, ComponentItem* tgtComp, const QString& tgtPin,
                         const std::vector<QPointF>& manualWaypoints = {}, bool startHFirst = true,
                         bool srcIsJunction = false, const QPointF& srcJunctionPos = QPointF(),
                         bool tgtIsJunction = false, const QPointF& tgtJunctionPos = QPointF());
    void deleteSelected();
    void clearWorkspace();
    void updateCablePaths();

    QVector<ComponentItem*> components() const { return m_components; }
    QVector<ConnectionCable*> cables() const { return m_cables; }

    void setSimulating(bool simulating) { m_isSimulating = simulating; }
    bool isSimulating() const { return m_isSimulating; }

    void setSmartConnectionEnabled(bool enabled) { m_smartConnectionEnabled = enabled; }
    bool isSmartConnectionEnabled() const { return m_smartConnectionEnabled; }

signals:
    void selectionChanged(ComponentItem* selectedComp);
    void rightClickedComponent(ComponentItem* comp, const QPointF& globalPos);
    void doubleClickedComponent(ComponentItem* comp, const QPointF& globalPos);
    void componentAdded(ComponentItem* comp);
    void cableAdded(ConnectionCable* cable);

protected:
    void drawBackground(QPainter* painter, const QRectF& rect) override;
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    void updateRoutingPreview(const QPointF& cursorPos);
    static std::vector<QPointF> lBend(const QPointF& from, const QPointF& to, bool hFirst);
    void cancelRouting();

    QVector<ComponentItem*> m_components;
    QVector<ConnectionCable*> m_cables;

    void applySmartConnection(ComponentItem* newComp);

    // ── KiCad-style interactive routing state ──────────────────────────────
    bool m_routing = false;
    ComponentItem* m_routeStartComp = nullptr;
    Pin*           m_routeStartPin  = nullptr;
    bool           m_routeStartIsJunction = false;
    QPointF        m_routeStartJunctionPos;

    std::vector<QPointF> m_routeWaypoints;

    bool m_routeHFirst = true;
    bool m_routeStartHFirst = true;
    QPointF m_lastMouseScenePos;

#include <memory>
    std::unique_ptr<QGraphicsPathItem> m_routePreview;

    QUndoStack* m_undoStack;
    QMap<ComponentItem*, QPointF> m_initialPositions;
    bool m_isSimulating = false;
    bool m_smartConnectionEnabled = false;
    bool m_isApplyingSmartConnection = false;
};
