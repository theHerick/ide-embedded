#pragma once
#include <QObject>
#include <QTimer>
#include <QMap>
#include <QVector>
#include <QVariant>
#include <QJsonObject>
#include "WorkspaceScene.h"
#include "BlockEditor.h"

#include <thread>
#include <atomic>
#include <QTcpServer>
#include <QTcpSocket>

class HardwareSimulator : public QObject {
    Q_OBJECT
public:
    explicit HardwareSimulator(QObject* parent = nullptr);
    ~HardwareSimulator() override;


    void startSimulation(WorkspaceScene* scene, const QMap<QString, QVector<EventLogicBlock>>& eventBlockStorage, const QJsonObject& webPageData = QJsonObject());
    void stopSimulation();
    void resetSimulation();
    void updateEventStorage(const QMap<QString, QVector<EventLogicBlock>>& eventBlockStorage);
    bool isRunning() const { return m_isRunning; }
    void setMultitaskingEnabled(bool enabled) { m_multitaskingEnabled = enabled; }
    bool isMultitaskingEnabled() const { return m_multitaskingEnabled; }
    void triggerComponentEvent(const QString& compId, const QString& eventName);
    void triggerLoopEvents();
    void triggerPeriodicEvents();

    // EEPROM persistence
    QMap<QString, QVariant> getEepromData() const { return m_eeprom; }
    void setEepromData(const QMap<QString, QVariant>& data) { m_eeprom = data; }
    void clearEeprom() { m_eeprom.clear(); }
    double getComponentSimValue(const QString& nameOrId);

signals:
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
    bool m_multitaskingEnabled = true;
    int m_activeExecutions = 0;

    // Component States
    QMap<QString, bool> m_ledStates;
    QMap<QString, bool> m_buttonStates;
    QMap<QString, double> m_motorSpeeds;
    QMap<QString, bool> m_executingLoop;

    // Event Blocks copied for simulation
    QMap<QString, QVector<EventLogicBlock>> m_eventStorage;

    // Persistent simulated storage
    QMap<QString, QVariant> m_eeprom;

    // WebPage Config
    QJsonObject m_webPageData;

    // Simulated variables
    QMap<QString, QVariant> m_simVariables;

    void executeBlockChain(const QVector<EventLogicBlock>& blocks);
    bool evaluateCondition(const QString& compId, const QString& param);
    bool evaluatePotCondition(const QString& compId, const QString& param);
    bool evaluateExpression(const QString& expr);
    double evaluateNumericExpression(const QString& expr);
    ComponentItem* findComponent(const QString& target);
    ComponentItem* findComponentByEspPin(int pinNum);
    
    void checkElectricalIntegrity();
    void updateSensorVariables();

    std::thread m_soundThread;
    std::atomic<bool> m_soundThreadRunning{false};
    std::atomic<int> m_activeBuzzerFreq{0};

    // Web Server for Simulation
    QTcpServer* m_webServer = nullptr;
    QList<QTcpSocket*> m_clients;
    
    void startWebServer();
    void stopWebServer();
    
private slots:
    void onNewConnection();
    void onReadyRead();
    void onClientDisconnected();

private:
    struct LevelState {
        bool active = false;
        bool lastIfTaken = false;
        int loopStartPc = -1;
        QString loopVar = "";
        int loopEnd = 0;
        int loopStep = 0;
        QString loopConditionOp = "";
    };

    void executeBlockChainInternal(const QVector<EventLogicBlock>& blocks, int pc, QVector<LevelState> execStack);

    qint64 m_lastMedir = 0;
    qint64 m_lastDht = 0;
    QHash<QString, ComponentItem*> m_componentCache;
};
