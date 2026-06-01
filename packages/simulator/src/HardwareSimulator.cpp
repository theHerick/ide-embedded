#include "HardwareSimulator.h"
#include "CustomComponent.h"
#include <QDebug>
#include <QEventLoop>
#include <QRegularExpression>
#include <chrono>

#ifdef _WIN32
#include <windows.h>
#endif


HardwareSimulator::HardwareSimulator(QObject* parent) : QObject(parent) {}

void HardwareSimulator::startSimulation(WorkspaceScene* scene, const QMap<QString, QVector<EventLogicBlock>>& eventBlockStorage, const QJsonObject& webPageData) {
    m_scene = scene;
    m_webPageData = webPageData;
    m_eventStorage = eventBlockStorage;
    m_isRunning = true;
    m_motorSpeeds.clear();
    m_simVariables.clear();
    m_executingLoop.clear();
    
    // Scan all event blocks for EEPROM keys to initialize them as variables
    for (auto it = eventBlockStorage.begin(); it != eventBlockStorage.end(); ++it) {
        for (const auto& b : it.value()) {
            if (b.type == LogicBlockType::EEPROM_OP) {
                QString key = b.actionTarget.trimmed().remove(" ");
                if (!key.isEmpty()) {
                    m_simVariables[key] = 0;
                }
            }
        }
    }
    
    for (auto it = m_eeprom.begin(); it != m_eeprom.end(); ++it) {
        m_simVariables[it.key()] = it.value();
    }
    
    if (!m_simTimer) {
        m_simTimer = new QTimer(this);
        connect(m_simTimer, &QTimer::timeout, this, [this]() {
            if (!m_scene) return;
            
            updateSensorVariables();
            triggerLoopEvents();
            triggerPeriodicEvents();

            int activeConsumers = 0;
            bool isChargerActive = false;
            
            for (auto* comp : m_scene->components()) {
                if (comp->componentType() == "led") {
                    if (m_ledStates.value(comp->id(), false)) activeConsumers++;
                } else if (comp->componentType() == "motor") {
                    // Approximate motor consumption
                    activeConsumers += 2;
                    // Increment motor angle if speed is active in simulator
                    double speed = m_motorSpeeds.value(comp->id(), 0.0);
                    if (std::abs(speed) > 0.01) {
                        auto* motor = static_cast<MotorItem*>(comp);
                        double delta = speed * 0.15;
                        double newAngle = std::fmod(motor->currentAngle() + delta, 360.0);
                        motor->setCurrentAngle(newAngle);
                    }
                } else if (comp->componentType() == "bess_charger") {
                    auto* charger = static_cast<BessChargerItem*>(comp);
                    if (charger->isPluggedIn()) isChargerActive = true;
                }
            }
            
            for (auto* comp : m_scene->components()) {
                if (comp->componentType() == "bess") {
                    auto* bess = static_cast<BessItem*>(comp);
                    double level = bess->chargeLevel();
                    if (isChargerActive) {
                        level += 5.0; // Charge by 5% per tick
                    } else if (activeConsumers > 0) {
                        level -= (activeConsumers * 0.5); // Drain by 0.5% per consumer
                    }
                    bess->setChargeLevel(level);
                }
            }
            
            // Check active buzzers and set active audio frequency
            int maxFreq = 0;
            for (auto* comp : m_scene->components()) {
                if (comp->componentType() == "buzzer") {
                    auto* buzzer = static_cast<BuzzerItem*>(comp);
                    if (buzzer->isActive()) {
                        // Play the exact frequency if it has been set by a tone() block,
                        // otherwise fallback to defaults based on type (active/passive).
                        if (buzzer->frequency() > 0) {
                            maxFreq = qMax(maxFreq, buzzer->frequency());
                        } else if (buzzer->isPassive()) {
                            maxFreq = qMax(maxFreq, 1000); // 1000 Hz default for passive
                        } else {
                            maxFreq = qMax(maxFreq, 2500); // 2500 Hz default for active
                        }
                    }
                }
            }
            m_activeBuzzerFreq.store(maxFreq);
            
            checkElectricalIntegrity();
        });
    }

    m_simTimer->start(50); // 50ms for more responsive loop simulation
    
    // Start background sound thread (Disabled: silent for now)
    m_activeBuzzerFreq.store(0);
    m_soundThreadRunning = true;
    m_soundThread = std::thread([this]() {
        while (m_soundThreadRunning) {
            int freq = m_activeBuzzerFreq.load();
            if (freq > 0) {
#ifdef _WIN32
                Beep(freq, 60);
#else
                std::this_thread::sleep_for(std::chrono::milliseconds(60));
#endif
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        }
    });
    
    // Trigger ESP32's 'aoIniciar' boot event
    updateSensorVariables();
    for (auto* comp : m_scene->components()) {
        if (comp->componentType() == "esp32") {
            triggerComponentEvent(comp->id(), "aoIniciar");
        } else if (comp->componentType() == "button") {
            auto* button = static_cast<ButtonItem*>(comp);
            connect(button, &ButtonItem::stateChanged, this, [this, button](bool pressed) {
                if (!m_isRunning) return;
                if (pressed) {
                    triggerComponentEvent(button->id(), "aoPressionar");
                } else {
                    triggerComponentEvent(button->id(), "aoSoltar");
                    triggerComponentEvent(button->id(), "aoClicar");
                }
            }, Qt::UniqueConnection);
        } else if (auto* custom = dynamic_cast<CustomComponentItem*>(comp)) {
            if (custom->category() == "digital_trigger") {
                connect(custom, &CustomComponentItem::stateChanged, this, [this, custom](bool pressed) {
                    if (!m_isRunning) return;
                    if (pressed) {
                        triggerComponentEvent(custom->id(), "aoPressionar");
                    } else {
                        triggerComponentEvent(custom->id(), "aoSoltar");
                        triggerComponentEvent(custom->id(), "aoClicar");
                    }
                }, Qt::UniqueConnection);
            }
        }
    }
    
    startWebServer();
}

void HardwareSimulator::resetSimulation() {
    if (!m_isRunning || !m_scene) return;
    
    emit serialMessage("--- ESP32 RESET (Hardware) ---", "INFO");
    
    // Clear volatile states but NOT m_eeprom
    m_ledStates.clear();
    m_buttonStates.clear();
    m_motorSpeeds.clear();
    m_simVariables.clear();
    m_executingLoop.clear();
    
    // Reset all component visual states to their default (off)
    for (auto* comp : m_scene->components()) {
        if (comp->componentType() == "led") {
            auto* led = static_cast<LEDItem*>(comp);
            led->setOn(false);
        } else if (comp->componentType() == "buzzer") {
            auto* buzzer = static_cast<BuzzerItem*>(comp);
            buzzer->setActive(false);
        } else if (comp->componentType() == "motor") {
            auto* motor = static_cast<MotorItem*>(comp);
            motor->setCurrentAngle(0);
        } else if (auto* custom = dynamic_cast<CustomComponentItem*>(comp)) {
            if (custom->category() == "digital_actuator") {
                custom->setOn(false);
            } else if (custom->category() == "active_actuator") {
                custom->setActive(false);
            }
        }
    }
    
    // Scan all event blocks for EEPROM keys to initialize them as variables
    for (auto it = m_eventStorage.begin(); it != m_eventStorage.end(); ++it) {
        for (const auto& b : it.value()) {
            if (b.type == LogicBlockType::EEPROM_OP) {
                QString key = b.actionTarget.trimmed().remove(" ");
                if (!key.isEmpty()) {
                    m_simVariables[key] = 0;
                }
            }
        }
    }
    
    for (auto it = m_eeprom.begin(); it != m_eeprom.end(); ++it) {
        m_simVariables[it.key()] = it.value();
    }
    
    updateSensorVariables();

    // Re-trigger boot event (setup)
    for (auto* comp : m_scene->components()) {
        if (comp->componentType() == "esp32") {
            triggerComponentEvent(comp->id(), "aoIniciar");
        }
    }
}

void HardwareSimulator::updateEventStorage(const QMap<QString, QVector<EventLogicBlock>>& eventBlockStorage) {
    m_eventStorage = eventBlockStorage;
}

void HardwareSimulator::stopSimulation() {
    stopWebServer();
    m_soundThreadRunning = false;
    m_activeBuzzerFreq.store(0);
    if (m_soundThread.joinable()) {
        m_soundThread.join();
    }

    m_isRunning = false;
    m_motorSpeeds.clear();
    if (m_simTimer) {
        m_simTimer->stop();
    }
    
    // Reset all LEDs and Buzzers to off on stop
    if (m_scene) {
        for (auto* comp : m_scene->components()) {
            if (comp->componentType() == "led") {
                auto* led = static_cast<LEDItem*>(comp);
                led->setOn(false);
            } else if (comp->componentType() == "buzzer") {
                auto* buzzer = static_cast<BuzzerItem*>(comp);
                buzzer->setActive(false);
            } else if (comp->componentType() == "motor") {
                auto* motor = static_cast<MotorItem*>(comp);
                motor->setCurrentAngle(0);
            } else if (auto* custom = dynamic_cast<CustomComponentItem*>(comp)) {
                if (custom->category() == "digital_actuator") {
                    custom->setOn(false);
                } else if (custom->category() == "active_actuator") {
                    custom->setActive(false);
                }
            }
        }
    }
    
    emit simulationStopped();
}

void HardwareSimulator::startWebServer() {
    if (m_webServer) return;
    m_webServer = new QTcpServer(this);
    connect(m_webServer, &QTcpServer::newConnection, this, &HardwareSimulator::onNewConnection);
    if (m_webServer->listen(QHostAddress::Any, 8080)) {
        serialMessage("Servidor HTTP Local (Simulador Web) Iniciado na porta 8080", "SYSTEM");
    } else {
        serialMessage("Falha ao iniciar Servidor HTTP na porta 8080", "ERROR");
    }
}

void HardwareSimulator::stopWebServer() {
    if (m_webServer) {
        for (auto* client : m_clients) {
            client->disconnectFromHost();
        }
        m_clients.clear();
        m_webServer->close();
        m_webServer->deleteLater();
        m_webServer = nullptr;
    }
}

void HardwareSimulator::checkElectricalIntegrity() {
    if (!m_scene) return;
    int totalLoadMa = 0;
    bool shortCircuit = false;

    // Evaluate load
    for (auto* comp : m_scene->components()) {
        if (comp->componentType() == "led") {
            auto* led = static_cast<LEDItem*>(comp);
            if (led->isOn()) totalLoadMa += 20; // 20mA per LED
        } else if (comp->componentType() == "motor") {
            auto* motor = static_cast<MotorItem*>(comp);
            if (motor->currentAngle() != 0) totalLoadMa += 250;
        } else if (comp->componentType() == "buzzer") {
            auto* buzzer = static_cast<BuzzerItem*>(comp);
            if (buzzer->isActive()) totalLoadMa += 40;
        } else if (auto* custom = dynamic_cast<CustomComponentItem*>(comp)) {
             if (custom->isOn() || custom->isActive()) {
                 totalLoadMa += custom->definition().currentConsumption > 0 ? custom->definition().currentConsumption : 20;
             }
        }
    }

    // Check for short circuit (VCC to GND)
    for (auto* cable : m_scene->cables()) {
        QString p1 = cable->sourcePinName().toUpper();
        QString p2 = cable->targetPinName().toUpper();
        bool p1Vcc = p1.contains("5V") || p1.contains("3V3") || p1.contains("VCC");
        bool p1Gnd = p1.contains("GND");
        bool p2Vcc = p2.contains("5V") || p2.contains("3V3") || p2.contains("VCC");
        bool p2Gnd = p2.contains("GND");
        
        if ((p1Vcc && p2Gnd) || (p1Gnd && p2Vcc)) {
            shortCircuit = true;
            break;
        }
    }

    if (shortCircuit) {
        emit serialMessage("CURTO-CIRCUITO DETECTADO! VCC e GND conectados diretamente.", "ERROR");
        stopSimulation();
        return;
    }

    if (totalLoadMa > 500) { // Assume ESP32 regulator max is 500mA
        emit serialMessage(QString("SOBRECARGA DETECTADA! Consumo atual: %1 mA (Limite: 500 mA).").arg(totalLoadMa), "ERROR");
        stopSimulation();
        return;
    }
}

void HardwareSimulator::triggerLoopEvents() {
    if (!m_isRunning || !m_scene) return;
    for (auto* comp : m_scene->components()) {
        if (!m_executingLoop.value(comp->id(), false)) {
            m_executingLoop[comp->id()] = true;
            triggerComponentEvent(comp->id(), "aoLoop");
            m_executingLoop[comp->id()] = false;
        }
    }
}

void HardwareSimulator::triggerPeriodicEvents() {
    if (!m_isRunning || !m_scene) return;
    
    static qint64 lastMedir = 0;
    static qint64 lastDht = 0;
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    
    if (now - lastMedir >= 100) {
        lastMedir = now;
        for (auto* comp : m_scene->components()) {
            if (comp->componentType() == "hcsr04") {
                QString eventKey = comp->id() + ":aoMedir";
                if (!m_executingLoop.value(eventKey, false)) {
                    m_executingLoop[eventKey] = true;
                    triggerComponentEvent(comp->id(), "aoMedir");
                    m_executingLoop[eventKey] = false;
                }
            }
        }
    }
    
    if (now - lastDht >= 2000) {
        lastDht = now;
        for (auto* comp : m_scene->components()) {
            if (comp->componentType() == "dht22") {
                QString eventKey = comp->id() + ":aoCalcularUmidade";
                if (!m_executingLoop.value(eventKey, false)) {
                    m_executingLoop[eventKey] = true;
                    triggerComponentEvent(comp->id(), "aoCalcularUmidade");
                    m_executingLoop[eventKey] = false;
                }
                
                QString eventKeyTemp = comp->id() + ":aoCalcularTemperatura";
                if (!m_executingLoop.value(eventKeyTemp, false)) {
                    m_executingLoop[eventKeyTemp] = true;
                    triggerComponentEvent(comp->id(), "aoCalcularTemperatura");
                    m_executingLoop[eventKeyTemp] = false;
                }
            }
        }
    }
    
    // Potenciômetros e Custom Sensors Analógicos/Digitais (100ms)
    for (auto* comp : m_scene->components()) {
        if (comp->componentType() == "potentiometer") {
            QString eventKey = comp->id() + ":aoGirar";
            if (!m_executingLoop.value(eventKey, false)) {
                m_executingLoop[eventKey] = true;
                triggerComponentEvent(comp->id(), "aoGirar");
                m_executingLoop[eventKey] = false;
            }
        } else if (comp->componentType() == "bess") {
            QString eventKey = comp->id() + ":aoGirar";
            if (!m_executingLoop.value(eventKey, false)) {
                m_executingLoop[eventKey] = true;
                triggerComponentEvent(comp->id(), "aoGirar");
                m_executingLoop[eventKey] = false;
            }
        } else if (auto* custom = dynamic_cast<CustomComponentItem*>(comp)) {
            if (custom->category() == "analog_input" || custom->category() == "digital_sensor") {
                QString eventKey = custom->id() + ":aoMudarValor";
                if (!m_executingLoop.value(eventKey, false)) {
                    m_executingLoop[eventKey] = true;
                    triggerComponentEvent(custom->id(), "aoMudarValor");
                    m_executingLoop[eventKey] = false;
                }
            }
        }
    }
}

static QString sanitizeIdentifier(const QString& name) {
    QString res = name.normalized(QString::NormalizationForm_D).toUpper();
    QString clean;
    for (int i = 0; i < res.length(); ++i) {
        QChar c = res.at(i);
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_') clean.append(c);
        else if (c.isSpace() || c == '.' || c == '-') clean.append('_');
    }
    if (clean.isEmpty()) clean = "COMP";
    if (!clean.isEmpty() && clean.at(0).isDigit()) clean.prepend('_');
    return clean;
}

void HardwareSimulator::updateSensorVariables() {
    if (!m_scene) return;
    
    auto getSuffix = [](const QString& name) -> QString {
        QString sName = sanitizeIdentifier(name);
        int lastUnderscore = sName.lastIndexOf('_');
        if (lastUnderscore != -1) {
            bool ok = false;
            int num = sName.mid(lastUnderscore + 1).toInt(&ok);
            if (ok) return QString("_%1").arg(num);
        }
        return "";
    };

    for (auto* comp : m_scene->components()) {
        if (comp->componentType() == "hcsr04") {
            auto* hcsr = static_cast<HCSR04Item*>(comp);
            m_simVariables["distancia" + getSuffix(hcsr->name())] = hcsr->distance();
        } else if (comp->componentType() == "dht22") {
            auto* dht = static_cast<DHT22Item*>(comp);
            QString suf = getSuffix(dht->name());
            m_simVariables["umidade" + suf] = dht->humidity();
            m_simVariables["temperatura" + suf] = dht->temperature();
        }
    }
}

void HardwareSimulator::triggerComponentEvent(const QString& compId, const QString& eventName) {
    if (!m_isRunning || !m_scene) return;


    QString key = QString("%1:%2").arg(compId).arg(eventName);
    if (m_eventStorage.contains(key)) {
        executeBlockChain(m_eventStorage[key]);
    }
}

ComponentItem* HardwareSimulator::findComponent(const QString& target) {
    if (!m_scene) return nullptr;
    for (auto* comp : m_scene->components()) {
        QString sName = sanitizeIdentifier(comp->name());
        if (comp->id() == target || comp->name() == target || sName == target || (QString("PIN_") + sName) == target) {
            return comp;
        }
    }
    return nullptr;
}

ComponentItem* HardwareSimulator::findComponentByEspPin(int pinNum) {
    if (!m_scene) return nullptr;
    
    auto extractPinNumber = [](const QString& name) -> QString {
        QRegularExpression re("GPIO(\\d+)");
        auto match = re.match(name);
        if (match.hasMatch()) return match.captured(1);
        if (name.startsWith("D")) {
            bool ok;
            int n = name.mid(1).toInt(&ok);
            if (ok) return QString::number(n);
        }
        return "";
    };

    auto isPassiveComponent = [](const QString& type) -> bool {
        return type == "resistor" || type == "capacitor" || type == "inductor" || type == "diode";
    };

    // Trace through passive components to find the actual component
    for (auto* comp : m_scene->components()) {
        if (comp->componentType() == "esp32") continue;
        if (isPassiveComponent(comp->componentType())) continue;

        for (const auto& pin : comp->pins()) {
            if (pin.connectedToComponent.isEmpty()) continue;

            // Direct connection
            if (pin.connectedToComponent.startsWith("esp32")) {
                QString num = extractPinNumber(pin.connectedToPin);
                if (num == QString::number(pinNum)) return comp;
                continue;
            }

            // Connection via passive (1 level)
            ComponentItem* next = nullptr;
            for (auto* c : m_scene->components()) {
                if (c->id() == pin.connectedToComponent) { next = c; break; }
            }
            if (!next || !isPassiveComponent(next->componentType())) continue;

            for (const auto& nextPin : next->pins()) {
                if (nextPin.connectedToComponent.isEmpty()) continue;
                if (nextPin.connectedToComponent == comp->id()) continue;
                if (nextPin.connectedToComponent.startsWith("esp32")) {
                    QString num = extractPinNumber(nextPin.connectedToPin);
                    if (num == QString::number(pinNum)) return comp;
                }
            }
        }
    }
    return nullptr;
}

#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

void HardwareSimulator::onNewConnection() {
    while (m_webServer->hasPendingConnections()) {
        QTcpSocket* client = m_webServer->nextPendingConnection();
        connect(client, &QTcpSocket::readyRead, this, &HardwareSimulator::onReadyRead);
        connect(client, &QTcpSocket::disconnected, this, &HardwareSimulator::onClientDisconnected);
        m_clients.append(client);
    }
}

void HardwareSimulator::onClientDisconnected() {
    QTcpSocket* client = qobject_cast<QTcpSocket*>(sender());
    if (client) {
        m_clients.removeOne(client);
        client->deleteLater();
    }
}

void HardwareSimulator::onReadyRead() {
    QTcpSocket* client = qobject_cast<QTcpSocket*>(sender());
    if (!client) return;

    if (!client->canReadLine()) return;
    
    QByteArray requestLine = client->readLine();
    QString req = QString::fromUtf8(requestLine).trimmed();
    
    // Read all remaining headers (we don't care about them, just flush them)
    while (client->canReadLine()) {
        QByteArray line = client->readLine();
        if (line == "\r\n" || line == "\n") break;
    }

    if (req.startsWith("GET /data")) {
        // Build JSON response
        QJsonObject json;
        if (m_scene) {
            QJsonObject webData = m_webPageData;
            QJsonArray elements = webData["elements"].toArray();
            for (int i = 0; i < elements.size(); ++i) {
                QJsonObject el = elements[i].toObject();
                QString type = el["type"].toString();
                QString id = el["id"].toString();
                if (type == "Text") {
                    QString boundVar = el["boundVar"].toString();
                    QString varName = boundVar.isEmpty() ? id : boundVar;
                    QString val = m_simVariables.value(varName).toString();
                    if (val.isEmpty() && boundVar.isEmpty()) val = el["text"].toString();
                    json[id] = val;
                    
                    // Add format dynamically from sim variables if they exist
                    json[id + "_color"] = m_simVariables.value("_webFormat_" + id + "_color", el["formatColor"].toString()).toString();
                    json[id + "_size"] = m_simVariables.value("_webFormat_" + id + "_size", QString::number(el.contains("formatSize") ? el["formatSize"].toInt() : 16) + "px").toString();
                    json[id + "_weight"] = m_simVariables.value("_webFormat_" + id + "_weight", el.contains("formatBold") && !el["formatBold"].toBool() ? "normal" : "bold").toString();
                } else if (type == "Slider") {
                    QString boundVar = el["boundVar"].toString();
                    QString varName = boundVar.isEmpty() ? id : boundVar;
                    QString val = m_simVariables.value(varName, "0").toString();
                    json[id] = val;
                } else if (type == "LED") {
                    QString boundVar = el["boundVar"].toString();
                    QString varName = boundVar.isEmpty() ? id : boundVar;
                    QString val = m_simVariables.value(varName, "0").toString();
                    json[id] = val;
                    json[id + "_color"] = el.contains("formatColor") ? el["formatColor"].toString() : "#ef4444";
                }
            }
        }
        
        QByteArray body = QJsonDocument(json).toJson(QJsonDocument::Compact);
        QString response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\n\r\n" + body;
        client->write(response.toUtf8());
        client->disconnectFromHost();
    } else if (req.startsWith("POST /event")) {
        // Parse URL params
        QString url = req.split(" ").value(1);
        QUrl qurl(url);
        QUrlQuery query(qurl);
        
        if (query.hasQueryItem("btn")) {
            QString btn = query.queryItemValue("btn");
            triggerComponentEvent(btn, "aoClicar");
        }
        if (query.hasQueryItem("var")) {
            QString varName = query.queryItemValue("var");
            QString val = query.queryItemValue("val");
            m_simVariables[varName] = val;
            triggerComponentEvent(varName, "aoAlterar");
            if (val == "0") {
                triggerComponentEvent(varName, "aoZerar");
                triggerComponentEvent(varName, "aoDesligar");
            }
        }
        
        QString response = "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n";
        client->write(response.toUtf8());
        client->disconnectFromHost();
    } else if (req.startsWith("GET / ")) {
        // Generate HTML
        QString html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
        html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
        html += "<style>";
        html += "body { margin: 0; padding: 20px; font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; ";
        html += "background: linear-gradient(135deg, #e0f7fa 0%, #b2ebf2 100%); min-height: 100vh; }";
        html += ".container { position: relative; width: 100%; height: 80vh; background: rgba(255, 255, 255, 0.4); ";
        html += "border-radius: 20px; box-shadow: 0 8px 32px rgba(0,0,0,0.1); backdrop-filter: blur(10px); border: 1px solid rgba(255,255,255,0.8); overflow: hidden; }";
        html += ".elem { position: absolute; }";
        html += "button.elem { background: linear-gradient(180deg, #4fc3f7 0%, #0288d1 100%); color: white; border: none; border-radius: 30px; ";
        html += "box-shadow: 0 4px 15px rgba(2,136,209,0.4), inset 0 2px 5px rgba(255,255,255,0.5); text-shadow: 0 1px 2px rgba(0,0,0,0.2); ";
        html += "cursor: pointer; font-weight: bold; font-size: 14px; transition: all 0.2s ease; }";
        html += "button.elem:active { transform: translateY(2px); box-shadow: 0 2px 5px rgba(2,136,209,0.4); }";
        html += "input.elem { background: rgba(255,255,255,0.7); border: 1px solid #81d4fa; border-radius: 10px; padding: 8px 15px; ";
        html += "box-shadow: inset 0 2px 4px rgba(0,0,0,0.05); outline: none; font-size: 14px; color: #01579b; }";
        html += "input.elem:focus { border-color: #0288d1; background: rgba(255,255,255,0.9); }";
        html += "input[type='range'].elem { -webkit-appearance: none; background: #e0e0e0; height: 8px; border-radius: 4px; padding: 0; outline: none; }";
        html += "input[type='range'].elem::-webkit-slider-thumb { -webkit-appearance: none; width: 18px; height: 18px; border-radius: 50%; background: #0288d1; cursor: pointer; transition: background 0.15s ease-in-out; }";
        html += "input[type='range'].elem::-webkit-slider-thumb:hover { background: #01579b; }";
        html += ".text.elem { font-size: 16px; color: #01579b; font-weight: 600; text-shadow: 0 1px 1px rgba(255,255,255,0.8); }";
        html += "</style></head><body>";
        html += "<div class='container'>";
        
        QJsonObject webData = m_webPageData;
        QJsonArray elements = webData["elements"].toArray();
        for (int i = 0; i < elements.size(); ++i) {
            QJsonObject el = elements[i].toObject();
            QString type = el["type"].toString();
            QString id = el["id"].toString();
            int x = el["x"].toInt();
            int y = el["y"].toInt();
            QString text = el["text"].toString().toHtmlEscaped();
            
            if (type == "Text") {
                int fs = el.contains("formatSize") ? el["formatSize"].toInt() : 16;
                QString fc = el.contains("formatColor") ? el["formatColor"].toString() : "#01579b";
                bool fb = el.contains("formatBold") ? el["formatBold"].toBool() : true;
                QString fw = fb ? "bold" : "normal";
                html += QString("<div class='elem text' style='left:%1px; top:%2px; font-size:%3px; color:%4; font-weight:%5;' id='%6'><span id='val_%6'>%7</span></div>\n")
                    .arg(x).arg(y).arg(fs).arg(fc).arg(fw).arg(id).arg(text);
            } else if (type == "Button") {
                html += QString("<button class='elem' style='left:%1px; top:%2px; width:%3px; height:%4px;' onclick='sendEvent(\"%5\")'>%6</button>\n")
                    .arg(x).arg(y).arg(el["width"].toInt(100)).arg(el["height"].toInt(40)).arg(id).arg(text);
            } else if (type == "Input") {
                html += QString("<input type='text' class='elem' style='left:%1px; top:%2px; width:%3px; height:%4px;' id='%5' value='' onchange='sendVar(\"%5\", this.value)'>\n")
                    .arg(x).arg(y).arg(el["width"].toInt(150)).arg(el["height"].toInt(30)).arg(id);
            } else if (type == "Slider") {
                int sliderVal = m_simVariables.value(id, "0").toInt();
                html += QString("<input type='range' class='elem' style='left:%1px; top:%2px; width:%3px; height:%4px;' id='%5' min='0' max='255' value='%6' oninput='sendVar(\"%5\", this.value)'>\n")
                    .arg(x).arg(y).arg(el["width"].toInt(150)).arg(el["height"].toInt(30)).arg(id).arg(sliderVal);
            } else if (type == "LED") {
                QString val = m_simVariables.value(id, "0").toString();
                QString ledColor = el.contains("formatColor") ? el["formatColor"].toString() : "#ef4444";
                bool isOn = (val != "0" && val.toLower() != "low" && val.toLower() != "false");
                QString color = isOn ? ledColor : "#cbd5e1";
                QString border = isOn ? "rgba(0,0,0,0.2)" : "#94a3b8";
                QString shadow = isOn ? QString("0 0 15px %1").arg(ledColor) : "none";
                html += QString("<div class='elem led' style='left:%1px; top:%2px; width:%3px; height:%4px; border-radius:50%; background:%5; border: 2px solid %6; box-shadow: %7; transition: all 0.3s ease;' id='%8'></div>\n")
                    .arg(x).arg(y).arg(el["width"].toInt(40)).arg(el["height"].toInt(40)).arg(color).arg(border).arg(shadow).arg(id);
            }
        }
        
        html += "</div>";
        html += "<script>\n";
        html += "function sendEvent(btn) { fetch('/event?btn=' + btn, {method: 'POST'}); }\n";
        html += "function sendVar(varName, val) { fetch('/event?var=' + varName + '&val=' + val, {method: 'POST'}); }\n";
        html += "setInterval(() => { fetch('/data').then(r=>r.json()).then(d => { \n";
        for (int i = 0; i < elements.size(); ++i) {
            QJsonObject el = elements[i].toObject();
            QString type = el["type"].toString();
            QString id = el["id"].toString();
            if (type == "Text") {
                html += QString("if(d.%1 !== undefined) document.getElementById('val_%1').innerText = d.%1; \n").arg(id);
                html += QString("if(d.%1_color !== undefined) document.getElementById('%1').style.color = d.%1_color; \n").arg(id);
                html += QString("if(d.%1_size !== undefined) document.getElementById('%1').style.fontSize = d.%1_size; \n").arg(id);
                html += QString("if(d.%1_weight !== undefined) document.getElementById('%1').style.fontWeight = d.%1_weight; \n").arg(id);
            } else if (type == "Slider") {
                html += QString("if(d.%1 !== undefined) { const slider = document.getElementById('%1'); if (document.activeElement !== slider) slider.value = d.%1; }\n").arg(id);
            } else if (type == "LED") {
                html += QString("if(d.%1 !== undefined) {\n").arg(id);
                html += QString("  const led = document.getElementById('%1');\n").arg(id);
                html += QString("  const isOn = (d.%1 !== '0' && d.%1.toLowerCase() !== 'low' && d.%1.toLowerCase() !== 'false');\n").arg(id);
                html += QString("  const ledColor = d.%1_color || '#ef4444';\n").arg(id);
                html += QString("  led.style.background = isOn ? ledColor : '#cbd5e1';\n").arg(id);
                html += QString("  led.style.border = isOn ? '2px solid rgba(0,0,0,0.2)' : '2px solid #94a3b8';\n").arg(id);
                html += QString("  led.style.boxShadow = isOn ? '0 0 15px ' + ledColor : 'none';\n");
                html += QString("}\n");
            }
        }
        html += "}); }, 1000);\n";
        html += "</script></body></html>";

        QString response = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n" + html;
        client->write(response.toUtf8());
        client->disconnectFromHost();
    } else {
        client->write("HTTP/1.1 404 Not Found\r\nConnection: close\r\n\r\n");
        client->disconnectFromHost();
    }
}

bool HardwareSimulator::evaluateExpression(const QString& expr) {
    QString lowExpr = expr.toLower().trimmed();
    if (lowExpr.isEmpty() || lowExpr == "true" || lowExpr == "1") return true;
    if (lowExpr == "false" || lowExpr == "0") return false;

    // Normalizar HIGH/LOW/true/false para números
    QString normExpr = expr.trimmed();
    normExpr.replace("HIGH", "1", Qt::CaseInsensitive);
    normExpr.replace("LOW", "0", Qt::CaseInsensitive);
    normExpr.replace("true", "1", Qt::CaseInsensitive);
    normExpr.replace("false", "0", Qt::CaseInsensitive);

    // Verificar se a expressão é simplesmente o nome de uma variável ou comparação de variável
    for (auto it = m_simVariables.begin(); it != m_simVariables.end(); ++it) {
        if (normExpr.compare(it.key(), Qt::CaseInsensitive) == 0) {
            QVariant val = it.value();
            if (val.typeId() == QMetaType::QString) {
                QString s = val.toString();
                return (!s.isEmpty() && s != "0" && s.toUpper() != "LOW" && s.toUpper() != "FALSE");
            }
            return (val.toDouble() != 0);
        }
        
        QRegularExpression compStrRegex(QString("^\\s*%1\\s*(==|!=)\\s*[\"']([^\"']*)[\"']\\s*$").arg(QRegularExpression::escape(it.key())), QRegularExpression::CaseInsensitiveOption);
        QRegularExpressionMatch matchStr = compStrRegex.match(normExpr);
        if (matchStr.hasMatch()) {
            QString op = matchStr.captured(1);
            QString rightVal = matchStr.captured(2);
            QString varVal = it.value().toString();
            if (op == "==") return (varVal.compare(rightVal, Qt::CaseInsensitive) == 0);
            if (op == "!=") return (varVal.compare(rightVal, Qt::CaseInsensitive) != 0);
        }
        
        QRegularExpression compRegex(QString("^\\s*%1\\s*(==|!=|<=|>=|<|>)\\s*(-?\\d+(\\.\\d+)?)\\s*$").arg(QRegularExpression::escape(it.key())), QRegularExpression::CaseInsensitiveOption);
        QRegularExpressionMatch match = compRegex.match(normExpr);
        if (match.hasMatch()) {
            QString op = match.captured(1);
            double rightVal = match.captured(2).toDouble();
            double varVal = it.value().toDouble();
            if (op == "==") return (varVal == rightVal);
            if (op == "!=") return (varVal != rightVal);
            if (op == "<") return (varVal < rightVal);
            if (op == ">") return (varVal > rightVal);
            if (op == "<=") return (varVal <= rightVal);
            if (op == ">=") return (varVal >= rightVal);
        }
    }

    // Try basic fallback
    if (evaluateCondition(expr, "")) return true;

    // Sort components by name length descending to avoid partial matches (e.g. LED_1 vs LED_10)
    QList<ComponentItem*> sortedComps = m_scene->components();
    std::sort(sortedComps.begin(), sortedComps.end(), [](ComponentItem* a, ComponentItem* b) {
        return a->name().length() > b->name().length();
    });

    for (auto* comp : sortedComps) {
        QString sName = sanitizeIdentifier(comp->name()).toLower();
        QString id = comp->id().toLower();
        QString name = comp->name().toLower();
        QString pinName = "pin_" + sName;

        if (lowExpr.contains(id) || lowExpr.contains(name) || lowExpr.contains(sName) || lowExpr.contains(pinName)) {
            auto* customCast = dynamic_cast<CustomComponentItem*>(comp);
            if (comp->componentType() == "potentiometer" || (customCast && customCast->category() == "analog_input")) {
                QString op = ">";
                double val = 500.0;
                if (lowExpr.contains("<")) op = "<";
                else if (lowExpr.contains("==")) op = "==";

                QRegularExpression numRegex("\\d+");
                QRegularExpressionMatch m = numRegex.match(lowExpr);
                if (m.hasMatch()) val = m.captured(0).toDouble();

                return evaluatePotCondition(comp->id(), QString("%1 %2").arg(op).arg(val));
            } else {
                QString state = "HIGH";
                // Check if the expression implies we are looking for a LOW state
                if (lowExpr.contains("low") || lowExpr.contains("0") || lowExpr.contains("false") || 
                    lowExpr.contains("desligado") || lowExpr.contains("== low") || lowExpr.contains("== 0")) {
                    state = "LOW";
                }
                return evaluateCondition(comp->id(), state);
            }
        }
    }
    return false;
}

double HardwareSimulator::evaluateNumericExpression(const QString& expr) {
    QString s = expr.trimmed();
    if (s.isEmpty()) return 0;
    
    // First, resolve pure variables
    if (!s.contains("+") && !s.contains("-") && !s.contains("*") && !s.contains("/")) {
        if (m_simVariables.contains(s)) return m_simVariables[s].toDouble();
        bool ok = false;
        double v = s.toDouble(&ok);
        if (ok) return v;
        return 0;
    }
    
    // Naive evaluation using reverse precedence
    int plus = s.lastIndexOf('+');
    if (plus > 0) return evaluateNumericExpression(s.mid(0, plus)) + evaluateNumericExpression(s.mid(plus + 1));
    
    int minus = s.lastIndexOf('-');
    // Ensure we don't split on a negative sign for the first number (e.g. "-10")
    if (minus > 0 && s.at(minus - 1) != '*' && s.at(minus - 1) != '/' && s.at(minus - 1) != '+' && s.at(minus - 1) != '-') {
        return evaluateNumericExpression(s.mid(0, minus)) - evaluateNumericExpression(s.mid(minus + 1));
    }
    
    int mult = s.lastIndexOf('*');
    if (mult > 0) return evaluateNumericExpression(s.mid(0, mult)) * evaluateNumericExpression(s.mid(mult + 1));
    
    int div = s.lastIndexOf('/');
    if (div > 0) {
        double denom = evaluateNumericExpression(s.mid(div + 1));
        return (denom != 0) ? evaluateNumericExpression(s.mid(0, div)) / denom : 0;
    }
    
    // Fallback if parsing fails (e.g. leading negative sign that didn't get caught)
    bool ok = false;
    double v = s.toDouble(&ok);
    return ok ? v : 0;
}

void HardwareSimulator::executeBlockChain(const QVector<EventLogicBlock>& blocks) {
    struct LevelState {
        bool active;
        bool lastIfTaken;
        int loopStartPc;
        QString loopVar;
        int loopEnd;
        int loopStep;
        QString loopConditionOp;
    };
    QVector<LevelState> execStack;
    execStack.push_back({true, false, -1, "", 0, 0, ""}); // Root level

    int pc = 0;
    int stepCount = 0;
    const int MAX_STEPS = 5000;
    
    while (pc < blocks.size()) {
        stepCount++;
        if (stepCount > MAX_STEPS) {
            emit serialMessage("Simulador: Loop infinito ou muito longo detectado! Execução abortada por segurança.", "ERROR");
            break;
        }
        
        if (!m_isRunning) return;
        const auto& block = blocks[pc];

        bool parentActive = execStack.last().active;
        bool shouldExecute = parentActive;

        if (block.type == LogicBlockType::CONDITION) {
            bool cond = false;
            int loopStartPc = -1;
            QString loopVar = "";
            int loopEnd = 0;
            int loopStep = 0;
            QString loopConditionOp = "";

            if (parentActive) {
                QString expr = block.conditionExpression.trimmed();
                QString lowExpr = expr.toLower();
                bool isElse = (lowExpr == "senao" || lowExpr == "else" || block.id.startsWith("else_") || block.id == "else");
                bool isElseIf = (lowExpr.startsWith("senao se") || lowExpr.startsWith("else if") || block.id.startsWith("elseif") || block.id.startsWith("else if"));
                bool isFor = lowExpr.startsWith("int ") && expr.contains(";") && expr.count(";") == 2;
                bool isWhile = lowExpr.startsWith("while") || block.id.startsWith("while_") || block.id == "while";
                bool isQuando = block.id.startsWith("quando");

                if (isElse) {
                    cond = !execStack.last().lastIfTaken;
                    if (cond) {
                        emit serialMessage("Simulador: Executando bloco SENÃO", "DEBUG");
                        execStack.last().lastIfTaken = true; 
                    }
                } else if (isElseIf) {
                    if (execStack.last().lastIfTaken) {
                        cond = false; // Already took a previous branch
                        emit serialMessage(QString("Simulador: Ignorando SENÃO SE pois um bloco anterior já foi executado"), "DEBUG");
                    } else {
                        QString subExpr = expr;
                        if (subExpr.toLower().startsWith("senao se")) subExpr.remove(0, 8);
                        else if (subExpr.toLower().startsWith("else if")) subExpr.remove(0, 7);
                        subExpr = subExpr.trimmed();
                        
                        cond = evaluateExpression(subExpr);
                        if (cond) {
                            emit serialMessage(QString("Simulador: SENÃO SE (%1) verdadeiro").arg(subExpr), "DEBUG");
                            execStack.last().lastIfTaken = true;
                        } else {
                            emit serialMessage(QString("Simulador: SENÃO SE (%1) falso").arg(subExpr), "DEBUG");
                        }
                    }
                } else if (isFor) {
                    // This is a for loop! e.g., int i = 1; i < 4; i++
                    execStack.last().lastIfTaken = false;
                    QStringList parts = expr.split(";");
                    if (parts.size() == 3) {
                        QString initPart = parts[0].trimmed(); // "int i = 1" or "int i=1"
                        QString condPart = parts[1].trimmed(); // "i < 4"
                        QString incPart = parts[2].trimmed();  // "i++"

                        if (initPart.startsWith("int ")) {
                            initPart = initPart.mid(4).trimmed(); // "i = 1"
                        }
                        QStringList initTokens = initPart.split("=");
                        if (initTokens.size() == 2) {
                            loopVar = initTokens[0].trimmed();
                            int startVal = initTokens[1].trimmed().toInt();
                            
                            // Initialize variable if not looping yet
                            // Wait, if we are entering this block for the first time
                            m_simVariables[loopVar] = startVal;
                        }

                        // condPart like "i < 4" or "i <= 4"
                        if (condPart.contains("<=")) { loopConditionOp = "<="; loopEnd = condPart.split("<=").last().trimmed().toInt(); }
                        else if (condPart.contains(">=")) { loopConditionOp = ">="; loopEnd = condPart.split(">=").last().trimmed().toInt(); }
                        else if (condPart.contains("<")) { loopConditionOp = "<"; loopEnd = condPart.split("<").last().trimmed().toInt(); }
                        else if (condPart.contains(">")) { loopConditionOp = ">"; loopEnd = condPart.split(">").last().trimmed().toInt(); }

                        // incPart like "i++" or "i--"
                        if (incPart.contains("++")) loopStep = 1;
                        else if (incPart.contains("--")) loopStep = -1;
                        
                        loopStartPc = pc;
                        
                        // evaluate condition initially
                        int curVal = m_simVariables[loopVar].toInt();
                        if (loopConditionOp == "<") cond = curVal < loopEnd;
                        else if (loopConditionOp == "<=") cond = curVal <= loopEnd;
                        else if (loopConditionOp == ">") cond = curVal > loopEnd;
                        else if (loopConditionOp == ">=") cond = curVal >= loopEnd;
                        else cond = false;
                    }
                } else if (isWhile) {
                    // This is a while loop!
                    execStack.last().lastIfTaken = false;
                    
                    QString subExpr = expr;
                    if (subExpr.toLower().startsWith("while")) {
                        subExpr.remove(0, 5);
                        if (subExpr.trimmed().startsWith("(")) {
                            subExpr = subExpr.trimmed().mid(1);
                            if (subExpr.endsWith(")")) subExpr.chop(1);
                        }
                    }
                    subExpr = subExpr.trimmed();
                    
                    loopStartPc = pc;
                    loopVar = "$WHILE$"; // Indicator that it's a while loop
                    
                    if (subExpr.isEmpty() || subExpr == "true" || subExpr == "1") {
                        cond = true;
                    } else {
                        cond = evaluateExpression(subExpr);
                    }
                } else if (isQuando) {
                    execStack.last().lastIfTaken = false;
                    
                    QString slideId = "webslider_1";
                    int percentage = 100;
                    int colonIdx = expr.indexOf(':');
                    if (colonIdx != -1) {
                        slideId = expr.left(colonIdx).trimmed();
                        percentage = expr.mid(colonIdx + 1).trimmed().toInt();
                    }
                    
                    int sliderVal = m_simVariables.value(slideId, "0").toInt();
                    int targetVal = percentage * 255 / 100;
                    cond = (sliderVal == targetVal);
                    
                    if (cond) {
                        emit serialMessage(QString("Simulador: QUANDO SLIDER (%1 == %2%) verdadeiro").arg(slideId).arg(percentage), "DEBUG");
                        execStack.last().lastIfTaken = true;
                    } else {
                        emit serialMessage(QString("Simulador: QUANDO SLIDER (%1 == %2%) falso (atual: %3)").arg(slideId).arg(percentage).arg(sliderVal), "DEBUG");
                    }
                } else {
                    // This is a new, independent 'IF' block
                    execStack.last().lastIfTaken = false; // Reset for this new chain
                    
                    if (expr.isEmpty() || expr == "true" || expr == "1") {
                        cond = true;
                        execStack.last().lastIfTaken = true;
                        emit serialMessage("Simulador: Bloco SE (sempre verdadeiro)", "DEBUG");
                    } else {
                        cond = evaluateExpression(expr);
                        if (cond) {
                            emit serialMessage(QString("Simulador: SE (%1) verdadeiro").arg(expr), "DEBUG");
                            execStack.last().lastIfTaken = true;
                        } else {
                            emit serialMessage(QString("Simulador: SE (%1) falso").arg(expr), "DEBUG");
                        }
                    }
                }
            } else {
                execStack.last().lastIfTaken = true;
            }
            execStack.push_back({parentActive && cond, false, loopStartPc, loopVar, loopEnd, loopStep, loopConditionOp});
        }
        else if (block.type == LogicBlockType::FIM) {
            if (execStack.size() > 1) {
                LevelState lastState = execStack.last();
                
                // If this FIM closes a FOR loop
                if (lastState.loopStartPc != -1 && lastState.active) {
                    if (lastState.loopVar == "$WHILE$") {
                        // Re-evaluate while condition
                        const auto& loopBlock = blocks[lastState.loopStartPc];
                        QString expr = loopBlock.conditionExpression.trimmed();
                        QString subExpr = expr;
                        if (subExpr.toLower().startsWith("while")) {
                            subExpr.remove(0, 5);
                            if (subExpr.trimmed().startsWith("(")) {
                                subExpr = subExpr.trimmed().mid(1);
                                if (subExpr.endsWith(")")) subExpr.chop(1);
                            }
                        }
                        subExpr = subExpr.trimmed();
                        
                        bool cond = false;
                        if (subExpr.isEmpty() || subExpr == "true" || subExpr == "1") {
                            cond = true;
                        } else {
                            cond = evaluateExpression(subExpr);
                        }
                        
                        if (cond) {
                            pc = lastState.loopStartPc + 1; // Jump back to INSIDE the loop
                            continue;
                        }
                    } else {
                        // Increment the loop variable
                        int curVal = m_simVariables[lastState.loopVar].toInt();
                        curVal += lastState.loopStep;
                        m_simVariables[lastState.loopVar] = curVal;
                        
                        // Evaluate condition again
                        bool cond = false;
                        if (lastState.loopConditionOp == "<") cond = curVal < lastState.loopEnd;
                        else if (lastState.loopConditionOp == "<=") cond = curVal <= lastState.loopEnd;
                        else if (lastState.loopConditionOp == ">") cond = curVal > lastState.loopEnd;
                        else if (lastState.loopConditionOp == ">=") cond = curVal >= lastState.loopEnd;
                        
                        if (cond) {
                            pc = lastState.loopStartPc + 1; // Jump back to INSIDE the loop
                            continue;
                        }
                    }
                }
                
                execStack.pop_back();
            }
        }
        else if (shouldExecute && block.type == LogicBlockType::ASSIGNMENT) {
            QString expr = block.assignExpression.trimmed();
            QVariant val;
            
            bool isString = (expr.startsWith("\"") && expr.endsWith("\"")) || 
                            (expr.startsWith("'") && expr.endsWith("'"));
            if (isString) {
                val = expr.mid(1, expr.length() - 2);
            } else {
                bool isNum = false;
                double literal = expr.toDouble(&isNum);
                if (isNum) {
                    val = literal;
                } else {
                    if (m_simVariables.contains(expr)) {
                        val = m_simVariables[expr];
                    } else if (evaluateExpression(expr)) {
                        val = 1.0;
                    } else {
                        val = evaluateNumericExpression(expr);
                    }
                }
            }
            m_simVariables[block.assignTarget.trimmed()] = val;
        }
        else if (shouldExecute && block.type == LogicBlockType::CREATE_VAR) {
            QString varName = block.createVarName.trimmed().remove(" ");
            if (!varName.isEmpty() && !m_simVariables.contains(varName)) {
                m_simVariables[varName] = 0;
            }
        }
        else if (shouldExecute && block.type == LogicBlockType::MATH) {
            QString operand1 = block.mathOperand1.trimmed();
            QString operand2 = block.mathOperand2.trimmed();
            double op1 = evaluateNumericExpression(operand1);
            double op2 = evaluateNumericExpression(operand2);
            double res = 0;
            QString op = block.mathOperator;
            if (op == "+") res = op1 + op2;
            else if (op == "-") res = op1 - op2;
            else if (op == "*") res = op1 * op2;
            else if (op == "/") res = (op2 != 0) ? op1 / op2 : 0;
            m_simVariables[block.mathTarget.trimmed()] = res;
        }
        else if (shouldExecute && block.type == LogicBlockType::SERIAL_PRINT) {
            QString expr = block.assignExpression.trimmed();
            QString output;
            
            if (!expr.isEmpty()) {
                QStringList parts = expr.split("<<");
                for (QString part : parts) {
                    part = part.trimmed();
                    if ((part.startsWith("\"") && part.endsWith("\"")) || (part.startsWith("'") && part.endsWith("'"))) {
                        output += part.mid(1, part.length() - 2);
                    } else {
                        bool found = false;
                        if (m_simVariables.contains(part)) {
                            output += m_simVariables[part].toString();
                            found = true;
                        } else {
                            for (auto* c : m_scene->components()) {
                                if (c->name() == part || c->id() == part) {
                                    if (c->componentType() == "potentiometer" || c->componentType() == "bess") {
                                        output += QString::number(evaluatePotCondition(c->id(), "> -1") ? 100 : 0); // Very naive
                                        found = true; break;
                                    } else if (auto* custom = dynamic_cast<CustomComponentItem*>(c)) {
                                        if (custom->category() == "analog_input") {
                                            output += QString::number(custom->value());
                                            found = true; break;
                                        }
                                    }
                                }
                            }
                        }
                        if (!found) {
                            double num = evaluateNumericExpression(part);
                            if (num != 0 || part.trimmed() == "0") {
                                output += QString::number(num);
                            } else {
                                output += part; // Just print the variable name if not found
                            }
                        }
                    }
                }
            }
            
            if (block.assignTarget != "SAME") {
                output += "\n";
            }
            emit serialPrint(output);
        }
        else if (shouldExecute && block.type == LogicBlockType::EEPROM_OP) {
            QString keyName = block.actionTarget;
            QString targetVar = block.assignTarget;
            if (keyName.isEmpty()) keyName = "config_default";

            if (block.actionCommand == "SAVE") {
                QVariant valToSave;
                QString targetVarTrimmed = targetVar.trimmed();
                
                bool isString = (targetVarTrimmed.startsWith("\"") && targetVarTrimmed.endsWith("\"")) || 
                                (targetVarTrimmed.startsWith("'") && targetVarTrimmed.endsWith("'"));
                if (isString) {
                    valToSave = targetVarTrimmed.mid(1, targetVarTrimmed.length() - 2);
                } else if (m_simVariables.contains(targetVarTrimmed)) {
                    valToSave = m_simVariables[targetVarTrimmed];
                } else if (evaluateExpression(targetVar)) {
                    valToSave = 1.0;
                } else {
                    double num = evaluateNumericExpression(targetVarTrimmed);
                    if (num != 0 || targetVarTrimmed == "0" || targetVarTrimmed.contains(QRegularExpression("^\\s*0\\s*$"))) {
                        valToSave = num;
                    } else {
                        valToSave = targetVar; // Fallback to raw string for literals like HIGH/LOW
                    }
                }

                m_eeprom[keyName] = valToSave; 
                m_simVariables[keyName] = valToSave;
                QString stateStr = valToSave.toString();
                emit serialMessage(QString("EEPROM: Salvo '%1' na chave '%2'.").arg(stateStr).arg(keyName), "INFO");
            } else {
                QVariant val = m_eeprom.value(keyName, 0.0);
                m_simVariables[targetVar.trimmed()] = val;
                m_simVariables[keyName] = val;
                QString stateStr = val.toString();
                emit serialMessage(QString("EEPROM: Lida chave '%1' (valor: %2) para variável '%3'.").arg(keyName).arg(stateStr).arg(targetVar), "INFO");
            }
        }
        else if (shouldExecute && block.type == LogicBlockType::ACTION) {

            QString targetId = block.actionTarget;
            QString param = block.actionCommand;

            ComponentItem* comp = findComponent(targetId);
            
            // If we didn't find by name, try to find by ESP32 pin number
            if (!comp) {
                bool ok;
                int pinNum = -1;
                if (m_simVariables.contains(targetId)) {
                    pinNum = m_simVariables[targetId].toInt();
                } else {
                    pinNum = targetId.toInt(&ok);
                    if (!ok) pinNum = -1;
                }
                if (pinNum != -1) {
                    comp = findComponentByEspPin(pinNum);
                }
            }

            if (param == "HIGH" || param == "LOW" || param == "TOGGLE") {
                if (comp) {
                    if (comp->componentType() == "led") {
                        auto* led = static_cast<LEDItem*>(comp);
                        bool wasOn = led->isOn();
                        bool nowOn = param == "TOGGLE" ? !wasOn : (param == "HIGH");
                        led->setOn(nowOn);
                        // Oscilloscope: report both Anode and Cathode transitions
                        emit pinStateChanged(comp->id(), "Anode", nowOn);
                        if (!wasOn && nowOn) {
                            QMetaObject::invokeMethod(this, [this, led]() {
                                triggerComponentEvent(led->id(), "aoLigar");
                            }, Qt::QueuedConnection);
                        }
                    } else if (comp->componentType() == "buzzer") {
                        auto* buzzer = static_cast<BuzzerItem*>(comp);
                        bool wasActive = buzzer->isActive();
                        bool nowActive = param == "TOGGLE" ? !wasActive : (param == "HIGH");
                        buzzer->setActive(nowActive);
                        // Oscilloscope: report buzzer pin 1
                        emit pinStateChanged(comp->id(), "1", nowActive);
                        if (!wasActive && nowActive) {
                            QMetaObject::invokeMethod(this, [this, buzzer]() {
                                triggerComponentEvent(buzzer->id(), "aoTocar");
                            }, Qt::QueuedConnection);
                        }
                    } else if (auto* custom = dynamic_cast<CustomComponentItem*>(comp)) {
                        QString category = custom->category();
                        if (category == "digital_actuator") {
                            bool wasOn = custom->isOn();
                            bool nowOn = param == "TOGGLE" ? !wasOn : (param == "HIGH");
                            custom->setOn(nowOn);
                            // Oscilloscope: use first pin name or fallback
                            QString oscPin = custom->pins().isEmpty() ? "OUT" : custom->pins().first().name;
                            emit pinStateChanged(comp->id(), oscPin, nowOn);
                            if (!wasOn && nowOn) {
                                QMetaObject::invokeMethod(this, [this, custom]() {
                                    triggerComponentEvent(custom->id(), "aoLigar");
                                }, Qt::QueuedConnection);
                            }
                        } else if (category == "active_actuator") {
                            bool wasActive = custom->isActive();
                            bool nowActive = param == "TOGGLE" ? !wasActive : (param == "HIGH");
                            custom->setActive(nowActive);
                            QString oscPin = custom->pins().isEmpty() ? "OUT" : custom->pins().first().name;
                            emit pinStateChanged(comp->id(), oscPin, nowActive);
                            if (!wasActive && nowActive) {
                                QMetaObject::invokeMethod(this, [this, custom]() {
                                    triggerComponentEvent(custom->id(), "aoTocar");
                                }, Qt::QueuedConnection);
                            }
                        }
                    }
                }
            } else if (param == "SET_FREQUENCY" || param == "BUZZER_TONE") {

                if (comp && comp->componentType() == "buzzer") {
                    auto* buzzer = static_cast<BuzzerItem*>(comp);
                    int freq = static_cast<int>(evaluateNumericExpression(block.actionParam));
                    if (freq <= 0) freq = 1000;
                    buzzer->setFrequency(freq);
                    
                    bool wasActive = buzzer->isActive();
                    buzzer->setActive(true);
                    emit pinStateChanged(comp->id(), "1", true);
                    if (!wasActive) {
                        QMetaObject::invokeMethod(this, [this, buzzer]() {
                            triggerComponentEvent(buzzer->id(), "aoTocar");
                        }, Qt::QueuedConnection);
                    }
                }
            } else if (param == "BUZZER_NOTONE") {

                if (comp && comp->componentType() == "buzzer") {
                    auto* buzzer = static_cast<BuzzerItem*>(comp);
                    buzzer->setActive(false);
                    buzzer->setFrequency(0);
                    emit pinStateChanged(comp->id(), "1", false);
                }
            } else if (param == "ROTATE_MOTOR") {
                double angle = evaluateNumericExpression(block.actionParam);

                if (comp && comp->componentType() == "motor") {
                    auto* motor = static_cast<MotorItem*>(comp);
                    m_motorSpeeds[comp->id()] = 0.0; // Parar rotação contínua
                    motor->setCurrentAngle(angle);
                }
            } else if (param == "CALC_BATTERY") {

                if (comp && comp->componentType() == "bess") {
                    double level = static_cast<BessItem*>(comp)->chargeLevel();
                    qDebug() << "Bateria calculada no simulador:" << level << "%";
                }
            } else if (param == "MOTOR_SPIN_INFINITE") {
                double speed = evaluateNumericExpression(block.actionParam);

                if (comp && comp->componentType() == "motor") {
                    m_motorSpeeds[comp->id()] = speed; // Iniciar rotação contínua
                }
            } else if (param == "MOTOR_SPIN_TIME") {
                double speed = evaluateNumericExpression(block.actionParam);
                int ms = static_cast<int>(evaluateNumericExpression(block.actionParam2));
                if (block.actionParam3 == "s") ms *= 1000;
                if (comp && comp->componentType() == "motor") {
                    m_motorSpeeds[comp->id()] = speed; // Iniciar rotação contínua por tempo
                }
                QEventLoop loop;
                QTimer::singleShot(ms, &loop, &QEventLoop::quit);
                loop.exec();
                if (comp && comp->componentType() == "motor") {
                    auto* motor = static_cast<MotorItem*>(comp);
                    m_motorSpeeds[comp->id()] = 0.0; // Parar rotação contínua
                    motor->setCurrentAngle(0); // Stop visually
                }
            } else if (param == "DELAY") {
                int ms = static_cast<int>(evaluateNumericExpression(block.actionParam));
                if (ms <= 0 && !block.actionParam.isEmpty()) {
                    ms = static_cast<int>(evaluateNumericExpression(targetId));
                }
                if (ms <= 0) ms = 500; // default
                QEventLoop loop;
                QTimer::singleShot(ms, &loop, &QEventLoop::quit);
                loop.exec();
            } else if (param == "WIFI_AP") {
                emit serialMessage("Simulador: Ponto de Acesso Wi-Fi Iniciado", "INFO");
            } else if (param == "WIFI_CONNECT") {
                emit serialMessage("Simulador: Wi-Fi Conectado com sucesso", "INFO");
            } else if (param == "FORMAT_WEB_COLOR") {
                m_simVariables["_webFormat_" + targetId + "_color"] = block.actionParam;
            } else if (param == "FORMAT_WEB_SIZE") {
                m_simVariables["_webFormat_" + targetId + "_size"] = QString::number((int)evaluateNumericExpression(block.actionParam)) + "px";
            } else if (param == "FORMAT_WEB_BOLD") {
                m_simVariables["_webFormat_" + targetId + "_weight"] = (block.actionParam == "true") ? "bold" : "normal";
            } else if (param == "CALL_FUNCTION") {
                QString funcKey = targetId + ":" + block.actionParam;
                if (m_eventStorage.contains(funcKey)) {
                    executeBlockChain(m_eventStorage[funcKey]);
                }
            } else if (param == "RETURN") {
                return; // Aborta e retorna da stack atual
            }
        }

        pc++;
    }
}

bool HardwareSimulator::evaluateCondition(const QString& compId, const QString& param) {
    if (!m_scene) return false;
    ComponentItem* comp = findComponent(compId);
    if (comp) {
        if (comp->componentType() == "led") {
            bool isOn = static_cast<LEDItem*>(comp)->isOn();
            bool expectedOn = (param == "HIGH");
            return (isOn == expectedOn);
        }
        else if (comp->componentType() == "button") {
            bool isPressed = static_cast<ButtonItem*>(comp)->isPressed();
            bool expectedPressed = (param == "LOW");
            return (isPressed == expectedPressed);
        } else if (auto* custom = dynamic_cast<CustomComponentItem*>(comp)) {
            QString category = custom->category();
            if (category == "digital_actuator" || category == "active_actuator") {
                bool isOn = custom->isOn() || custom->isActive();
                bool expectedOn = (param == "HIGH");
                return (isOn == expectedOn);
            } else if (category == "digital_trigger") {
                bool isPressed = custom->isPressed();
                bool expectedPressed = (param == "LOW");
                return (isPressed == expectedPressed);
            }
        }
    }
    return false;
}

bool HardwareSimulator::evaluatePotCondition(const QString& compId, const QString& param) {
    if (!m_scene) return false;
    ComponentItem* comp = findComponent(compId);
    if (comp) {
        bool isPot = (comp->componentType() == "potentiometer");
        bool isBess = (comp->componentType() == "bess");
        double potVal = 0.0;
        bool matched = false;
        if (isPot) {
            auto* pot = static_cast<PotentiometerItem*>(comp);
            potVal = pot->value();
            matched = true;
        } else if (isBess) {
            auto* bess = static_cast<BessItem*>(comp);
            potVal = bess->chargeLevel();
            matched = true;
        } else {
            if (auto* custom = dynamic_cast<CustomComponentItem*>(comp)) {
                if (custom->category() == "analog_input") {
                    potVal = custom->value();
                    matched = true;
                }
            }
        }
        if (matched) {
            QString op = ">";
            double threshold = 50.0;
            QStringList parts = param.split(' ', Qt::SkipEmptyParts);
            if (parts.size() >= 2) {
                op = parts[0];
                threshold = parts[1].toDouble();
            } else if (parts.size() == 1) {
                bool ok = false;
                double tempVal = parts[0].toDouble(&ok);
                if (ok) threshold = tempVal;
                else op = parts[0];
            }

            if (op == ">") {
                return potVal > threshold;
            } else if (op == "<") {
                return potVal < threshold;
            } else if (op == "==") {
                return std::abs(potVal - threshold) < 0.01;
            }
        }
    }
    return false;
}


