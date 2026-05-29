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

void HardwareSimulator::startSimulation(WorkspaceScene* scene, const QMap<QString, QVector<EventLogicBlock>>& eventBlockStorage) {
    m_scene = scene;
    m_eventStorage = eventBlockStorage;
    m_isRunning = true;
    m_motorSpeeds.clear();
    m_simVariables.clear();
    
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
            
            triggerLoopEvents();

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
                        if (buzzer->isPassive()) {
                            maxFreq = qMax(maxFreq, buzzer->frequency());
                        } else {
                            maxFreq = qMax(maxFreq, 2500); // 2500 Hz default for active buzzer
                        }
                    }
                }
            }
            m_activeBuzzerFreq.store(maxFreq);
            
            checkElectricalIntegrity();
        });
    }

    m_simTimer->start(50); // 50ms for more responsive loop simulation
    
    // Start background sound thread
    m_activeBuzzerFreq.store(0);
    m_soundThreadRunning = true;
    m_soundThread = std::thread([this]() {
        #ifdef _WIN32
        while (m_soundThreadRunning) {
            int freq = m_activeBuzzerFreq.load();
            if (freq > 0) {
                Beep(freq, 50); // Play a 50ms tone
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
        }
        #else
        while (m_soundThreadRunning) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        #endif
    });
    
    // Trigger ESP32's 'aoIniciar' boot event
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
}

void HardwareSimulator::resetSimulation() {
    if (!m_isRunning || !m_scene) return;
    
    emit serialMessage("--- ESP32 RESET (Hardware) ---", "INFO");
    
    // Clear volatile states but NOT m_eeprom
    m_ledStates.clear();
    m_buttonStates.clear();
    m_motorSpeeds.clear();
    m_simVariables.clear();
    
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
        triggerComponentEvent(comp->id(), "aoLoop");
    }
}

void HardwareSimulator::triggerComponentEvent(const QString& compId, const QString& eventName) {
    if (!m_isRunning || !m_scene) return;


    QString key = QString("%1:%2").arg(compId).arg(eventName);
    if (m_eventStorage.contains(key)) {
        executeBlockChain(m_eventStorage[key]);
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

void HardwareSimulator::executeBlockChain(const QVector<EventLogicBlock>& blocks) {
    struct LevelState {
        bool active;
        bool lastIfTaken;
    };
    QVector<LevelState> execStack;
    execStack.push_back({true, false}); // Root level

    for (const auto& block : blocks) {
        if (!m_isRunning) return;

        bool parentActive = execStack.last().active;
        bool shouldExecute = parentActive;

        if (block.type == LogicBlockType::CONDITION) {
            bool cond = false;
            if (parentActive) {
                QString expr = block.conditionExpression.trimmed().toLower();
                bool isElse = (expr == "senao" || expr == "else" || block.id.startsWith("else_") || block.id == "else");
                bool isElseIf = (expr.startsWith("senao se") || expr.startsWith("else if") || block.id.startsWith("elseif") || block.id.startsWith("else if"));

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
                        if (subExpr.startsWith("senao se")) subExpr.remove(0, 8);
                        else if (subExpr.startsWith("else if")) subExpr.remove(0, 7);
                        subExpr = subExpr.trimmed();
                        
                        cond = evaluateExpression(subExpr);
                        if (cond) {
                            emit serialMessage(QString("Simulador: SENÃO SE (%1) verdadeiro").arg(subExpr), "DEBUG");
                            execStack.last().lastIfTaken = true;
                        } else {
                            emit serialMessage(QString("Simulador: SENÃO SE (%1) falso").arg(subExpr), "DEBUG");
                        }
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
            execStack.push_back({parentActive && cond, false});
        }
        else if (block.type == LogicBlockType::FIM) {
            if (execStack.size() > 1) {
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
                        val = 0.0;
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
            double op1 = 0;
            double op2 = 0;
            QString operand1 = block.mathOperand1.trimmed();
            QString operand2 = block.mathOperand2.trimmed();
            if (m_simVariables.contains(operand1)) {
                op1 = m_simVariables[operand1].toDouble();
            } else {
                bool ok;
                double val = operand1.toDouble(&ok);
                if (ok) op1 = val;
            }
            if (m_simVariables.contains(operand2)) {
                op2 = m_simVariables[operand2].toDouble();
            } else {
                bool ok;
                double val = operand2.toDouble(&ok);
                if (ok) op2 = val;
            }
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
                        if (!found) output += part; // Just print the variable name if not found
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
                    bool ok;
                    double literal = targetVarTrimmed.toDouble(&ok);
                    if (ok) valToSave = literal;
                    else valToSave = targetVar; // Fallback to raw string for literals like HIGH/LOW
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

            if (param == "HIGH" || param == "LOW" || param == "TOGGLE") {
                ComponentItem* comp = findComponent(targetId);
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
                ComponentItem* comp = findComponent(targetId);
                if (comp && comp->componentType() == "buzzer") {
                    auto* buzzer = static_cast<BuzzerItem*>(comp);
                    int freq = 1000;
                    QString freqStr = block.actionParam.trimmed();
                    if (m_simVariables.contains(freqStr)) {
                        freq = m_simVariables[freqStr].toInt();
                    } else {
                        bool ok;
                        int val = freqStr.toInt(&ok);
                        if (ok) freq = val;
                    }
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
                ComponentItem* comp = findComponent(targetId);
                if (comp && comp->componentType() == "buzzer") {
                    auto* buzzer = static_cast<BuzzerItem*>(comp);
                    buzzer->setActive(false);
                    emit pinStateChanged(comp->id(), "1", false);
                }
            } else if (param == "ROTATE_MOTOR") {
                double angle = block.actionParam.toDouble();
                ComponentItem* comp = findComponent(targetId);
                if (comp && comp->componentType() == "motor") {
                    auto* motor = static_cast<MotorItem*>(comp);
                    m_motorSpeeds[comp->id()] = 0.0; // Parar rotação contínua
                    motor->setCurrentAngle(angle);
                }
            } else if (param == "CALC_BATTERY") {
                ComponentItem* comp = findComponent(targetId);
                if (comp && comp->componentType() == "bess") {
                    double level = static_cast<BessItem*>(comp)->chargeLevel();
                    qDebug() << "Bateria calculada no simulador:" << level << "%";
                }
            } else if (param == "MOTOR_SPIN_INFINITE") {
                double speed = block.actionParam.toDouble();
                ComponentItem* comp = findComponent(targetId);
                if (comp && comp->componentType() == "motor") {
                    m_motorSpeeds[comp->id()] = speed; // Iniciar rotação contínua
                }
            } else if (param == "MOTOR_SPIN_TIME") {
                double speed = block.actionParam.toDouble();
                int ms = block.actionParam2.toInt();
                if (block.actionParam3 == "s") ms *= 1000;
                ComponentItem* comp = findComponent(targetId);
                if (comp && comp->componentType() == "motor") {
                    m_motorSpeeds[comp->id()] = speed; // Iniciar rotação contínua por tempo
                }
                QEventLoop loop;
                QTimer::singleShot(ms, &loop, &QEventLoop::quit);
                loop.exec();
                comp = findComponent(targetId);
                if (comp && comp->componentType() == "motor") {
                    auto* motor = static_cast<MotorItem*>(comp);
                    m_motorSpeeds[comp->id()] = 0.0; // Parar rotação contínua
                    motor->setCurrentAngle(0); // Stop visually
                }
            }
 else if (param == "DELAY") {
                // Assuming actionTarget has the delay ms since it doesn't fit the UI perfectly yet
                int ms = targetId.toInt();
                if (ms == 0) ms = 500; // default
                QEventLoop loop;
                QTimer::singleShot(ms, &loop, &QEventLoop::quit);
                loop.exec();
            }
        }
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


