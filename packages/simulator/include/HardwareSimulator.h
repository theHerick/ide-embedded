#pragma once
#include <QObject>
#include <QTimer>
#include <QMap>
#include <QVector>
#include <QVariant>
#include "WorkspaceScene.h"
#include "BlockEditor.h"

class HardwareSimulator : public QObject {
    Q_OBJECT
public:
    explicit HardwareSimulator(QObject* parent = nullptr);
    ~HardwareSimulator() = default;

    void startSimulation(WorkspaceScene* scene, const QMap<QString, QVector<EventLogicBlock>>& eventBlockStorage);
    void stopSimulation();
    void resetSimulation();
    bool isRunning() const { return m_isRunning; }
    void triggerComponentEvent(const QString& compId, const QString& eventName);
    void triggerLoopEvents();

signals:
    void activeComponentChanged(const QString& compId, bool isActive);
    void simulationStopped();
    // Emitted whenever a digital output pin changes state during simulation
    void pinStateChanged(const QString& compId, const QString& pinName, bool isHigh);
    
    // Serial Monitor signals
    void serialPrint(const QString& text);
    void serialMessage(const QString& message, const QString& type = "INFO");

private:
    WorkspaceScene* m_scene = nullptr;
    QTimer* m_simTimer = nullptr;
    bool m_isRunning = false;

    // Component States
    QMap<QString, bool> m_ledStates;
    QMap<QString, bool> m_buttonStates;
    QMap<QString, double> m_motorSpeeds;

    // Event Blocks copied for simulation
    QMap<QString, QVector<EventLogicBlock>> m_eventStorage;

    // Persistent simulated storage
    QMap<QString, QVariant> m_eeprom;

    // Simulated variables
    QMap<QString, QVariant> m_simVariables;

    void executeBlockChain(const QVector<EventLogicBlock>& blocks);
    bool evaluateCondition(const QString& compId, const QString& param);
    bool evaluatePotCondition(const QString& compId, const QString& param);
    bool evaluateExpression(const QString& expr);
    ComponentItem* findComponent(const QString& target);
    
    void checkElectricalIntegrity();
};
