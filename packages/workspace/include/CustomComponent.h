#pragma once
#include "ComponentItem.h"
#include "BlockEditor.h"
#include <QColor>
#include <QString>
#include <QVector>
#include <QPointF>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QFile>

struct CustomComponentPin {
    QString name;
    QString side; // "Left", "Right", "Top", "Bottom"
    QString color; // Hex color string, e.g. "#EF4444"
    bool isOutput = false;
    bool generateCode = true;

    // Advanced Modeling
    QString role = "signal"; // power, ground, trigger, echo, signal, etc.
    QString direction = "bidir"; // in, out, bidir
    QString signalType = "digital"; // digital, analog, pwm, i2c, spi, uart
    QString electricalType = "generic"; // 3V3, 5V, open_drain
};

struct PinRelation {
    QString sourcePin;
    QString targetPin;
    QString relationType; // ex: "trigger_echo", "i2c_bus", "spi_bus", "serial_rx_tx"
};

struct CustomComponentOutput {
    QString name;
    QString type = "int"; // int, float, bool, string
    QString unit;
    double min = 0.0;
    double max = 100.0;
    QString expression; // Legacy C++
    QVector<EventLogicBlock> blocks; // Visual logic
    bool cacheable = true;
};

struct LoopDeclaration {
    QString type = "float";
    QString name;
    QString initialValue = "0";
    QString updateExpression = ""; // Legacy C++
    QVector<EventLogicBlock> blocks; // Visual logic
};

struct CustomFunction {
    QString name;
    QString code; // Legacy C++
    QVector<EventLogicBlock> blocks; // Visual logic
};


struct CustomEventDef {
    QString name;
    QString callback;
    QString condition; // Legacy C++
    QVector<EventLogicBlock> conditionBlocks; // Visual logic

    // Advanced Event Modeling
    int debounceMs = 0;
    int cooldownMs = 0;
    bool stateful = false;
    QString triggerMode = "rising"; // rising, falling, change, continuous
    QString eventData = ""; // Struct/Data exposed to the event block
    QString executionMode = "async"; // async, blocking
};

struct CustomComponentDef {
    QString type;       // Unique ID, e.g. "sensor_dht11"
    QString name;       // Friendly toolbox name, e.g. "Sensor DHT11"
    int width = 80;
    int height = 80;
    QString color = "#3B82F6"; // Hex body color
    QString shape = "rectangle"; // "rectangle", "circle", "capsule"
    QString category = "digital_actuator"; // "digital_actuator", "digital_trigger", "analog_input", "active_actuator"
    QString labelText = ""; // Text drawn in center
    QVector<CustomComponentPin> pins;

    // Legacy outputs (Kept for compatibility, but overridden by outputs vector)
    double minValue = 0.0;
    double maxValue = 100.0;
    QString valueUnit = "";
    
    // Modern Outputs
    QVector<CustomComponentOutput> outputs;

    QVector<CustomEventDef> customEvents;

    // Pin Relationships
    QVector<PinRelation> pinRelations;

    // Temporality & Cache (Component-Level)
    int updateIntervalMs = 0; // 0 means continuous/immediate
    bool requiresStateCache = false;

    // Electrical
    QString operatingVoltage = "3V3";
    QString logicLevel = "3V3";
    double currentConsumption = 0.0;
    bool pullupRequired = false;
    bool pulldownRequired = false;

    // Semantics
    QString sensorType = "";
    QString actuatorType = "";
    QString communicationType = "";
    QString technology = "";
    QStringList capabilities;

    // Simulation
    double simDefaultValue = 0.0;
    QString simBehavior = "static";
    QString simWaveform = "none";

    // Initialization & Reading
    bool handlesOwnPinSetup = false;
    QString customLoop = ""; // Legacy C++
    QVector<EventLogicBlock> loopBlocks; // Visual logic
    
    QString readType = "polling"; // polling, interrupt
    QString measurementMethod = "adc";
    QString protocol = "none";
    int readCost = 1;
    bool cacheReadValue = false;
    QString updatePolicy = "on_demand";

    // Alerts & Validation
    QStringList warnings;
    QStringList incompatibilities;
    QStringList requiredAdapters;

    // Advanced C++ Code settings
    QString codeIncludes = "";
    QString codeGlobals = "";
    QString codeSetup = ""; // Legacy C++
    QVector<EventLogicBlock> setupBlocks; // Visual logic
    
    QString codeReadExpression = "";

    // Loop Declarations & Custom Functions
    QVector<LoopDeclaration> loopDeclarations;
    QVector<CustomFunction> customFunctions;

    QStringList detectEventSlots() const;

    QJsonObject toJson() const;
    static CustomComponentDef fromJson(const QJsonObject& obj);
};

class CustomComponentItem : public ComponentItem {
    Q_OBJECT
public:
    CustomComponentItem(const QString& id, const CustomComponentDef& def, QGraphicsItem* parent = nullptr);
    ~CustomComponentItem() override = default;

    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

    QString category() const { return m_def.category; }
    CustomComponentDef definition() const { return m_def; }

    // Simulation & State Behaviors
    bool isOn() const { return m_isOn; }
    void setOn(bool on) { m_isOn = on; update(); }

    bool isPressed() const { return m_isPressed; }
    void setPressed(bool pressed) { m_isPressed = pressed; emit stateChanged(pressed); update(); }

    double value() const { return m_value; }
    void setValue(double val) { m_value = val; emit valueChanged(val); update(); }

    bool isActive() const { return m_isActive; }
    void setActive(bool active) { m_isActive = active; update(); }

signals:
    void stateChanged(bool pressed);
    void valueChanged(double val);

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;
    void wheelEvent(QGraphicsSceneWheelEvent* event) override;

private:
    CustomComponentDef m_def;
    bool m_isOn = false;
    bool m_isPressed = false;
    double m_value = 0.0;
    bool m_isActive = false;

    void distributePins();
};

#include <QCoreApplication>

class CustomComponentManager {
public:
    static QString getRegistryPath() {
        QString appDir = QCoreApplication::applicationDirPath();
        QString path = appDir + "/custom_components.json";
        if (QFile::exists(path)) return path;

        path = appDir + "/../custom_components.json";
        if (QFile::exists(path)) return path;

        path = appDir + "/../packages/workspace/src/custom_components.json";
        if (QFile::exists(path)) return path;

        path = appDir + "/packages/workspace/src/custom_components.json";
        if (QFile::exists(path)) return path;

        return "packages/workspace/src/custom_components.json";
    }

    static CustomComponentManager& instance() {
        static CustomComponentManager inst;
        return inst;
    }

    QVector<CustomComponentDef> registeredComponents() const { return m_registry; }

    void clearRegistry() {
        m_registry.clear();
    }

    void registerComponent(const CustomComponentDef& def) {
        bool found = false;
        for (int i = 0; i < m_registry.size(); ++i) {
            if (m_registry[i].type == def.type) {
                m_registry[i] = def;
                found = true;
                break;
            }
        }
        if (!found) {
            m_registry.append(def);
        }
        saveToFile();
    }

    bool loadFromFile() {
        m_registry.clear();
        QString path = getRegistryPath();
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            return false;
        }

        QByteArray data = file.readAll();
        file.close();

        QJsonDocument doc = QJsonDocument::fromJson(data);
        if (doc.isNull() || !doc.isArray()) return false;

        QJsonArray arr = doc.array();
        for (int i = 0; i < arr.size(); ++i) {
            m_registry.append(CustomComponentDef::fromJson(arr[i].toObject()));
        }
        return true;
    }

    bool removeComponent(const QString& typeId) {
        for (int i = 0; i < m_registry.size(); ++i) {
            if (m_registry[i].type == typeId) {
                m_registry.remove(i);
                saveToFile();
                return true;
            }
        }
        return false;
    }

    void saveToFile() {
        QJsonArray arr;
        for (const auto& def : m_registry) {
            arr.append(def.toJson());
        }
        QJsonDocument doc(arr);

        QString path = getRegistryPath();
        QFile file(path);
        if (file.open(QIODevice::WriteOnly)) {
            file.write(doc.toJson());
            file.close();
        }

        QString appDir = QCoreApplication::applicationDirPath();
        QString backupPath = appDir + "/../packages/workspace/src/custom_components.json";
        if (backupPath != path) {
            QFile backup(backupPath);
            if (backup.open(QIODevice::WriteOnly)) {
                backup.write(doc.toJson());
                backup.close();
            }
        }
    }

private:
    CustomComponentManager() {
        loadFromFile();
    }

    QVector<CustomComponentDef> m_registry;
};
