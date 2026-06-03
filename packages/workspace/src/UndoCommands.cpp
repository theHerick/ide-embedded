#include "UndoCommands.h"
#include "WorkspaceScene.h"

// --- MoveComponentCommand ---
MoveComponentCommand::MoveComponentCommand(ComponentItem* item, const QPointF& oldPos, const QPointF& newPos, QUndoCommand* parent)
    : QUndoCommand(parent), m_item(item), m_oldPos(oldPos), m_newPos(newPos) {
    setText(QString("Mover %1").arg(item->name()));
}

void MoveComponentCommand::undo() {
    m_item->setPos(m_oldPos);
    if (auto* scene = qobject_cast<WorkspaceScene*>(m_item->scene())) {
        scene->updateCablePaths();
    }
}

void MoveComponentCommand::redo() {
    m_item->setPos(m_newPos);
    if (auto* scene = qobject_cast<WorkspaceScene*>(m_item->scene())) {
        scene->updateCablePaths();
    }
}

bool MoveComponentCommand::mergeWith(const QUndoCommand* other) {
    if (other->id() != id()) return false;
    const MoveComponentCommand* next = static_cast<const MoveComponentCommand*>(other);
    if (next->m_item != m_item) return false;
    m_newPos = next->m_newPos;
    return true;
}

// --- AddComponentCommand ---
AddComponentCommand::AddComponentCommand(WorkspaceScene* scene, ComponentItem* item, QUndoCommand* parent)
    : QUndoCommand(parent), m_scene(scene), m_item(item) {
    setText(QString("Adicionar %1").arg(item->name()));
}

AddComponentCommand::~AddComponentCommand() {
    if (m_ownsItem) delete m_item;
}

void AddComponentCommand::undo() {
    m_scene->removeComponent(m_item);
    m_ownsItem = true;
}

void AddComponentCommand::redo() {
    m_scene->addComponent(m_item);
    m_ownsItem = false;
}

// --- RemoveComponentCommand ---
RemoveComponentCommand::RemoveComponentCommand(WorkspaceScene* scene, ComponentItem* item, QUndoCommand* parent)
    : QUndoCommand(parent), m_scene(scene), m_item(item) {
    setText(QString("Remover %1").arg(item->name()));
}

RemoveComponentCommand::~RemoveComponentCommand() {
    if (m_ownsItem) delete m_item;
}

void RemoveComponentCommand::undo() {
    m_scene->addComponent(m_item);
    m_ownsItem = false;
}

void RemoveComponentCommand::redo() {
    if (m_item->componentType() == "esp32") return; // Safety guard
    m_scene->removeComponent(m_item);
    m_ownsItem = true;
}

// --- AddCableCommand ---
AddCableCommand::AddCableCommand(WorkspaceScene* scene, ConnectionCable* cable, QUndoCommand* parent)
    : QUndoCommand(parent), m_scene(scene), m_cable(cable) {
    setText("Adicionar Cabo");
}

AddCableCommand::~AddCableCommand() {
    if (m_ownsCable) delete m_cable;
}

void AddCableCommand::undo() {
    m_scene->removeCable(m_cable);
    m_ownsCable = true;
}

void AddCableCommand::redo() {
    m_scene->addCable(m_cable);
    m_ownsCable = false;
}

// --- RemoveCableCommand ---
RemoveCableCommand::RemoveCableCommand(WorkspaceScene* scene, ConnectionCable* cable, QUndoCommand* parent)
    : QUndoCommand(parent), m_scene(scene), m_cable(cable) {
    setText("Remover Cabo");
}

RemoveCableCommand::~RemoveCableCommand() {
    if (m_ownsCable) delete m_cable;
}

void RemoveCableCommand::undo() {
    m_scene->addCable(m_cable);
    m_ownsCable = false;
}

void RemoveCableCommand::redo() {
    m_scene->removeCable(m_cable);
    m_ownsCable = true;
}

// --- RenameComponentCommand ---
RenameComponentCommand::RenameComponentCommand(ComponentItem* item, const QString& oldName, const QString& newName, QUndoCommand* parent)
    : QUndoCommand(parent), m_item(item), m_oldName(oldName), m_newName(newName) {
    setText(QString("Renomear %1").arg(newName));
}

void RenameComponentCommand::undo() {
    m_item->setName(m_oldName);
}

void RenameComponentCommand::redo() {
    m_item->setName(m_newName);
}
