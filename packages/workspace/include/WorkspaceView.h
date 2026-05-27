#pragma once
#include <QGraphicsView>
#include "WorkspaceScene.h"

class WorkspaceView : public QGraphicsView {
    Q_OBJECT
public:
    explicit WorkspaceView(QWidget* parent = nullptr);
    WorkspaceView(WorkspaceScene* scene, QWidget* parent = nullptr);

protected:
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

signals:
    void requestComponentCreation();

private:
    void spawnSearchBox(const QPoint& viewPos, const QString& initialText = "");
};

