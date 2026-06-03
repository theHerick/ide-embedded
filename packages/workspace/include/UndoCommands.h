#pragma once
#include <QUndoCommand>
#include <QPointF>
#include "ComponentItem.h"
#include "ConnectionCable.h"

class WorkspaceScene;

class MoveComponentCommand : public QUndoCommand {
public:
    MoveComponentCommand(ComponentItem* item, const QPointF& oldPos, const QPointF& newPos, QUndoCommand* parent = nullptr);
    void undo() override;
    void redo() override;
    bool mergeWith(const QUndoCommand* other) override;
    int id() const override { return 1; }

private:
    ComponentItem* m_item;
    QPointF m_oldPos;
    QPointF m_newPos;
};

class AddComponentCommand : public QUndoCommand {
public:
    AddComponentCommand(WorkspaceScene* scene, ComponentItem* item, QUndoCommand* parent = nullptr);
    ~AddComponentCommand();
    void undo() override;
    void redo() override;

private:
    WorkspaceScene* m_scene;
    ComponentItem* m_item;
    bool m_ownsItem = false;
};

class RemoveComponentCommand : public QUndoCommand {
public:
    RemoveComponentCommand(WorkspaceScene* scene, ComponentItem* item, QUndoCommand* parent = nullptr);
    ~RemoveComponentCommand();
    void undo() override;
    void redo() override;

private:
    WorkspaceScene* m_scene;
    ComponentItem* m_item;
    bool m_ownsItem = false;
};

class AddCableCommand : public QUndoCommand {
public:
    AddCableCommand(WorkspaceScene* scene, ConnectionCable* cable, QUndoCommand* parent = nullptr);
    ~AddCableCommand();
    void undo() override;
    void redo() override;

private:
    WorkspaceScene* m_scene;
    ConnectionCable* m_cable;
    bool m_ownsCable = false;
};

class RemoveCableCommand : public QUndoCommand {
public:
    RemoveCableCommand(WorkspaceScene* scene, ConnectionCable* cable, QUndoCommand* parent = nullptr);
    ~RemoveCableCommand();
    void undo() override;
    void redo() override;

private:
    WorkspaceScene* m_scene;
    ConnectionCable* m_cable;
    bool m_ownsCable = false;
};

class RenameComponentCommand : public QUndoCommand {
public:
    RenameComponentCommand(ComponentItem* item, const QString& oldName, const QString& newName, QUndoCommand* parent = nullptr);
    void undo() override;
    void redo() override;

private:
    ComponentItem* m_item;
    QString m_oldName;
    QString m_newName;
};
