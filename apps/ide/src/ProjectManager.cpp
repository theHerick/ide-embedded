#include "ProjectManager.h"
#include "MainWindow.h"
#include "CodeGenerator.h"
#include "PcbExporter.h"
#include "CustomComponent.h"
#include "ComponentCreatorDialog.h"
#include "WebPageEditorDialog.h"
#include "ComponentItem.h"
#include <QMenuBar>
#include <QToolBar>
#include <QToolButton>
#include <QTime>
#include <QTimer>
#include <QClipboard>
#include <QGuiApplication>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QUrl>
#include <QMessageBox>
#include <QInputDialog>
#include <QFileDialog>
#include "UndoCommands.h"
#include <QMenu>
#include <QAction>
#include <QStatusBar>
#include <QHeaderView>
#include <QScrollBar>
#include <QDialog>
#include <QScrollArea>
#ifdef IDE_EMBEDDED_HAS_QT_PDF
#include <QPdfDocument>
#endif
#include <QVBoxLayout>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>

#include <QComboBox>
#include <QCheckBox>
#include <QProcess>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QTableWidget>
#include <QStandardPaths>
#include <QSerialPortInfo>
#include <QSerialPort>
#include <QTableWidgetItem>
#include <QSlider>
#include <QSpinBox>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QFileInfo>
#include <QDesktopServices>
#include <QUrl>
#include <QSettings>
#include <QHash>
#include <QDir>
#include <QRegularExpression>
#include <QImage>
#include <QPixmap>
#include <QScrollArea>
#include <QTextBrowser>
#include <QPainter>
#include <QMouseEvent>
#include <QScopeGuard>


// --- HACK MACROS FOR DECOUPLING ---
#define m_scene m_mainWindow->m_scene
#define m_blockEditor m_mainWindow->m_blockEditor
#define m_webPageData m_mainWindow->m_webPageData
#define m_simulator m_mainWindow->m_simulator
#define m_currentProjectPath m_mainWindow->m_currentProjectPath
#define loadToolboxItems() m_mainWindow->loadToolboxItems()
#define compileCode() m_mainWindow->compileCode()
#define statusBar() m_mainWindow->statusBar()
#define logMessage m_mainWindow->logMessage

ProjectManager::ProjectManager(MainWindow* mainWindow) : m_mainWindow(mainWindow) {}

static QString sanitizeIdentifier(const QString& name) {
    QString res = name.normalized(QString::NormalizationForm_D).toUpper();
    QString clean;
    for (int i = 0; i < res.length(); ++i) {
        QChar c = res.at(i);
        if (c.category() == QChar::Mark_NonSpacing || c.category() == QChar::Mark_SpacingCombining) {
            continue;
        }
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_') {
            clean.append(c);
        } else if (c.isSpace()) {
            clean.append('_');
        } else if (c == '.' || c == '-') {
            clean.append('_'); // e.g. 4.7K -> 4_7K
        }
    }
    if (clean.isEmpty()) {
        clean = "COMP";
    }
    if (!clean.isEmpty() && clean.at(0).isDigit()) {
        clean.prepend('_');
    }
    return clean;
}

static QString logicBlockTypeToString(LogicBlockType type) {
    switch (type) {
        case LogicBlockType::ASSIGNMENT: return "ASSIGNMENT";
        case LogicBlockType::CONDITION: return "CONDITION";
        case LogicBlockType::ACTION: return "ACTION";
        case LogicBlockType::MATH: return "MATH";
        case LogicBlockType::CREATE_VAR: return "CREATE_VAR";
        case LogicBlockType::FIM: return "FIM";
        case LogicBlockType::SERIAL_PRINT: return "SERIAL_PRINT";
        case LogicBlockType::EEPROM_OP: return "EEPROM_OP";
        case LogicBlockType::EVENT_CREATE: return "EVENT_CREATE";
    }
    return "ACTION";
}

static LogicBlockType logicBlockTypeFromString(const QString& type) {
    const QString upper = type.trimmed().toUpper();
    if (upper == "ASSIGNMENT") return LogicBlockType::ASSIGNMENT;
    if (upper == "CONDITION") return LogicBlockType::CONDITION;
    if (upper == "ACTION") return LogicBlockType::ACTION;
    if (upper == "MATH") return LogicBlockType::MATH;
    if (upper == "CREATE_VAR") return LogicBlockType::CREATE_VAR;
    if (upper == "SERIAL_PRINT") return LogicBlockType::SERIAL_PRINT;
    if (upper == "EEPROM_OP") return LogicBlockType::EEPROM_OP;
    if (upper == "EVENT_CREATE") return LogicBlockType::EVENT_CREATE;
    return LogicBlockType::FIM;
}

static QJsonObject serializeEventLogicBlock(const EventLogicBlock& block) {
    QJsonObject obj;
    obj["id"] = block.id;
    obj["type"] = logicBlockTypeToString(block.type);
    obj["assignTarget"] = block.assignTarget;
    obj["assignExpression"] = block.assignExpression;
    obj["conditionExpression"] = block.conditionExpression;
    obj["actionTarget"] = block.actionTarget;
    obj["actionCommand"] = block.actionCommand;
    obj["actionParam"] = block.actionParam;
    obj["actionParam2"] = block.actionParam2;
    obj["actionParam3"] = block.actionParam3;
    obj["mathTarget"] = block.mathTarget;
    obj["mathOperand1"] = block.mathOperand1;
    obj["mathOperator"] = block.mathOperator;
    obj["mathOperand2"] = block.mathOperand2;
    obj["createVarName"] = block.createVarName;
    obj["createVarType"] = static_cast<int>(block.createVarType);
    return obj;
}

static EventLogicBlock deserializeEventLogicBlock(const QJsonObject& obj) {
    EventLogicBlock block;
    block.id = obj["id"].toString();
    block.type = logicBlockTypeFromString(obj["type"].toString());
    block.assignTarget = obj["assignTarget"].toString();
    block.assignExpression = obj["assignExpression"].toString();
    block.conditionExpression = obj["conditionExpression"].toString();
    block.actionTarget = obj["actionTarget"].toString();
    block.actionCommand = obj["actionCommand"].toString();
    block.actionParam = obj["actionParam"].toString();
    block.actionParam2 = obj["actionParam2"].toString();
    block.actionParam3 = obj["actionParam3"].toString();
    block.mathTarget = obj["mathTarget"].toString();
    block.mathOperand1 = obj["mathOperand1"].toString();
    block.mathOperator = obj["mathOperator"].toString();
    block.mathOperand2 = obj["mathOperand2"].toString();
    block.createVarName = obj["createVarName"].toString();
    block.createVarType = static_cast<VarType>(obj["createVarType"].toInt(static_cast<int>(VarType::INT)));
    return block;
}

static QJsonObject serializeComponentItem(ComponentItem* comp) {
    QJsonObject obj;
    obj["id"] = comp->id();
    obj["type"] = comp->componentType();
    obj["name"] = comp->name();
    obj["x"] = comp->pos().x();
    obj["y"] = comp->pos().y();

    QJsonObject state;
    if (auto* led = dynamic_cast<LEDItem*>(comp)) {
        state["on"] = led->isOn();
    } else if (auto* button = dynamic_cast<ButtonItem*>(comp)) {
        state["pressed"] = button->isPressed();
    } else if (auto* resistor = dynamic_cast<ResistorItem*>(comp)) {
        state["resistance"] = resistor->resistance();
    } else if (auto* capacitor = dynamic_cast<CapacitorItem*>(comp)) {
        state["capacitance"] = capacitor->capacitance();
        state["isSMD"] = capacitor->isSMD();
        state["smdSize"] = capacitor->smdSize();
    } else if (auto* pot = dynamic_cast<PotentiometerItem*>(comp)) {
        state["value"] = pot->value();
    } else if (auto* buzzer = dynamic_cast<BuzzerItem*>(comp)) {
        state["active"] = buzzer->isActive();
        state["isPassive"] = buzzer->isPassive();
        state["frequency"] = buzzer->frequency();
    } else if (auto* motor = dynamic_cast<MotorItem*>(comp)) {
        state["motorType"] = motor->motorType();
        state["currentAngle"] = motor->currentAngle();
    } else if (auto* relay = dynamic_cast<RelayItem*>(comp)) {
        state["isOn"] = relay->isOn();
    } else if (auto* dht = dynamic_cast<DHT22Item*>(comp)) {
        state["humidity"] = dht->humidity();
        state["temperature"] = dht->temperature();
    } else if (auto* hc = dynamic_cast<HCSR04Item*>(comp)) {
        state["distance"] = hc->distance();
    } else if (auto* lamp = dynamic_cast<LampItem*>(comp)) {
        state["on"] = lamp->isOn();
    } else if (auto* custom = dynamic_cast<CustomComponentItem*>(comp)) {
        state["category"] = custom->category();
        state["on"] = custom->isOn();
        state["pressed"] = custom->isPressed();
        state["value"] = custom->value();
        state["active"] = custom->isActive();
    }

    if (comp->property("isSMD").toBool()) {
        state["isSMD"] = true;
        state["smdSize"] = comp->property("smdSize").toString();
        state["smdProps"] = comp->property("smdProps").toJsonObject();
    }

    // Persist microcontroller configuration if present
    QVariant mcfg = comp->property("microcontrollerConfig");
    if (mcfg.isValid() && mcfg.canConvert<QString>()) {
        state["microcontrollerConfig"] = mcfg.toString();
    }

    obj["state"] = state;
    return obj;
}

static QJsonObject serializeCableItem(ConnectionCable* cable) {
    QJsonObject obj;
    obj["sourceComponent"] = cable->sourceComponent() ? cable->sourceComponent()->id() : QString();
    obj["sourcePin"] = cable->sourcePinName();
    obj["targetComponent"] = cable->targetComponent() ? cable->targetComponent()->id() : QString();
    obj["targetPin"] = cable->targetPinName();
    // Save manual waypoints
    QJsonArray wpts;
    for (const auto& wp : cable->manualWaypoints()) {
        QJsonObject pt;
        pt["x"] = wp.x();
        pt["y"] = wp.y();
        wpts.append(pt);
    }
    if (!wpts.isEmpty()) obj["waypoints"] = wpts;
    obj["startHFirst"] = cable->startHFirst();
    return obj;
}

static void applyComponentState(ComponentItem* comp, const QJsonObject& state) {
    if (auto* led = dynamic_cast<LEDItem*>(comp)) {
        led->setOn(state["on"].toBool(led->isOn()));
    } else if (auto* button = dynamic_cast<ButtonItem*>(comp)) {
        Q_UNUSED(button);
    } else if (auto* resistor = dynamic_cast<ResistorItem*>(comp)) {
        if (state.contains("resistance")) resistor->setResistance(state["resistance"].toDouble(resistor->resistance()));
    } else if (auto* capacitor = dynamic_cast<CapacitorItem*>(comp)) {
        if (state.contains("capacitance")) capacitor->setCapacitance(state["capacitance"].toDouble(capacitor->capacitance()));
        if (state.contains("isSMD")) capacitor->setSMD(state["isSMD"].toBool(capacitor->isSMD()));
        if (state.contains("smdSize")) capacitor->setSmdSize(state["smdSize"].toString());
    } else if (auto* pot = dynamic_cast<PotentiometerItem*>(comp)) {
        if (state.contains("value")) pot->setValue(state["value"].toDouble(pot->value()));
    } else if (auto* buzzer = dynamic_cast<BuzzerItem*>(comp)) {
        buzzer->setActive(state["active"].toBool(buzzer->isActive()));
        if (state.contains("isPassive")) buzzer->setPassive(state["isPassive"].toBool());
        if (state.contains("frequency")) buzzer->setFrequency(state["frequency"].toInt());
    } else if (auto* motor = dynamic_cast<MotorItem*>(comp)) {
        if (state.contains("motorType")) motor->setMotorType(state["motorType"].toString());
        if (state.contains("currentAngle")) motor->setCurrentAngle(state["currentAngle"].toDouble());
    } else if (auto* relay = dynamic_cast<RelayItem*>(comp)) {
        if (state.contains("isOn")) relay->setOn(state["isOn"].toBool());
    } else if (auto* dht = dynamic_cast<DHT22Item*>(comp)) {
        if (state.contains("humidity")) dht->setHumidity(state["humidity"].toDouble());
        if (state.contains("temperature")) dht->setTemperature(state["temperature"].toDouble());
    } else if (auto* hc = dynamic_cast<HCSR04Item*>(comp)) {
        if (state.contains("distance")) hc->setDistance(state["distance"].toDouble());
    } else if (auto* lamp = dynamic_cast<LampItem*>(comp)) {
        lamp->setOn(state["on"].toBool(lamp->isOn()));
    } else if (auto* custom = dynamic_cast<CustomComponentItem*>(comp)) {
        custom->setOn(state["on"].toBool(custom->isOn()));
        custom->setPressed(state["pressed"].toBool(custom->isPressed()));
        custom->setValue(state["value"].toDouble(custom->value()));
        custom->setActive(state["active"].toBool(custom->isActive()));
    }

    if (state.contains("isSMD") && state["isSMD"].toBool()) {
        comp->setProperty("isSMD", true);
        QString smdSize = state["smdSize"].toString();
        comp->setProperty("smdSize", smdSize);
        if (state.contains("smdProps")) {
            comp->setProperty("smdProps", state["smdProps"].toObject());
        }
        comp->updateLayoutForSMD(smdSize);
    }

    // Restore microcontroller configuration if provided
    if (state.contains("microcontrollerConfig")) {
        QString cfg = state["microcontrollerConfig"].toString();
        comp->setProperty("microcontrollerConfig", cfg);
        QJsonParseError perr;
        QJsonDocument doc = QJsonDocument::fromJson(cfg.toUtf8(), &perr);
        if (perr.error == QJsonParseError::NoError && doc.isObject()) {
            comp->applyMicrocontrollerConfig(doc.object());
        }
    }
}
bool ProjectManager::saveProjectToFile(const QString& filePath) {
    if (filePath.isEmpty()) {
        return false;
    }

    QJsonObject root;
    root["version"] = 1;
    root["projectName"] = QFileInfo(filePath).baseName();

    QJsonArray customComponents;
    for (const auto& def : CustomComponentManager::instance().registeredComponents()) {
        customComponents.append(def.toJson());
    }
    root["customComponents"] = customComponents;

    QJsonArray components;
    for (auto* comp : m_scene->components()) {
        components.append(serializeComponentItem(comp));
    }
    root["components"] = components;

    QJsonArray cables;
    for (auto* cable : m_scene->cables()) {
        cables.append(serializeCableItem(cable));
    }
    root["cables"] = cables;

    QJsonArray blocks;
    const QMap<QString, QVector<EventLogicBlock>> storage = m_blockEditor->getEventBlockStorage();
    for (auto it = storage.cbegin(); it != storage.cend(); ++it) {
        QJsonObject entry;
        entry["key"] = it.key();
        QJsonArray blockArray;
        for (const auto& block : it.value()) {
            blockArray.append(serializeEventLogicBlock(block));
        }
        entry["blocks"] = blockArray;
        blocks.append(entry);
    }
    root["eventBlocks"] = blocks;
    
    root["webPageData"] = m_webPageData;

    // Save EEPROM persistent data
    QJsonObject eepromObj;
    const QMap<QString, QVariant> eepromData = m_simulator->getEepromData();
    for (auto it = eepromData.cbegin(); it != eepromData.cend(); ++it) {
        QJsonObject entry;
        if (it.value().typeId() == QMetaType::QString) {
            entry["type"] = "string";
            entry["value"] = it.value().toString();
        } else {
            entry["type"] = "number";
            entry["value"] = it.value().toDouble();
        }
        eepromObj[it.key()] = entry;
    }
    root["eeprom"] = eepromObj;

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }

    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    file.close();
    return true;
}

bool ProjectManager::loadProjectFromFile(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (doc.isNull() || !doc.isObject()) {
        return false;
    }

    const QJsonObject root = doc.object();

    if (m_blockEditor) {
        bool prevBlockSignals = m_blockEditor->signalsBlocked();
        m_blockEditor->blockSignals(true);
        m_blockEditor->clearAllBlocks();
        m_blockEditor->blockSignals(prevBlockSignals);
    }
    if (m_scene) {
        bool prevSceneSignals = m_scene->signalsBlocked();
        m_scene->blockSignals(true);
        m_scene->clearWorkspace();
        m_scene->blockSignals(prevSceneSignals);
    }

    m_webPageData = root.value("webPageData").toObject();

    CustomComponentManager::instance().clearRegistry();
    const QJsonArray customComponents = root["customComponents"].toArray();
    for (const auto& value : customComponents) {
        CustomComponentManager::instance().registerComponent(CustomComponentDef::fromJson(value.toObject()));
    }

    QHash<QString, ComponentItem*> componentMap;
    const QJsonArray components = root["components"].toArray();
    for (const auto& value : components) {
        const QJsonObject obj = value.toObject();
        const QString type = obj["type"].toString();
        const QString name = obj["name"].toString(type);
        const QString id = obj["id"].toString();
        const QPointF pos(obj["x"].toDouble(), obj["y"].toDouble());
        ComponentItem* comp = m_scene->addComponent(type, name, pos, id);
        if (!comp) {
            continue;
        }
        applyComponentState(comp, obj["state"].toObject());
        componentMap.insert(comp->id(), comp);
    }

    const QJsonArray cables = root["cables"].toArray();
    for (const auto& value : cables) {
        const QJsonObject obj = value.toObject();
        ComponentItem* src = componentMap.value(obj["sourceComponent"].toString(), nullptr);
        ComponentItem* tgt = componentMap.value(obj["targetComponent"].toString(), nullptr);
        if (!src || !tgt) {
            continue;
        }
        // Restore manual waypoints if saved
        std::vector<QPointF> waypoints;
        if (obj.contains("waypoints")) {
            for (const auto& wv : obj["waypoints"].toArray()) {
                QJsonObject wp = wv.toObject();
                waypoints.push_back(QPointF(wp["x"].toDouble(), wp["y"].toDouble()));
            }
        }
        bool startHFirst = obj.value("startHFirst").toBool(true);
        m_scene->connectPins(src, obj["sourcePin"].toString(), tgt, obj["targetPin"].toString(), waypoints, startHFirst);
    }

    const QJsonArray eventBlocks = root["eventBlocks"].toArray();
    for (const auto& value : eventBlocks) {
        const QJsonObject obj = value.toObject();
        const QString key = obj["key"].toString();
        const int sep = key.indexOf(':');
        if (sep <= 0) {
            continue;
        }
        const QString compId = key.left(sep);
        const QString eventName = key.mid(sep + 1);
        QVector<EventLogicBlock> blocks;
        const QJsonArray blockArray = obj["blocks"].toArray();
        for (const auto& blockValue : blockArray) {
            blocks.append(deserializeEventLogicBlock(blockValue.toObject()));
        }
        m_blockEditor->setEventBlocks(compId, eventName, blocks);
    }

    // Load EEPROM persistent data
    if (root.contains("eeprom")) {
        QMap<QString, QVariant> eepromData;
        const QJsonObject eepromObj = root["eeprom"].toObject();
        for (auto it = eepromObj.begin(); it != eepromObj.end(); ++it) {
            QJsonObject entry = it.value().toObject();
            if (entry["type"].toString() == "string") {
                eepromData[it.key()] = entry["value"].toString();
            } else {
                eepromData[it.key()] = entry["value"].toDouble();
            }
        }
        m_simulator->setEepromData(eepromData);
    }

    m_currentProjectPath = filePath;
    loadToolboxItems();
    compileCode();
    statusBar()->showMessage(QString("Projeto carregado: %1").arg(QFileInfo(filePath).fileName()), 3500);
    logMessage(QString("Projeto aberto com sucesso: %1").arg(filePath), "SUCCESS");
    return true;
}
