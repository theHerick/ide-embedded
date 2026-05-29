#include "CodeGenerator.h"
#include "CustomComponent.h"
#include <QDebug>
#include <QSet>
#include <QRegularExpression>

static QString compileMathFormula(QString formula) {
    // Replace pi
    formula.replace("pi", "3.14159265358979323846", Qt::CaseInsensitive);
    formula.replace("π", "3.14159265358979323846");
    
    // 1. Compile fraction(A, B) -> ((double)(A)/(B))
    QRegularExpression fracRegex("fraction\\(([^,]+),([^\\)]+)\\)");
    while (true) {
        QRegularExpressionMatch m = fracRegex.match(formula);
        if (!m.hasMatch()) break;
        QString top = m.captured(1);
        QString bottom = m.captured(2);
        QString replacement = QString("((double)(%1)/(%2))").arg(top).arg(bottom);
        formula.replace(m.capturedStart(0), m.capturedLength(0), replacement);
    }
    
    // 2. Compile integral(lower, upper, expr) -> numeric integration lambda
    QRegularExpression intRegex("integral\\(([^,]+),([^,]+),([^\\)]+)\\)");
    while (true) {
        QRegularExpressionMatch m = intRegex.match(formula);
        if (!m.hasMatch()) break;
        QString lower = m.captured(1);
        QString upper = m.captured(2);
        QString expr = m.captured(3);
        QString replacement = QString(
            "([&](){ "
            "double sum = 0; "
            "double step = ((double)(%2) - (%1)) / 1000.0; "
            "for (int i = 0; i < 1000; ++i) { "
            "  double X = (%1) + i * step; "
            "  sum += (%3) * step; "
            "} "
            "return sum; "
            "})()"
        ).arg(lower).arg(upper).arg(expr);
        formula.replace(m.capturedStart(0), m.capturedLength(0), replacement);
    }
    
    // 3. Compile base^exp shorthand -> pow(base, exp)
    QRegularExpression powShorthand("([A-Za-z0-9_]+|\\([^\\)]+\\))\\^([A-Za-z0-9_]+|\\([^\\)]+\\))");
    while (true) {
        QRegularExpressionMatch m = powShorthand.match(formula);
        if (!m.hasMatch()) break;
        QString base = m.captured(1);
        QString exp = m.captured(2);
        QString replacement = QString("pow(%1, %2)").arg(base).arg(exp);
        formula.replace(m.capturedStart(0), m.capturedLength(0), replacement);
    }
    
    return formula;
}

static QSet<QString> g_eepromKeys;
static QMap<QString, QString> g_eepromKeyTypes;

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

static QString getNumericSuffixFromSanitized(const QString& name) {
    int lastUnderscore = name.lastIndexOf('_');
    if (lastUnderscore != -1) {
        bool ok = false;
        int num = name.mid(lastUnderscore + 1).toInt(&ok);
        if (ok) {
            return QString("_%1").arg(num);
        }
    }
    return "";
}

#include <QSet>

static QString formatSerialPart(const QString& part, const QSet<QString>& knownVars) {
    QString trimmed = part.trimmed();
    if (trimmed.isEmpty()) return "\"\"";
    
    // Already quoted
    if ((trimmed.startsWith("\"") && trimmed.endsWith("\"")) || 
        (trimmed.startsWith("'") && trimmed.endsWith("'"))) {
        return trimmed;
    }
    
    // Is a number?
    bool isInt = false;
    trimmed.toInt(&isInt);
    if (isInt) return trimmed;
    
    bool isDouble = false;
    trimmed.toDouble(&isDouble);
    if (isDouble) return trimmed;
    
    // Is a known variable/keyword?
    if (knownVars.contains(trimmed)) {
        return trimmed;
    }
    
    // Check if it has operators: +, -, *, /, =, <, >, !, &, |
    bool hasOperators = false;
    for (QChar c : trimmed) {
        if (c == '+' || c == '-' || c == '*' || c == '/' || c == '=' || 
            c == '<' || c == '>' || c == '!' || c == '&' || c == '|') {
            hasOperators = true;
            break;
        }
    }
    
    // Check if it is a function call or array access (e.g. readSensor() or vals[0])
    bool hasExpressionSymbols = (trimmed.contains('(') && trimmed.contains(')')) || 
                                 (trimmed.contains('[') && trimmed.contains(']')) || 
                                 (trimmed.contains('.'));
                                 
    if (hasOperators || (hasExpressionSymbols && !trimmed.contains(' '))) {
        return trimmed;
    }
    
    // Otherwise, wrap in quotes!
    QString escaped = trimmed;
    escaped.replace("\"", "\\\"");
    return QString("\"%1\"").arg(escaped);
}

#include <QRegularExpression>

static QString replaceCustomComponentPlaceholders(QString text, CustomComponentItem* custom) {
    if (!custom) return text;

    QString varName = sanitizeIdentifier(custom->name());

    // Precompiled common regexes to avoid recompilation cost
    static const QRegularExpression idRegex("\\{ID\\}", QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression pinRegex("\\{PIN_([^\\}]+)\\}", QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression eventRegex("\\bevent(\\w+)\\b");

    // Replace case-insensitively {ID}
    text.replace(idRegex, varName);

    // Replace case-insensitively {PIN_XXX} with PIN_<VARNAME>_<XXX_SANITIZED>
    while (true) {
        QRegularExpressionMatch match = pinRegex.match(text);
        if (!match.hasMatch()) break;
        QString pinName = match.captured(1);
        QString replacement = QString("PIN_%1_%2").arg(varName).arg(sanitizeIdentifier(pinName));
        text.replace(match.capturedStart(0), match.capturedLength(0), replacement);
    }

    CustomComponentDef def = custom->definition();

    // Replace Loop Declarations variables (whole word)
    for (const auto& decl : def.loopDeclarations) {
        if (decl.name.isEmpty()) continue;
        QRegularExpression varRegex(QString("\\b%1\\b").arg(decl.name));
        text.replace(varRegex, QString("%1_%2").arg(varName).arg(decl.name));
    }

    // Replace Custom Functions names (whole word)
    for (const auto& func : def.customFunctions) {
        if (func.name.isEmpty()) continue;
        QRegularExpression funcRegex(QString("\\b%1\\b").arg(func.name));
        text.replace(funcRegex, QString("%1_%2").arg(varName).arg(func.name));
    }

    // Replace event calls (e.g. eventAoObjetoProximo -> <InstanceName>_eventAoObjetoProximo)
    text.replace(eventRegex, QString("%1_event\\1").arg(varName));

    return text;
}

static QString replaceAllComponentNames(QString text, const QHash<ComponentItem*, QString>& sanitized, bool isLhs = false) {
    if (text.isEmpty()) return text;

    // Sort components by name length descending to avoid partial matches
    QList<ComponentItem*> sortedComps = sanitized.keys();
    std::sort(sortedComps.begin(), sortedComps.end(), [](ComponentItem* a, ComponentItem* b) {
        return a->name().length() > b->name().length();
    });

    for (auto* comp : sortedComps) {
        QString originalName = comp->name();
        QString sanitizedName = sanitized[comp];
        QString pinMacroName = QString("PIN_%1").arg(sanitizedName);
        
        // 1. Check for original name (e.g., "led-6")
        QRegularExpression reOriginal("(?<![a-zA-Z0-9_])" + QRegularExpression::escape(originalName) + "(?![a-zA-Z0-9_])");
        
        // 2. Check for sanitized name (e.g., "LED_6")
        QRegularExpression reSanitized("(?<![a-zA-Z0-9_])" + QRegularExpression::escape(sanitizedName) + "(?![a-zA-Z0-9_])");

        // 3. Check for PIN_ macro name (e.g., "PIN_LED_6")
        QRegularExpression rePinMacro("(?<![a-zA-Z0-9_])" + QRegularExpression::escape(pinMacroName) + "(?![a-zA-Z0-9_])");
        
        if (isLhs) {
            text.replace(reOriginal, sanitizedName);
            text.replace(rePinMacro, sanitizedName);
            text.replace(reSanitized, sanitizedName);
        } else {
            bool isIO = (comp->componentType() == "led" || comp->componentType() == "button" || comp->componentType() == "buzzer");
            if (isIO) {
                QString wrap = QString("digitalRead(%1)").arg(pinMacroName);
                text.replace(reOriginal, wrap);
                text.replace(rePinMacro, wrap);
                text.replace(reSanitized, wrap);
            } else {
                text.replace(reOriginal, sanitizedName);
                text.replace(rePinMacro, sanitizedName);
                text.replace(reSanitized, sanitizedName);
            }
        }
    }
    return text;
}

static QString compileBlocks(
    const QVector<EventLogicBlock>& blocks,
    const QVector<ComponentItem*>& components,
    int baseIndentSpaces,
    CustomComponentItem* owner = nullptr,
    const QHash<ComponentItem*, QString>* sanitizedMap = nullptr,
    QMap<QString, int>* eepromOffsets = nullptr,
    int* nextEepromOffset = nullptr
) {
    // Build or use sanitized map
    QHash<ComponentItem*, QString> localSanitized;
    if (sanitizedMap) {
        localSanitized = *sanitizedMap;
    } else {
        for (auto* c : components) localSanitized[c] = sanitizeIdentifier(c->name());
    }

    QString res;
    int nestLevel = 0;

    QSet<QString> knownVars;
    knownVars.insert("true");
    knownVars.insert("false");
    knownVars.insert("HIGH");
    knownVars.insert("LOW");
    knownVars.insert("INPUT");
    knownVars.insert("OUTPUT");
    knownVars.insert("valor");

    for (const auto& b : blocks) {
        if (b.type == LogicBlockType::CREATE_VAR) {
            QString name = b.createVarName.trimmed().remove(" ");
            if (!name.isEmpty()) {
                knownVars.insert(name);
            }
        }
    }

    for (auto* c : components) {
        QString sName = localSanitized[c];
        knownVars.insert(sName);

        QString suffix = getNumericSuffixFromSanitized(sName);
        if (c->componentType() == "dht22" || c->componentType() == "dht") {
            knownVars.insert(QString("umidade%1").arg(suffix));
            knownVars.insert(QString("temperatura%1").arg(suffix));
        } else if (c->componentType() == "hcsr04") {
            knownVars.insert(QString("distancia%1").arg(suffix));
        }
    }

    for (const auto& key : g_eepromKeys) {
        knownVars.insert(key);
    }

    for (const auto& block : blocks) {
        QString indent = QString(baseIndentSpaces + nestLevel * 4, ' ');

        QString condExpr = block.conditionExpression;
        QString assignTgt = block.assignTarget;
        QString assignExpr = block.assignExpression;
        QString mathTgt = block.mathTarget;
        QString mathOp1 = block.mathOperand1;
        QString mathOp2 = block.mathOperand2;
        QString actionTgt = block.actionTarget;
        QString actionCmd = block.actionCommand;

        // 1. First, replace all literal component names with their sanitized versions in all expressions
        condExpr = replaceAllComponentNames(condExpr, localSanitized, false);
        assignTgt = replaceAllComponentNames(assignTgt, localSanitized, true);
        assignExpr = replaceAllComponentNames(assignExpr, localSanitized, false);
        mathTgt = replaceAllComponentNames(mathTgt, localSanitized, true);
        mathOp1 = replaceAllComponentNames(mathOp1, localSanitized, false);
        mathOp2 = replaceAllComponentNames(mathOp2, localSanitized, false);
        actionTgt = replaceAllComponentNames(actionTgt, localSanitized, true);

        // 2. Custom Component specific replacements (placeholders like {ID}, {PIN_X})
        if (owner) {
            condExpr = replaceCustomComponentPlaceholders(condExpr, owner);
            assignTgt = replaceCustomComponentPlaceholders(assignTgt, owner);
            assignExpr = replaceCustomComponentPlaceholders(assignExpr, owner);
            mathTgt = replaceCustomComponentPlaceholders(mathTgt, owner);
            mathOp1 = replaceCustomComponentPlaceholders(mathOp1, owner);
            mathOp2 = replaceCustomComponentPlaceholders(mathOp2, owner);
            actionTgt = replaceCustomComponentPlaceholders(actionTgt, owner);
        }

        for (auto* comp : components) {
            if (comp == owner) continue;
            if (auto* custom = dynamic_cast<CustomComponentItem*>(comp)) {
                condExpr = replaceCustomComponentPlaceholders(condExpr, custom);
                assignTgt = replaceCustomComponentPlaceholders(assignTgt, custom);
                assignExpr = replaceCustomComponentPlaceholders(assignExpr, custom);
                mathTgt = replaceCustomComponentPlaceholders(mathTgt, custom);
                mathOp1 = replaceCustomComponentPlaceholders(mathOp1, custom);
                mathOp2 = replaceCustomComponentPlaceholders(mathOp2, custom);
                actionTgt = replaceCustomComponentPlaceholders(actionTgt, custom);
            }
        }

        if (block.type == LogicBlockType::CONDITION) {
            if (condExpr == "senao" || condExpr.toLower() == "else") {
                res += QString("%1else {\n").arg(indent);
            } else if (block.id.startsWith("while")) {
                res += QString("%1while (%2) {\n").arg(indent).arg(condExpr);
            } else if (block.id.startsWith("elseif") || block.id.startsWith("else if")) {
                res += QString("%1else if (%2) {\n").arg(indent).arg(condExpr);
            } else if (block.id.startsWith("for")) {
                res += QString("%1for (%2) {\n").arg(indent).arg(condExpr);
            } else {
                res += QString("%1if (%2) {\n").arg(indent).arg(condExpr);
            }
            nestLevel++;
        }
        else if (block.type == LogicBlockType::FIM) {
            if (nestLevel > 0) {
                nestLevel--;
            }
            QString fimIndent = QString(baseIndentSpaces + nestLevel * 4, ' ');
            res += QString("%1}\n").arg(fimIndent);
        }
        else if (block.type == LogicBlockType::ASSIGNMENT) {
            // Check if the target is a component. If so, generate digitalWrite
            bool targetIsComp = false;
            QString targetPin;
            for (auto* c : components) {
                QString sName = localSanitized[c];
                if (assignTgt == sName || assignTgt == "PIN_" + sName) {
                    targetIsComp = true;
                    targetPin = "PIN_" + sName;
                    break;
                }
            }

            if (targetIsComp) {
                res += QString("%1digitalWrite(%2, %3);\n").arg(indent).arg(targetPin).arg(assignExpr);
            } else {
                res += QString("%1%2 = %3;\n").arg(indent).arg(assignTgt).arg(assignExpr);
            }
        }
        else if (block.type == LogicBlockType::CREATE_VAR) {
            QString name = block.createVarName.trimmed().remove(" ");
            if (!name.isEmpty()) {
                QString cppType = "int";
                QString initVal = "0";
                if (block.createVarType == VarType::FLOAT) {
                    cppType = "float";
                    initVal = "0.0";
                } else if (block.createVarType == VarType::BOOL) {
                    cppType = "bool";
                    initVal = "false";
                } else if (block.createVarType == VarType::STRING) {
                    cppType = "String";
                    initVal = "\"\"";
                }
                res += QString("%1%2 %3 = %4;\n").arg(indent).arg(cppType).arg(name).arg(initVal);
            }
        }
        else if (block.type == LogicBlockType::MATH) {
            if (block.mathOperator.isEmpty() || block.mathOperator == " ") {
                QString compiledFormula = compileMathFormula(mathOp1);
                res += QString("%1%2 = %3;\n")
                    .arg(indent)
                    .arg(mathTgt)
                    .arg(compiledFormula);
            } else {
                res += QString("%1%2 = %3 %4 %5;\n")
                    .arg(indent)
                    .arg(mathTgt)
                    .arg(mathOp1)
                    .arg(block.mathOperator)
                    .arg(mathOp2);
            }
        }
        else if (block.type == LogicBlockType::EEPROM_OP) {
            QString keyName = block.actionTarget.trimmed();
            keyName = replaceAllComponentNames(keyName, localSanitized, true);
            keyName.remove(" ");
            
            if (keyName.isEmpty()) keyName = "config_default";
            
            bool isComp = false;
            ComponentItem* targetComp = nullptr;
            for (auto* c : components) {
                QString sName = localSanitized[c];
                if (keyName == sName || keyName == "PIN_" + sName) {
                    isComp = true;
                    targetComp = c;
                    break;
                }
            }
            
            int offset = (qHash(keyName) % 120) * 4; // Map to 0-480 range in 4-byte steps
            
            if (block.actionCommand == "SAVE") {
                if (isComp && targetComp) {
                    QString sName = localSanitized[targetComp];
                    bool isAnalog = (targetComp->componentType() == "potentiometer");
                    if (isAnalog) {
                        res += QString("%1EEPROM.put(%2, analogRead(PIN_%3));\n").arg(indent).arg(offset).arg(sName);
                    } else {
                        res += QString("%1EEPROM.put(%2, digitalRead(PIN_%3));\n").arg(indent).arg(offset).arg(sName);
                    }
                } else {
                    QString type = g_eepromKeyTypes.value(keyName, "int");
                    if (type == "String") {
                        res += QString("%1EEPROM.writeString(%2, %3);\n").arg(indent).arg(offset).arg(keyName);
                    } else {
                        res += QString("%1EEPROM.put(%2, %3);\n").arg(indent).arg(offset).arg(keyName);
                    }
                }
                res += QString("%1EEPROM.commit();\n").arg(indent);
            } else {
                if (isComp && targetComp) {
                    QString sName = localSanitized[targetComp];
                    bool isAnalog = (targetComp->componentType() == "potentiometer");
                    res += QString("%1{\n").arg(indent);
                    if (isAnalog) {
                        res += QString("%1    int restoredVal = 0;\n").arg(indent);
                        res += QString("%1    EEPROM.get(%2, restoredVal);\n").arg(indent).arg(offset);
                        res += QString("%1    analogWrite(PIN_%2, restoredVal);\n").arg(indent).arg(sName);
                    } else {
                        res += QString("%1    int restoredVal = 0;\n").arg(indent);
                        res += QString("%1    EEPROM.get(%2, restoredVal);\n").arg(indent).arg(offset);
                        res += QString("%1    digitalWrite(PIN_%2, restoredVal);\n").arg(indent).arg(sName);
                    }
                    res += QString("%1}\n").arg(indent);
                } else {
                    QString type = g_eepromKeyTypes.value(keyName, "int");
                    if (type == "String") {
                        res += QString("%1%2 = EEPROM.readString(%3);\n").arg(indent).arg(keyName).arg(offset);
                    } else {
                        res += QString("%1EEPROM.get(%2, %3);\n").arg(indent).arg(offset).arg(keyName);
                    }
                }
            }
        }
        else if (block.type == LogicBlockType::ACTION) {
            QString normalizedAction = actionCmd.toUpper();
            if (normalizedAction.contains("CALL") || normalizedAction.contains("CHAMAR")) {
                actionCmd = "CALL_FUNCTION";
            } else if (normalizedAction.contains("RETURN") || normalizedAction.contains("RETORN")) {
                actionCmd = "RETURN";
            } else if (normalizedAction.contains("HIGH")) {
                actionCmd = "HIGH";
            } else if (normalizedAction.contains("LOW")) {
                actionCmd = "LOW";
            } else if (normalizedAction.contains("TOGGLE") || normalizedAction.contains("INVER")) {
                actionCmd = "TOGGLE";
            } else if (normalizedAction.contains("DELAY") || normalizedAction.contains("AGUARD")) {
                actionCmd = "DELAY";
            } else if (normalizedAction.contains("ROTATE") || normalizedAction.contains("GIRAR") || normalizedAction.contains("MOTOR_SPIN")) {
                if (normalizedAction.contains("TEMPO") || normalizedAction.contains("TIME")) {
                    actionCmd = "MOTOR_SPIN_TIME";
                } else if (normalizedAction.contains("INFINI") || normalizedAction.contains("SPIN")) {
                    actionCmd = "MOTOR_SPIN_INFINITE";
                } else {
                    actionCmd = "ROTATE_MOTOR";
                }
            }

            if (actionCmd == "RETURN") {
                QString param = block.actionParam.trimmed().isEmpty() ? actionTgt : block.actionParam.trimmed();
                res += QString("%1return %2;\n").arg(indent).arg(param);
                continue;
            }

            if (actionCmd == "CALL_FUNCTION") {
                QString param = block.actionParam.trimmed();
                QString callExpr = actionTgt.trimmed();
                if (!callExpr.isEmpty()) {
                    if (!param.isEmpty()) {
                        callExpr += "(" + param + ")";
                    } else if (!callExpr.endsWith(')')) {
                        callExpr += "()";
                    }
                    res += QString("%1%2;\n").arg(indent).arg(callExpr);
                }
                continue;
            }

            QString tgtName = "UNKNOWN";
            for (auto* c : components) {
                QString sName = sanitizedMap ? sanitizedMap->value(c, sanitizeIdentifier(c->name())) : sanitizeIdentifier(c->name());
                if (c->name() == actionTgt || c->id() == actionTgt || sName == actionTgt || (QString("PIN_") + sName) == actionTgt) {
                    tgtName = sName;
                    break;
                }
            }
            if (tgtName == "UNKNOWN") tgtName = actionTgt; // Fallback to raw string if it's a variable or pin

            if (actionCmd == "HIGH" || actionCmd == "LOW") {
                bool isBuzzer = false;
                for (auto* c : components) {
                    QString sName = sanitizedMap ? sanitizedMap->value(c, sanitizeIdentifier(c->name())) : sanitizeIdentifier(c->name());
                    if (c->componentType() == "buzzer" && (c->name() == actionTgt || c->id() == actionTgt || sName == actionTgt || (QString("PIN_") + sName) == actionTgt)) {
                        isBuzzer = true;
                        break;
                    }
                }
                if (isBuzzer && actionCmd == "LOW") {
                    res += QString("%1noTone(PIN_%2);\n").arg(indent).arg(tgtName);
                } else {
                    res += QString("%1digitalWrite(PIN_%2, %3);\n").arg(indent).arg(tgtName).arg(actionCmd);
                }
            } else if (actionCmd == "SET_FREQUENCY" || actionCmd == "BUZZER_TONE") {
                QString freq = block.actionParam.trimmed().isEmpty() ? "1000" : block.actionParam.trimmed();
                res += QString("%1tone(PIN_%2, %3);\n").arg(indent).arg(tgtName).arg(freq);
            } else if (actionCmd == "BUZZER_NOTONE") {
                res += QString("%1noTone(PIN_%2);\n").arg(indent).arg(tgtName);
            } else if (actionCmd == "TOGGLE") {
                res += QString("%1digitalWrite(PIN_%2, !digitalRead(PIN_%2));\n").arg(indent).arg(tgtName);
            } else if (actionCmd == "DELAY") {
                QString param = block.actionParam.trimmed().isEmpty() ? tgtName : block.actionParam.trimmed();
                res += QString("%1delay(%2);\n").arg(indent).arg(param);
            } else if (actionCmd == "ROTATE_MOTOR") {
                QString param = block.actionParam.trimmed().isEmpty() ? "0" : block.actionParam.trimmed();
                res += QString("%1// Rotacionando motor (requer objeto %2 instanciado)\n").arg(indent).arg(tgtName);
                res += QString("%1%2.write(%3);\n").arg(indent).arg(tgtName).arg(param);
            } else if (actionCmd == "MOTOR_SPIN_INFINITE") {
                QString speed = block.actionParam.trimmed().isEmpty() ? "0" : block.actionParam.trimmed();
                res += QString("%1// Girando motor continuamente\n").arg(indent);
                res += QString("%1%2.write(%3);\n").arg(indent).arg(tgtName).arg(speed);
            } else if (actionCmd == "MOTOR_SPIN_TIME") {
                QString speed = block.actionParam.trimmed().isEmpty() ? "0" : block.actionParam.trimmed();
                QString duration = block.actionParam2.trimmed().isEmpty() ? "1000" : block.actionParam2.trimmed();
                QString unit = block.actionParam3.trimmed().isEmpty() ? "ms" : block.actionParam3.trimmed();
                res += QString("%1// Girar motor por tempo determinado\n").arg(indent);
                res += QString("%1%2.write(%3);\n").arg(indent).arg(tgtName).arg(speed);
                if (unit == "s") {
                    res += QString("%1delay((%2) * 1000);\n").arg(indent).arg(duration);
                } else {
                    res += QString("%1delay(%2);\n").arg(indent).arg(duration);
                }
                res += QString("%1%2.stop();\n").arg(indent).arg(tgtName);
            } else if (actionCmd == "CALC_BATTERY") {
                QString destVar = block.actionParam.trimmed().isEmpty() ? "nivelBateria" : block.actionParam.trimmed();
                res += QString("%1// Calculando %% da bateria via divisor (2 resistores iguais)\n").arg(indent);
                res += QString("%1int rawADC_%2 = analogRead(PIN_%3);\n").arg(indent).arg(tgtName).arg(tgtName);
                res += QString("%1float vADC_%2 = (rawADC_%3 / 4095.0) * 3.3;\n").arg(indent).arg(tgtName).arg(tgtName);
                res += QString("%1%2 = (vADC_%3 * 2.0 / 4.2) * 100.0;\n").arg(indent).arg(destVar).arg(tgtName);
                res += QString("%1if (%2 > 100.0) %2 = 100.0;\n").arg(indent).arg(destVar);
                res += QString("%1if (%2 < 0.0) %2 = 0.0;\n").arg(indent).arg(destVar);
            } else if (actionCmd == "WIFI_AP") {
                QString ssid = actionTgt.isEmpty() ? "\"ESP32_Network\"" : actionTgt;
                QString pwd = block.actionParam.trimmed();
                
                auto formatArg = [](const QString& arg) {
                    QString s = arg.trimmed();
                    if (s.isEmpty()) return QString("\"\"");
                    if (s.startsWith("\"") && s.endsWith("\"")) return s;
                    if (s.startsWith("'") && s.endsWith("'")) return s;
                    // Let's assume valid identifier = variable, otherwise we quote it
                    QRegularExpression varRegex("^[A-Za-z_][A-Za-z0-9_]*$");
                    if (varRegex.match(s).hasMatch()) return s; 
                    return QString("\"%1\"").arg(s);
                };

                QString fmtSsid = formatArg(ssid);
                QString fmtPwd = formatArg(pwd);

                res += QString("%1// Criar Ponto de Acesso (AP)\n").arg(indent);
                if (fmtPwd == "\"\"" || fmtPwd.isEmpty()) {
                    res += QString("%1WiFi.softAP(%2);\n").arg(indent).arg(fmtSsid);
                } else {
                    res += QString("%1WiFi.softAP(%2, %3);\n").arg(indent).arg(fmtSsid).arg(fmtPwd);
                }
            } else if (actionCmd == "WIFI_CONNECT") {
                QString ssid = actionTgt;
                QString pwd = block.actionParam.trimmed();
                
                auto formatArg = [](const QString& arg) {
                    QString s = arg.trimmed();
                    if (s.isEmpty()) return QString("\"\"");
                    if (s.startsWith("\"") && s.endsWith("\"")) return s;
                    if (s.startsWith("'") && s.endsWith("'")) return s;
                    QRegularExpression varRegex("^[A-Za-z_][A-Za-z0-9_]*$");
                    if (varRegex.match(s).hasMatch()) return s; 
                    return QString("\"%1\"").arg(s);
                };
                
                QString fmtSsid = formatArg(ssid);
                QString fmtPwd = formatArg(pwd);

                res += QString("%1// Conectar a rede WiFi\n").arg(indent);
                res += QString("%1WiFi.begin(%2, %3);\n").arg(indent).arg(fmtSsid).arg(fmtPwd);
            } else if (actionCmd == "FORMAT_WEB_COLOR") {
                res += QString("%1_webFormat_%2_color = String(%3);\n").arg(indent).arg(actionTgt).arg(block.actionParam.isEmpty() ? "\"#000000\"" : block.actionParam);
            } else if (actionCmd == "FORMAT_WEB_SIZE") {
                res += QString("%1_webFormat_%2_size = String(%3) + \"px\";\n").arg(indent).arg(actionTgt).arg(block.actionParam.isEmpty() ? "\"16\"" : block.actionParam);
            } else if (actionCmd == "FORMAT_WEB_BOLD") {
                QString param = block.actionParam.trimmed();
                res += QString("%1if (String(%2) == \"1\" || String(%2).equalsIgnoreCase(\"true\") || String(%2).equalsIgnoreCase(\"high\")) {\n").arg(indent).arg(param.isEmpty() ? "\"0\"" : param);
                res += QString("%1    _webFormat_%2_weight = \"bold\";\n").arg(indent).arg(actionTgt);
                res += QString("%1} else {\n").arg(indent);
                res += QString("%1    _webFormat_%2_weight = \"normal\";\n").arg(indent).arg(actionTgt);
                res += QString("%1}\n").arg(indent);
            }
        }
        else if (block.type == LogicBlockType::SERIAL_PRINT) {
            QString expr = block.assignExpression.trimmed();
            bool newline = (block.assignTarget != "SAME");
            
            if (expr.isEmpty()) {
                if (newline) {
                    res += QString("%1Serial.println();\n").arg(indent);
                } else {
                    res += QString("%1Serial.print(\"\");\n").arg(indent);
                }
            } else {
                QStringList parts = expr.split("<<");
                for (int i = 0; i < parts.size(); ++i) {
                    QString part = parts[i].trimmed();
                    if (part.isEmpty()) continue;
                    
                    bool isLast = (i == parts.size() - 1);
                    bool printLn = newline && isLast;
                    
                    res += QString("%1Serial.%2(%3);\n")
                        .arg(indent)
                        .arg(printLn ? "println" : "print")
                        .arg(formatSerialPart(part, knownVars));
                }
            }
        }
    }

    // Auto-balance any open brackets!
    while (nestLevel > 0) {
        nestLevel--;
        QString fimIndent = QString(baseIndentSpaces + nestLevel * 4, ' ');
        res += QString("%1} // Fechamento automatico do IF\n").arg(fimIndent);
    }

    return res;
}

static bool isPowerRailPin(const QString& pinName) {
    QString upper = pinName.toUpper();
    return upper.contains("GND") || upper.contains("5V") || upper.contains("3V3") || upper.contains("3.3V") || upper.contains("VIN");
}

static bool isPassiveComponent(const QString& type) {
    // Passive/power components that should not generate #define PIN_...
    return type == "resistor" || type == "capacitor" || type == "bess_charger" || type == "bess";
}

// Extract the numeric pin number from a pin name regardless of naming convention.
// Handles: G4 -> 4, GPIO4 -> 4, PIO4 -> 4, D4 -> 4, PA4 -> 4, A4 -> 4, 4 -> 4, IO4 -> 4
static QString extractPinNumber(const QString& pinName) {
    // Strip leading non-digit prefix (G, GPIO, PIO, PA, D, IO, A, etc.)
    static const QRegularExpression leadingAlpha("^[A-Za-z_]+");
    QString stripped = pinName;
    stripped.remove(leadingAlpha);
    if (!stripped.isEmpty() && stripped.at(0).isDigit()) {
        return stripped;
    }
    // Fallback: return as-is if no digits found (e.g. MISO, SCL)
    return pinName;
}

static QString emitPinDefinitions(
    const QVector<ComponentItem*>& components,
    const QHash<ComponentItem*, QString>& sanitized,
    ComponentItem* esp32
) {
    // Build a quick ID → ComponentItem* lookup for tracing connections
    QHash<QString, ComponentItem*> idMap;
    for (auto* c : components) idMap[c->id()] = c;

    // Follow a chain of passive components until we reach the ESP32.
    // Returns the GPIO pin name if found, or empty string.
    auto traceToEsp32 = [&](ComponentItem* startComp, const QString& startPinName,
                             int maxDepth = 6) -> QString {
        // BFS / iterative follow
        struct Hop { ComponentItem* comp; QString pinName; int depth; };
        QVector<Hop> frontier;
        frontier.push_back({startComp, startPinName, 0});

        while (!frontier.isEmpty()) {
            auto [comp, pinName, depth] = frontier.takeFirst();
            if (depth > maxDepth) continue;

            for (const auto& pin : comp->pins()) {
                if (pin.name != pinName) continue;
                if (pin.connectedToComponent.isEmpty()) continue;

                // Is connected to ESP32?
                bool toEsp = (esp32 && pin.connectedToComponent == esp32->id())
                             || pin.connectedToComponent.startsWith("esp32_");
                if (toEsp) {
                    if (!isPowerRailPin(pin.connectedToPin)) {
                        return pin.connectedToPin; // Found GPIO!
                    }
                    return QString(); // Connected to power rail — not usable
                }

                // Connected to another component — follow if it's passive
                ComponentItem* next = idMap.value(pin.connectedToComponent, nullptr);
                if (!next) continue;
                if (!isPassiveComponent(next->componentType())) continue;

                // Continue through each pin of the passive that connects further
                for (const auto& nextPin : next->pins()) {
                    if (nextPin.connectedToComponent.isEmpty()) continue;
                    if (nextPin.connectedToComponent == comp->id()) continue; // don't go back
                    frontier.push_back({next, nextPin.name, depth + 1});
                }
            }
        }
        return QString();
    };

    QString code;
    code += "// ── DEFINIÇÃO DOS PINOS ─────────────────────────────────\n";
    if (!esp32) {
        code += "// AVISO: Nenhum ESP32 encontrado na área de trabalho!\n";
    }

    for (auto* comp : components) {
        if (comp->componentType() == "esp32") continue;

        // RULE 1: Skip passive/power-only components entirely — they never get a #define
        if (isPassiveComponent(comp->componentType())) continue;

        QString gpio;   // GPIO number (e.g. "4", "21")
        QString comment;

        for (const auto& pin : comp->pins()) {
            if (!pin.generateCode) continue;
            if (pin.connectedToComponent.isEmpty()) continue;

            // Case A: directly connected to ESP32
            bool directToEsp = (esp32 && pin.connectedToComponent == esp32->id())
                                || pin.connectedToComponent.startsWith("esp32_");

            if (directToEsp) {
                QString espPin = pin.connectedToPin;
                if (isPowerRailPin(espPin)) continue; // skip power rails
                QString num = extractPinNumber(espPin);
                if (!num.isEmpty() && gpio.isEmpty()) {
                    gpio = num;
                    comment = "";
                }
                continue;
            }

            // Case B: connected to a passive — trace through it to the ESP32
            ComponentItem* next = idMap.value(pin.connectedToComponent, nullptr);
            if (!next) continue;
            if (!isPassiveComponent(next->componentType())) continue;

            // Follow through the passive chain
            // We start from the OTHER pins of the passive (not the one we came from)
            for (const auto& nextPin : next->pins()) {
                if (nextPin.connectedToComponent.isEmpty()) continue;
                if (nextPin.connectedToComponent == comp->id()) continue; // don't go back

                QString found = traceToEsp32(next, nextPin.name, 5);
                if (!found.isEmpty() && gpio.isEmpty()) {
                    QString num = extractPinNumber(found);
                    if (!num.isEmpty()) {
                        gpio = num;
                        comment = " // via " + next->name();
                    }
                }
            }
        }

        if (gpio.isEmpty()) continue; // unconnected or only on power rails

        QString macroName = sanitized[comp];
        code += QString("#define PIN_%1 %2%3\n").arg(macroName).arg(gpio).arg(comment);

        // Also define the sanitized name directly to the PIN_ version, 
        // to allow using "LED_6" directly in expressions as a synonym for PIN_LED_6.
        static const QSet<QString> reservedKeywords = {
            "HIGH", "LOW", "INPUT", "OUTPUT", "INPUT_PULLUP", "LED_BUILTIN",
            "true", "false", "int", "float", "bool", "String", "if", "else", "while", "for"
        };
        if (!reservedKeywords.contains(macroName)) {
            // Only define the direct synonym (e.g. #define LED_3 PIN_LED_3) if it's a basic I/O component.
            // If it's a complex object (motor, dht, etc), we need that name for the object instance!
            bool isComplexObject = (comp->componentType() == "motor" || comp->componentType() == "dht22" || comp->componentType() == "dht" || comp->componentType() == "hcsr04");
            if (!isComplexObject) {
                code += QString("#define %1 PIN_%1\n").arg(macroName);
            }
        }

        if (comp->componentType() == "dht22") {
            code += QString("#define PIN_%1_DATA %2%3\n").arg(macroName).arg(gpio).arg(comment);
        }
        if (comp->componentType() == "hcsr04") {
            QString trigGpio = "";
            QString echoGpio = "";
            for (const auto& pin : comp->pins()) {
                if (pin.name == "TRIG" || pin.name == "ECHO") {
                    QString pinGpio = "";
                    if (!pin.connectedToComponent.isEmpty()) {
                        bool toEsp = (esp32 && pin.connectedToComponent == esp32->id())
                                     || pin.connectedToComponent.startsWith("esp32_");
                        if (toEsp) {
                            pinGpio = extractPinNumber(pin.connectedToPin);
                        } else {
                            ComponentItem* next = idMap.value(pin.connectedToComponent, nullptr);
                            if (next && isPassiveComponent(next->componentType())) {
                                for (const auto& nextPin : next->pins()) {
                                    if (nextPin.connectedToComponent.isEmpty() || nextPin.connectedToComponent == comp->id()) continue;
                                    QString found = traceToEsp32(next, nextPin.name, 5);
                                    if (!found.isEmpty()) {
                                        pinGpio = extractPinNumber(found);
                                        break;
                                    }
                                }
                            }
                        }
                    }
                    if (pin.name == "TRIG") trigGpio = pinGpio;
                    else echoGpio = pinGpio;
                }
            }
            if (trigGpio.isEmpty()) trigGpio = "2"; // fallback
            if (echoGpio.isEmpty()) echoGpio = "3"; // fallback
            code += QString("#define PIN_%1_TRIG %2\n").arg(macroName).arg(trigGpio);
            code += QString("#define PIN_%1_ECHO %2\n").arg(macroName).arg(echoGpio);
        }

        // For custom components with multiple GPIO pins, also emit per-pin macros
        if (auto* custom = dynamic_cast<CustomComponentItem*>(comp)) {
            for (const auto& pin : custom->pins()) {
                if (!pin.generateCode) continue;
                if (pin.connectedToComponent.isEmpty()) continue;

                bool toEsp = (esp32 && pin.connectedToComponent == esp32->id())
                             || pin.connectedToComponent.startsWith("esp32_");
                if (!toEsp) continue;

                QString espPin = pin.connectedToPin;
                if (isPowerRailPin(espPin)) continue;

                QString pinGpio = extractPinNumber(espPin);
                if (pinGpio.isEmpty()) continue;

                QString pinMacroName = QString("%1_%2").arg(macroName).arg(sanitizeIdentifier(pin.name));
                code += QString("#define PIN_%1 %2\n").arg(pinMacroName).arg(pinGpio);
            }
        }
    }
    code += "\n";
    return code;
}


static QString emitStateVariables(
    const QVector<ComponentItem*>& components,
    const QMap<QString, QVector<EventLogicBlock>>& eventBlockStorage,
    const QHash<ComponentItem*, QString>& sanitized
) {
    QString code;
    code += "// ── VARIÁVEIS DE ESTADO E DEBOUNCING ────────────────────\n";

    // 1. Scan for used variables that are NOT components or macros
    QMap<QString, QString> autoDeclared; // Name -> Type ("int" or "bool")
    QSet<QString> componentNames;
    for (auto* c : components) {
        componentNames.insert(sanitized[c]);
        componentNames.insert("PIN_" + sanitized[c]);
    }

    auto scanBlocks = [&](const QVector<EventLogicBlock>& blocks) {
        for (const auto& b : blocks) {
            // Check if this variable is used in a numeric context
            bool isNumericContext = (b.type == LogicBlockType::MATH || 
                                    b.type == LogicBlockType::EEPROM_OP ||
                                    (b.type == LogicBlockType::ACTION && (b.actionCommand.contains("ROTATE") || b.actionCommand.contains("MOTOR") || b.actionCommand.contains("BATTERY"))));

            QString targets[] = {b.assignTarget, b.mathTarget, b.createVarName};
            for (const QString& rawTgt : targets) {
                QString name = sanitizeIdentifier(rawTgt);
                if (name.startsWith("PIN_")) name = name.mid(4);
                
                if (!name.isEmpty() && !componentNames.contains(name) && 
                    !componentNames.contains("PIN_" + name)) {
                    
                    static const QSet<QString> keywords = {"HIGH", "LOW", "INPUT", "OUTPUT", "TRUE", "FALSE", "VALOR"};
                    if (!keywords.contains(name.toUpper())) {
                        if (isNumericContext) {
                            // If used in motor or math, must be int
                            if (autoDeclared.contains(name) && autoDeclared[name] == "bool") {
                                // Upgrade to int if previously declared as bool
                                autoDeclared[name] = "int";
                            } else if (!autoDeclared.contains(name)) {
                                autoDeclared[name] = "int";
                            }
                        } else {
                            // Default to bool if not seen before
                            if (!autoDeclared.contains(name)) {
                                autoDeclared[name] = "bool";
                            }
                        }
                    }
                }
            }
        }
    };

    for (auto it = eventBlockStorage.begin(); it != eventBlockStorage.end(); ++it) {
        scanBlocks(it.value());
    }

    // Now emit the declarations
    for (auto it = autoDeclared.begin(); it != autoDeclared.end(); ++it) {
        QString initial = (it.value() == "bool") ? "false" : "0";
        code += QString("%1 %2 = %3;\n").arg(it.value()).arg(it.key()).arg(initial);
    }

    // ── DECLARAÇÃO DOS REGISTROS EEPROM COMO VARIÁVEIS GLOBAIS ──
    QSet<QString> eepromKeys;
    for (auto it = eventBlockStorage.begin(); it != eventBlockStorage.end(); ++it) {
        for (const auto& b : it.value()) {
            if (b.type == LogicBlockType::EEPROM_OP && b.actionCommand == "SAVE") {
                QString key = b.actionTarget.trimmed().remove(" ");
                if (!key.isEmpty()) {
                    eepromKeys.insert(key);
                }
            }
        }
    }
    
    if (!eepromKeys.isEmpty()) {
        code += "// Registros persistentes salvos na EEPROM\n";
        for (const auto& key : eepromKeys) {
            if (!autoDeclared.contains(key)) {
                QString type = g_eepromKeyTypes.value(key, "int");
                if (type == "String") {
                    code += QString("String %1 = \"\";\n").arg(key);
                } else {
                    code += QString("int %1 = 0;\n").arg(key);
                }
            }
        }
        code += "\n";
    }

    for (auto* comp : components) {
        if (comp->componentType() == "button") {
            QString name = sanitized[comp];
            // Para aoClicar
            code += QString("int lastState_%1 = HIGH;\n").arg(name);
            code += QString("int state_%1 = HIGH;\n").arg(name);
            code += QString("unsigned long lastDebounce_%1 = 0;\n").arg(name);
            
            // Para aoPressionar
            code += QString("int lastState_aoPressionar_%1 = HIGH;\n").arg(name);
            code += QString("int state_aoPressionar_%1 = HIGH;\n").arg(name);
            code += QString("unsigned long lastDebounce_aoPressionar_%1 = 0;\n").arg(name);
            
            // Para aoSoltar
            code += QString("int lastState_aoSoltar_%1 = HIGH;\n").arg(name);
            code += QString("int state_aoSoltar_%1 = HIGH;\n").arg(name);
            code += QString("unsigned long lastDebounce_aoSoltar_%1 = 0;\n").arg(name);
        } else if (comp->componentType() == "led") {
            QString clickKey = QString("%1:aoLigar").arg(comp->id());
            if (eventBlockStorage.contains(clickKey) && !eventBlockStorage[clickKey].isEmpty()) {
                QString name = sanitized[comp];
                code += QString("int lastState_%1 = LOW;\n").arg(name);
            }
        } else if (comp->componentType() == "potentiometer") {
            QString clickKey = QString("%1:aoGirar").arg(comp->id());
            if (eventBlockStorage.contains(clickKey) && !eventBlockStorage[clickKey].isEmpty()) {
                QString name = sanitized[comp];
                code += QString("int lastVal_%1 = -999;\n").arg(name);
            }
        } else if (comp->componentType() == "buzzer") {
            QString clickKey = QString("%1:aoTocar").arg(comp->id());
            if (eventBlockStorage.contains(clickKey) && !eventBlockStorage[clickKey].isEmpty()) {
                QString name = sanitized[comp];
                code += QString("int lastState_%1 = LOW;\n").arg(name);
            }
        } else if (auto* custom = dynamic_cast<CustomComponentItem*>(comp)) {
            QString category = custom->category();
            if (category == "digital_trigger") {
                QString name = sanitized[comp];
                code += QString("int lastState_%1 = HIGH;\n").arg(name);
                code += QString("int state_%1 = HIGH;\n").arg(name);
                code += QString("unsigned long lastDebounce_%1 = 0;\n").arg(name);
            } else if (category == "digital_actuator") {
                QString clickKey = QString("%1:aoLigar").arg(comp->id());
                if (eventBlockStorage.contains(clickKey) && !eventBlockStorage[clickKey].isEmpty()) {
                    QString name = sanitized[comp];
                    code += QString("int lastState_%1 = LOW;\n").arg(name);
                }
            } else if (category == "analog_input") {
                QString clickKey = QString("%1:aoGirar").arg(comp->id());
                if (eventBlockStorage.contains(clickKey) && !eventBlockStorage[clickKey].isEmpty()) {
                    QString name = sanitized[comp];
                    code += QString("int lastVal_%1 = -999;\n").arg(name);
                }
            } else if (category == "active_actuator") {
                QString clickKey = QString("%1:aoTocar").arg(comp->id());
                if (eventBlockStorage.contains(clickKey) && !eventBlockStorage[eventBlockStorage.contains(clickKey) ? clickKey : QString()].isEmpty()) {
                    QString name = sanitized[comp];
                    code += QString("int lastState_%1 = LOW;\n").arg(name);
                }
            }

            // Loop through custom events to declare their state trackers
            for (const auto& ev : custom->definition().customEvents) {
                QString eventKey = QString("%1:%2").arg(custom->id()).arg(ev.callback);
                if (!ev.condition.trimmed().isEmpty() && eventBlockStorage.contains(eventKey) && !eventBlockStorage[eventKey].isEmpty()) {
                    QString compName = sanitized[custom];
                    code += QString("bool lastCond_%1_%2 = false;\n").arg(compName).arg(ev.callback);
                }
            }
        }
    }
    code += "\n";
    return code;
}

QString CodeGenerator::generateArduinoCode(
    const QVector<ComponentItem*>& components,
    const QVector<ConnectionCable*>& cables,
    const QMap<QString, QVector<EventLogicBlock>>& eventBlockStorage,
    const QJsonObject& webPageData
) {
    g_eepromKeys.clear();
    g_eepromKeyTypes.clear();
    
    // First, scan all CREATE_VAR blocks to pre-populate g_eepromKeyTypes with variable types!
    for (auto it = eventBlockStorage.begin(); it != eventBlockStorage.end(); ++it) {
        for (const auto& b : it.value()) {
            if (b.type == LogicBlockType::CREATE_VAR) {
                QString name = b.createVarName.trimmed().remove(" ");
                if (!name.isEmpty()) {
                    if (b.createVarType == VarType::STRING) {
                        g_eepromKeyTypes[name] = "String";
                    } else if (b.createVarType == VarType::FLOAT) {
                        g_eepromKeyTypes[name] = "float";
                    } else if (b.createVarType == VarType::BOOL) {
                        g_eepromKeyTypes[name] = "bool";
                    } else {
                        g_eepromKeyTypes[name] = "int";
                    }
                }
            }
        }
    }

    for (auto it = eventBlockStorage.begin(); it != eventBlockStorage.end(); ++it) {
        for (const auto& b : it.value()) {
            if (b.type == LogicBlockType::EEPROM_OP) {
                QString key = b.actionTarget.trimmed().remove(" ");
                if (!key.isEmpty()) {
                    g_eepromKeys.insert(key);
                    if (!g_eepromKeyTypes.contains(key)) {
                        g_eepromKeyTypes[key] = "int";
                    }
                }
            }
        }
    }

    // ── VALIDATION OF MANDATORY POWER AND GND CONNECTIONS FOR PERIPHERALS ──
    ComponentItem* esp32 = nullptr;
    QHash<QString, ComponentItem*> idMap;
    for (auto* c : components) {
        idMap[c->id()] = c;
        if (c->componentType() == "esp32") {
            esp32 = c;
        }
    }

    // Build a graph of all physical cable connections
    struct ConnectionDest { ComponentItem* comp; QString pin; };
    QHash<QString, QVector<ConnectionDest>> adj;
    for (auto* cable : cables) {
        if (!cable || !cable->sourceComponent() || !cable->targetComponent()) continue;
        
        QString srcId = cable->sourceComponent()->id();
        QString srcPin = cable->sourcePinName();
        QString tgtId = cable->targetComponent()->id();
        QString tgtPin = cable->targetPinName();
        
        QString srcKey = QString("%1:%2").arg(srcId).arg(srcPin);
        QString tgtKey = QString("%1:%2").arg(tgtId).arg(tgtPin);
        
        adj[srcKey].append({cable->targetComponent(), tgtPin});
        adj[tgtKey].append({cable->sourceComponent(), srcPin});
    }

    auto traceToRail = [&](ComponentItem* startComp, const QString& startPinName, bool targetGnd, int maxDepth = 15) -> bool {
        struct Hop { ComponentItem* comp; QString pinName; int depth; };
        QVector<Hop> frontier;
        frontier.push_back({startComp, startPinName, 0});
        QSet<QString> visited;

        while (!frontier.isEmpty()) {
            auto [comp, pinName, depth] = frontier.takeFirst();
            if (depth > maxDepth) continue;

            QString visitKey = QString("%1:%2").arg(comp->id()).arg(pinName);
            if (visited.contains(visitKey)) continue;
            visited.insert(visitKey);

            QString compType = comp->componentType().toLower();

            // A. Check if the current pin or component itself represents the target rail
            QString pinUpper = pinName.trimmed().toUpper();
            if (targetGnd) {
                if (compType == "gnd" || pinUpper.contains("GND") || pinUpper == "VSS") return true;
            } else {
                if (compType == "vcc" || compType == "power" || pinUpper == "5V" || pinUpper == "3V3" || 
                    pinUpper == "3.3V" || pinUpper == "VIN" || pinUpper == "VCC" || pinUpper == "VDD") return true;
            }

            // B. Traverse through other pins of passive components or tracks
            bool isPassive = (compType == "resistor" || compType == "capacitor" || 
                              compType == "bess" || compType == "bess_charger" || 
                              compType == "wire" || compType == "track" || compType.contains("trilha"));
            if (isPassive) {
                for (const auto& nextPin : comp->pins()) {
                    if (nextPin.name == pinName) continue; // don't go back
                    frontier.push_back({comp, nextPin.name, depth}); // Transitioning within passive doesn't increase depth
                }
            }

            // C. Follow all physical cables connected to this specific pin
            QString currentKey = QString("%1:%2").arg(comp->id()).arg(pinName);
            if (adj.contains(currentKey)) {
                for (const auto& neighbor : adj[currentKey]) {
                    ComponentItem* next = neighbor.comp;
                    QString nextPin = neighbor.pin;
                    if (!next) continue;

                    QString nextPinUpper = nextPin.trimmed().toUpper();
                    QString nextType = next->componentType().toLower();

                    // Check if the destination is the target rail
                    bool isEsp = next->id().startsWith("esp32_") || next->id() == "esp32" || 
                                 (esp32 && next->id() == esp32->id());
                    if (isEsp) {
                        if (targetGnd) {
                            if (nextPinUpper.contains("GND") || nextPinUpper == "VSS") return true;
                        } else {
                            if (nextPinUpper == "5V" || nextPinUpper == "3V3" || nextPinUpper == "3.3V" || 
                                nextPinUpper == "VIN" || nextPinUpper == "VCC" || nextPinUpper == "VDD") return true;
                        }
                    }

                    if (targetGnd) {
                        if (nextType == "gnd" || nextPinUpper.contains("GND") || nextPinUpper == "VSS") return true;
                    } else {
                        if (nextType == "vcc" || nextType == "power" || nextPinUpper == "5V" || 
                            nextPinUpper == "3V3" || nextPinUpper == "VCC" || nextPinUpper == "VDD") return true;
                    }

                    // Otherwise, queue the connected pin to continue tracing
                    frontier.push_back({next, nextPin, depth + 1});
                }
            }
        }
        return false;
    };

    for (ComponentItem* comp : components) {
        if (comp->componentType() == "esp32" || isPassiveComponent(comp->componentType())) {
            continue; // Skip the microcontroller and passive components
        }
        for (const Pin& pin : comp->pins()) {
            if (!pin.generateCode) {
                continue; // Skip non-code-generating pins like NC
            }
            QString pinNameUpper = pin.name.trimmed().toUpper();

            // Determine if this is a VCC-class pin
            bool isVccPin = (pinNameUpper == "VCC" || pinNameUpper == "VDD" ||
                             pinNameUpper == "5V"  || pinNameUpper == "3V3" ||
                             pinNameUpper == "3.3V" || pinNameUpper == "VIN");

            // Determine if this is a GND-class pin
            bool isGndPin = (pinNameUpper == "GND" || pinNameUpper == "VSS" ||
                             pinNameUpper == "GND1" || pinNameUpper == "GND2");

            if (!isVccPin && !isGndPin) continue;

            if (pin.connectedToComponent.isEmpty()) {
                return QString("// ERROR: O componente '%1' (%2) possui o pino '%3' desconectado. "
                               "Por favor, certifique-se de conectar todos os pinos de VCC/alimentação e GND para compilar ou simular!")
                       .arg(comp->name())
                       .arg(comp->id())
                       .arg(pin.name);
            }

            // Also validate that the other end is actually a power/GND rail (not a GPIO pin)
            if (isVccPin && !pin.connectedToPin.isEmpty()) {
                QString otherPin = pin.connectedToPin.trimmed().toUpper();
                bool validPowerRail = (otherPin == "5V"  || otherPin == "3V3"  ||
                                       otherPin == "3.3V" || otherPin == "VIN"  ||
                                       otherPin == "VCC"  || otherPin == "VDD"  ||
                                       otherPin == "VBUS" || otherPin == "VUSB");
                
                if (!validPowerRail) {
                    ComponentItem* next = idMap.value(pin.connectedToComponent, nullptr);
                    if (next) {
                        validPowerRail = traceToRail(next, pin.connectedToPin, false);
                    }
                }

                if (!validPowerRail) {
                    return QString("// ERROR: O componente '%1' (%2) tem o pino VCC conectado a '%3' que não é um rail de alimentação (esperado: 5V, 3V3 ou VCC). "
                                   "Conecte o VCC a um pino de alimentação!")
                           .arg(comp->name())
                           .arg(comp->id())
                           .arg(pin.connectedToPin);
                }
            }

            if (isGndPin && !pin.connectedToPin.isEmpty()) {
                QString otherPin = pin.connectedToPin.trimmed().toUpper();
                bool validGnd = otherPin.contains("GND") || otherPin == "VSS";
                
                if (!validGnd) {
                    ComponentItem* next = idMap.value(pin.connectedToComponent, nullptr);
                    if (next) {
                        validGnd = traceToRail(next, pin.connectedToPin, true);
                    }
                }

                if (!validGnd) {
                    return QString("// ERROR: O componente '%1' (%2) tem o pino GND conectado a '%3' que não é um pino de terra (esperado: GND). "
                                   "Conecte o GND ao pino GND!")
                           .arg(comp->name())
                           .arg(comp->id())
                           .arg(pin.connectedToPin);
                }
            }
        }
    }

    QString code;
    code += "/*\n";
    code += " *  =======================================================\n";
    code += " *  CÓDIGO GERADO AUTOMATICAMENTE PELA IDE EMBEDDED\n";
    code += " *  Paradigma Orientado a Eventos e Roteamento Físico\n";
    code += " *  =======================================================\n";
    code += " */\n\n";

    // Deduplicate and compile includes for Custom Components
    QStringList includesList;
    for (auto* comp : components) {
        if (comp->componentType() == "dht22") {
            if (!includesList.contains("#include <DHT.h>")) {
                includesList.append("#include <DHT.h>");
            }
        }
        if (auto* custom = dynamic_cast<CustomComponentItem*>(comp)) {
            QString rawIncludes = custom->definition().codeIncludes.trimmed();
            if (!rawIncludes.isEmpty()) {
                QString compiledIncludes = replaceCustomComponentPlaceholders(rawIncludes, custom);
                QStringList lines = compiledIncludes.split('\n');
                for (auto& line : lines) {
                    QString trimmedLine = line.trimmed();
                    if (!trimmedLine.isEmpty() && !includesList.contains(trimmedLine)) {
                        includesList.append(trimmedLine);
                    }
                }
            }
        }
    }

    if (webPageData.value("enabled").toBool()) {
        if (!includesList.contains("#include <WiFi.h>")) includesList.append("#include <WiFi.h>");
        if (!includesList.contains("#include <WebServer.h>")) includesList.append("#include <WebServer.h>");
    }

    if (!includesList.isEmpty()) {
        code += "// ── BIBLIOTECAS ADICIONAIS ─────────────────────────────\n";
        for (const auto& inc : includesList) {
            code += inc + "\n";
        }
        code += "\n";
    }

    // Precompute sanitized identifiers per component to avoid repeated computation
    QHash<ComponentItem*, QString> sanitized;
    for (auto* comp : components) sanitized[comp] = sanitizeIdentifier(comp->name());

    // Initialize EEPROM address mapping registry for this generation
    QMap<QString, int> eepromOffsets;
    int nextEepromOffset = 0;

    // Resolve component pin mapping relative to ESP32 (already resolved above)

    // Identify which loop calls are already visually present in esp32:aoLoop blocks
    QStringList alreadyCalled;
    if (esp32) {
        QString loopKey = QString("%1:aoLoop").arg(esp32->id());
        if (eventBlockStorage.contains(loopKey)) {
            for (const auto& block : eventBlockStorage[loopKey]) {
                if (block.type == LogicBlockType::ACTION && block.actionCommand == "CALL_FUNCTION") {
                    QString tgt = block.actionTarget.trimmed();
                    if (!tgt.endsWith("()")) {
                        tgt += "()";
                    }
                    alreadyCalled.append(tgt);
                }
            }
        }
    }


    code += emitPinDefinitions(components, sanitized, esp32);

    // Inject Motor wrapper if any motors exist
    bool hasMotors = false;
    for (auto* comp : components) {
        if (comp->componentType() == "motor") {
            hasMotors = true;
            break;
        }
    }
    
    if (hasMotors) {
        code += "// ── DRIVER NATIVO DE MOTOR ESP32 ──\n";
        code += "struct IdeMotorDriver {\n";
        code += "    int _pin = -1;\n";
        code += "    int _channel = 0;\n";
        code += "    int _type = 0; // 0=servo, 1=servo360, 2=dc\n";
        code += "#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3\n";
        code += "    void attach(int pin, int type = 0, int channel = -1) {\n";
        code += "        _pin = pin;\n";
        code += "        _type = type;\n";
        code += "        ledcAttach(_pin, _type == 2 ? 1000 : 50, 16);\n";
        code += "    }\n";
        code += "    void write(int val) {\n";
        code += "        if (_type == 2) { // DC PWM\n";
        code += "            int duty = map(val, 0, 100, 0, 65535);\n";
        code += "            ledcWrite(_pin, duty);\n";
        code += "        } else {\n";
        code += "            int pulse_us = map(val, 0, 180, 500, 2500);\n";
        code += "            int duty = (pulse_us * 65535) / 20000;\n";
        code += "            ledcWrite(_pin, duty);\n";
        code += "        }\n";
        code += "    }\n";
        code += "    void stop() {\n";
        code += "        write(_type == 1 ? 90 : 0);\n";
        code += "    }\n";
        code += "#else\n";
        code += "    void attach(int pin, int type = 0, int channel = 2) {\n";
        code += "        _pin = pin;\n";
        code += "        _type = type;\n";
        code += "        _channel = channel;\n";
        code += "        ledcSetup(_channel, _type == 2 ? 1000 : 50, 16);\n";
        code += "        ledcAttachPin(_pin, _channel);\n";
        code += "    }\n";
        code += "    void write(int val) {\n";
        code += "        if (_type == 2) { // DC PWM\n";
        code += "            int duty = map(val, 0, 100, 0, 65535);\n";
        code += "            ledcWrite(_channel, duty);\n";
        code += "        } else {\n";
        code += "            int pulse_us = map(val, 0, 180, 500, 2500);\n";
        code += "            int duty = (pulse_us * 65535) / 20000;\n";
        code += "            ledcWrite(_channel, duty);\n";
        code += "        }\n";
        code += "    }\n";
        code += "    void stop() {\n";
        code += "        write(_type == 1 ? 90 : 0);\n";
        code += "    }\n";
        code += "#endif\n";
        code += "};\n\n";
        
        code += "// ── INSTÂNCIAS DE MOTORES ──\n";
        int motorChannel = 2; // Começa no canal 2 para evitar conflito com tons
        for (auto* comp : components) {
            if (comp->componentType() == "motor") {
                QString motorName = sanitized[comp];
                code += QString("IdeMotorDriver %1; // Canal PWM %2\n").arg(motorName).arg(motorChannel++);
            }
        }
        code += "\n";
    }
    
    if (webPageData.value("enabled").toBool()) {
        QJsonArray elements = webPageData["elements"].toArray();
        for (int i = 0; i < elements.size(); ++i) {
            QJsonObject el = elements[i].toObject();
            if (el["type"].toString() == "Text") {
                QString id = el["id"].toString();
                QString fc = el.contains("formatColor") ? el["formatColor"].toString() : "#01579b";
                int fs = el.contains("formatSize") ? el["formatSize"].toInt() : 16;
                bool fb = el.contains("formatBold") ? el["formatBold"].toBool() : true;
                
                code += QString("String _webFormat_%1_color = \"%2\";\n").arg(id).arg(fc);
                code += QString("String _webFormat_%1_size = \"%2px\";\n").arg(id).arg(fs);
                code += QString("String _webFormat_%1_weight = \"%2\";\n").arg(id).arg(fb ? "bold" : "normal");
                
                QString boundVar = el["boundVar"].toString();
                if (boundVar.isEmpty()) {
                    QString defaultText = el["text"].toString();
                    code += QString("String %1 = \"%2\";\n").arg(id).arg(defaultText);
                }
            }
        }
    }
    
    code += emitStateVariables(components, eventBlockStorage, sanitized);

    // ── INSTANCIAÇÃO E GLOBAIS DO DHT22 ──
    bool hasDht22 = false;
    for (auto* comp : components) {
        if (comp->componentType() == "dht22") {
            hasDht22 = true;
            break;
        }
    }
    if (hasDht22) {
        code += "// ── INSTÂNCIAS E VARIÁVEIS DO DHT22 ─────────────────────\n";
        for (auto* comp : components) {
            if (comp->componentType() == "dht22") {
                QString name = sanitized[comp];
                QString suffix = getNumericSuffixFromSanitized(name);
                code += QString("float umidade%1 = 0.0;\n").arg(suffix);
                code += QString("float temperatura%1 = 0.0;\n").arg(suffix);
                code += QString("DHT dht%1(PIN_%1_DATA, DHT22);\n").arg(name);
                code += QString("void %1_eventAoCalcularUmidade() __attribute__((weak));\n").arg(name);
                code += QString("void %1_eventAoCalcularUmidade() {}\n").arg(name);
                code += QString("void %1_eventAoCalcularTemperatura() __attribute__((weak));\n").arg(name);
                code += QString("void %1_eventAoCalcularTemperatura() {}\n\n").arg(name);
            }
        }
        code += "\n";
    }

    // ── INSTANCIAÇÃO E GLOBAIS DO HC-SR04 ──
    bool hasHcsr04 = false;
    for (auto* comp : components) {
        if (comp->componentType() == "hcsr04") {
            hasHcsr04 = true;
            break;
        }
    }
    if (hasHcsr04) {
        code += "// ── VARIÁVEIS E STUBS DO SENSOR HC-SR04 ─────────────────\n";
        for (auto* comp : components) {
            if (comp->componentType() == "hcsr04") {
                QString name = sanitized[comp];
                QString suffix = getNumericSuffixFromSanitized(name);
                code += QString("float distancia%1 = 0.0;\n").arg(suffix);
                code += QString("void %1_eventAoMedir() __attribute__((weak));\n").arg(name);
                code += QString("void %1_eventAoMedir() {}\n\n").arg(name);
            }
        }
        code += "\n";
    }

    // Append Custom Component Globals, Loop Declarations, Custom Functions, and Event Handlers
    bool hasCustomGlobals = false;
    for (auto* comp : components) {
        if (auto* custom = dynamic_cast<CustomComponentItem*>(comp)) {
            QString compName = sanitizeIdentifier(custom->name());
            CustomComponentDef def = custom->definition();

            if (!hasCustomGlobals) {
                code += "// ── INSTANCIAÇÃO DE COMPONENTES MODELADOS ──────────────\n";
                hasCustomGlobals = true;
            }

            code += QString("// Componente: %1 (%2)\n").arg(custom->name()).arg(custom->id());

            // 1. Loop Declarations (Global state variables)
            if (!def.loopDeclarations.isEmpty()) {
                code += "  // Loop Declarations\n";
                for (const auto& decl : def.loopDeclarations) {
                    QString initialVal = decl.initialValue.trimmed();
                    if (initialVal.isEmpty()) initialVal = "0";
                    QString varDecl = QString("%1 %2_%3 = %4;\n").arg(decl.type).arg(compName).arg(decl.name).arg(initialVal);
                    code += "  " + replaceCustomComponentPlaceholders(varDecl, custom);
                }
            }

            // 2. Event Handlers (User logic wrappers) - only generate when there is logic
            for (const auto& ev : def.customEvents) {
                QString eventKey = QString("%1:%2").arg(custom->id()).arg(ev.callback);
                bool userImplementsCallback = false;
                for (const auto& fn : def.customFunctions) {
                    if (fn.name.trimmed() == ev.callback.trimmed()) {
                        userImplementsCallback = true;
                        break;
                    }
                }
                // If user provided the callback implementation, skip generator (user owns it)
                if (userImplementsCallback) {
                    continue;
                }
                // Only generate a wrapper if there are blocks associated (avoid empty stubs)
                if (!eventBlockStorage.contains(eventKey) || eventBlockStorage[eventKey].isEmpty()) {
                    continue;
                }
                code += QString("  // Slot de Evento: %1\n").arg(ev.name);
                code += QString("  void %1_%2() {\n").arg(compName).arg(ev.callback);
                code += compileBlocks(eventBlockStorage[eventKey], components, 6, custom, &sanitized, &eepromOffsets, &nextEepromOffset);
                code += "  }\n";
            }

            // 2b. Monitores para eventos customizados (gerar como funções separadas)
            for (const auto& ev : def.customEvents) {
                QString eventKey = QString("%1:%2").arg(custom->id()).arg(ev.callback);
                if (!ev.condition.trimmed().isEmpty() && eventBlockStorage.contains(eventKey) && !eventBlockStorage[eventKey].isEmpty()) {
                    // Gera função monitor_<COMP>_<callback>()
                    code += QString("  void monitor_%1_%2() {\n").arg(compName).arg(ev.callback);
                    code += QString("      bool currentCond_%1_%2 = (%3);\n")
                                .arg(compName)
                                .arg(ev.callback)
                                .arg(replaceCustomComponentPlaceholders(ev.condition, custom));
                    code += QString("      if (currentCond_%1_%2 && !lastCond_%1_%2) {\n").arg(compName).arg(ev.callback);
                    code += QString("          Serial.println(\"Evento customizado %1 disparado!\");\n").arg(ev.name);
                    code += QString("          %1_%2();\n").arg(compName).arg(ev.callback);
                    code += "      }\n";
                    code += QString("      lastCond_%1_%2 = currentCond_%1_%2;\n").arg(compName).arg(ev.callback);
                    code += "  }\n";
                }
            }

            // 3. Custom Functions
            if (!def.customFunctions.isEmpty()) {
                code += "  // Custom Functions\n";
                for (const auto& func : def.customFunctions) {
                    QString compiledFunc = replaceCustomComponentPlaceholders(func.code, custom);
                    code += compiledFunc + "\n";
                }
            }

            // 4. Legacy codeGlobals (for backward compatibility)
            QString rawGlobals = def.codeGlobals.trimmed();
            if (!rawGlobals.isEmpty()) {
                code += "  // Legacy Globals\n";
                code += "  " + replaceCustomComponentPlaceholders(rawGlobals, custom) + "\n";
            }
            code += "\n";
        }
    }
    if (hasCustomGlobals) {
        code += "\n";
    }

    // Native handlers in the same function-event pattern as custom components
    bool hasNativeHandlers = false;
    for (auto* comp : components) {
        if (comp->componentType() == "button") {
            // aoClicar
            {
                QString eventKey = QString("%1:aoClicar").arg(comp->id());
                if (eventBlockStorage.contains(eventKey) && !eventBlockStorage[eventKey].isEmpty()) {
                    QString eventBody = compileBlocks(eventBlockStorage[eventKey], components, 4, nullptr, &sanitized, &eepromOffsets, &nextEepromOffset);
                    if (!eventBody.trimmed().isEmpty()) {
                        if (!hasNativeHandlers) {
                            code += "// ── EVENTOS PADRÃO (BOTÃO/LED) ───────────────────────\n";
                            hasNativeHandlers = true;
                        }
                        QString fnName = QString("%1_eventAoClicar").arg(sanitizeIdentifier(comp->name()));
                        code += QString("void %1() {\n").arg(fnName);
                        code += eventBody;
                        code += "}\n\n";
                    }
                }
            }
            // aoPressionar
            {
                QString eventKey = QString("%1:aoPressionar").arg(comp->id());
                if (eventBlockStorage.contains(eventKey) && !eventBlockStorage[eventKey].isEmpty()) {
                    QString eventBody = compileBlocks(eventBlockStorage[eventKey], components, 4, nullptr, &sanitized, &eepromOffsets, &nextEepromOffset);
                    if (!eventBody.trimmed().isEmpty()) {
                        if (!hasNativeHandlers) {
                            code += "// ── EVENTOS PADRÃO (BOTÃO/LED) ───────────────────────\n";
                            hasNativeHandlers = true;
                        }
                        QString fnName = QString("%1_eventAoPressionar").arg(sanitizeIdentifier(comp->name()));
                        code += QString("void %1() {\n").arg(fnName);
                        code += eventBody;
                        code += "}\n\n";
                    }
                }
            }
            // aoSoltar
            {
                QString eventKey = QString("%1:aoSoltar").arg(comp->id());
                if (eventBlockStorage.contains(eventKey) && !eventBlockStorage[eventKey].isEmpty()) {
                    QString eventBody = compileBlocks(eventBlockStorage[eventKey], components, 4, nullptr, &sanitized, &eepromOffsets, &nextEepromOffset);
                    if (!eventBody.trimmed().isEmpty()) {
                        if (!hasNativeHandlers) {
                            code += "// ── EVENTOS PADRÃO (BOTÃO/LED) ───────────────────────\n";
                            hasNativeHandlers = true;
                        }
                        QString fnName = QString("%1_eventAoSoltar").arg(sanitizeIdentifier(comp->name()));
                        code += QString("void %1() {\n").arg(fnName);
                        code += eventBody;
                        code += "}\n\n";
                    }
                }
            }
        } else if (comp->componentType() == "led") {
            QString eventKey = QString("%1:aoLigar").arg(comp->id());
            if (eventBlockStorage.contains(eventKey) && !eventBlockStorage[eventKey].isEmpty()) {
                QString eventBody = compileBlocks(eventBlockStorage[eventKey], components, 4, nullptr, &sanitized, &eepromOffsets, &nextEepromOffset);
                if (eventBody.trimmed().isEmpty()) {
                    continue;
                }
                if (!hasNativeHandlers) {
                    code += "// ── EVENTOS PADRÃO (NATIVOS) ──────────────────────────\n";
                    hasNativeHandlers = true;
                }
                QString fnName = QString("%1_eventAoLigar").arg(sanitizeIdentifier(comp->name()));
                code += QString("void %1() {\n").arg(fnName);
                code += eventBody;
                code += "}\n\n";
            }
        } else if (comp->componentType() == "potentiometer") {
            QString eventKey = QString("%1:aoGirar").arg(comp->id());
            if (eventBlockStorage.contains(eventKey) && !eventBlockStorage[eventKey].isEmpty()) {
                QString eventBody = compileBlocks(eventBlockStorage[eventKey], components, 4, nullptr, &sanitized, &eepromOffsets, &nextEepromOffset);
                if (eventBody.trimmed().isEmpty()) {
                    continue;
                }
                if (!hasNativeHandlers) {
                    code += "// ── EVENTOS PADRÃO (NATIVOS) ──────────────────────────\n";
                    hasNativeHandlers = true;
                }
                QString fnName = QString("%1_eventAoGirar").arg(sanitizeIdentifier(comp->name()));
                code += QString("void %1() {\n").arg(fnName);
                code += eventBody;
                code += "}\n\n";
            }
        } else if (comp->componentType() == "buzzer") {
            QString eventKey = QString("%1:aoTocar").arg(comp->id());
            if (eventBlockStorage.contains(eventKey) && !eventBlockStorage[eventKey].isEmpty()) {
                QString eventBody = compileBlocks(eventBlockStorage[eventKey], components, 4, nullptr, &sanitized, &eepromOffsets, &nextEepromOffset);
                if (eventBody.trimmed().isEmpty()) {
                    continue;
                }
                if (!hasNativeHandlers) {
                    code += "// ── EVENTOS PADRÃO (NATIVOS) ──────────────────────────\n";
                    hasNativeHandlers = true;
                }
                QString fnName = QString("%1_eventAoTocar").arg(sanitizeIdentifier(comp->name()));
                code += QString("void %1() {\n").arg(fnName);
                code += eventBody;
                code += "}\n\n";
            }
        } else if (comp->componentType() == "dht22") {
            // aoCalcularUmidade
            {
                QString eventKey = QString("%1:aoCalcularUmidade").arg(comp->id());
                if (eventBlockStorage.contains(eventKey) && !eventBlockStorage[eventKey].isEmpty()) {
                    QString eventBody = compileBlocks(eventBlockStorage[eventKey], components, 4, nullptr, &sanitized, &eepromOffsets, &nextEepromOffset);
                    if (!eventBody.trimmed().isEmpty()) {
                        if (!hasNativeHandlers) {
                            code += "// ── EVENTOS PADRÃO (NATIVOS) ──────────────────────────\n";
                            hasNativeHandlers = true;
                        }
                        QString fnName = QString("%1_eventAoCalcularUmidade").arg(sanitizeIdentifier(comp->name()));
                        code += QString("void %1() {\n").arg(fnName);
                        code += eventBody;
                        code += "}\n\n";
                    }
                }
            }
            // aoCalcularTemperatura
            {
                QString eventKey = QString("%1:aoCalcularTemperatura").arg(comp->id());
                if (eventBlockStorage.contains(eventKey) && !eventBlockStorage[eventKey].isEmpty()) {
                    QString eventBody = compileBlocks(eventBlockStorage[eventKey], components, 4, nullptr, &sanitized, &eepromOffsets, &nextEepromOffset);
                    if (!eventBody.trimmed().isEmpty()) {
                        if (!hasNativeHandlers) {
                            code += "// ── EVENTOS PADRÃO (NATIVOS) ──────────────────────────\n";
                            hasNativeHandlers = true;
                        }
                        QString fnName = QString("%1_eventAoCalcularTemperatura").arg(sanitizeIdentifier(comp->name()));
                        code += QString("void %1() {\n").arg(fnName);
                        code += eventBody;
                        code += "}\n\n";
                    }
                }
            }
        } else if (comp->componentType() == "hcsr04") {
            // aoMedir
            {
                QString eventKey = QString("%1:aoMedir").arg(comp->id());
                if (eventBlockStorage.contains(eventKey) && !eventBlockStorage[eventKey].isEmpty()) {
                    QString eventBody = compileBlocks(eventBlockStorage[eventKey], components, 4, nullptr, &sanitized, &eepromOffsets, &nextEepromOffset);
                    if (!eventBody.trimmed().isEmpty()) {
                        if (!hasNativeHandlers) {
                            code += "// ── EVENTOS PADRÃO (NATIVOS) ──────────────────────────\n";
                            hasNativeHandlers = true;
                        }
                        QString fnName = QString("%1_eventAoMedir").arg(sanitizeIdentifier(comp->name()));
                        code += QString("void %1() {\n").arg(fnName);
                        code += eventBody;
                        code += "}\n\n";
                    }
                }
            }
        }
    }

    // Native monitors call the wrappers from loop()
    bool hasNativeMonitors = false;
    for (auto* comp : components) {
        if (comp->componentType() == "button") {
            // aoClicar
            {
                QString eventKey = QString("%1:aoClicar").arg(comp->id());
                if (eventBlockStorage.contains(eventKey) && !eventBlockStorage[eventKey].isEmpty()) {
                    if (!hasNativeMonitors) {
                        code += "// ── MONITORES NATIVOS ───────────────────────────────────\n";
                        hasNativeMonitors = true;
                    }
                    QString name = sanitizeIdentifier(comp->name());
                    QString pinMacro = QString("PIN_%1").arg(name);
                    code += QString("void monitor_%1_eventAoClicar() {\n").arg(name);
                    code += QString("    static int lastReading_%1 = HIGH;\n").arg(name);
                    code += QString("    static unsigned long lastTime_%1 = 0;\n").arg(name);
                    code += QString("    int reading = digitalRead(PIN_%1);\n").arg(name);
                    code += QString("    if (reading != lastReading_%1) {\n").arg(name);
                    code += QString("        lastTime_%1 = millis();\n").arg(name);
                    code += "    }\n";
                    code += "#ifdef IS_SIMULATION\n";
                    code += "    bool debouncePassed = true;\n";
                    code += "#else\n";
                    code += QString("    bool debouncePassed = (millis() - lastTime_%1) > 50;\n").arg(name);
                    code += "#endif\n";
                    code += "    if (debouncePassed) {\n";
                    code += QString("        if (reading == LOW && state_%1 == HIGH) {\n").arg(name);
                    code += QString("            %1_eventAoClicar();\n").arg(name);
                    code += "        }\n";
                    code += QString("        state_%1 = reading;\n").arg(name);
                    code += "    }\n";
                    code += QString("    lastReading_%1 = reading;\n").arg(name);
                    code += "}\n\n";
                }
            }
            // aoPressionar
            {
                QString eventKey = QString("%1:aoPressionar").arg(comp->id());
                if (eventBlockStorage.contains(eventKey) && !eventBlockStorage[eventKey].isEmpty()) {
                    if (!hasNativeMonitors) {
                        code += "// ── MONITORES NATIVOS ───────────────────────────────────\n";
                        hasNativeMonitors = true;
                    }
                    QString name = sanitizeIdentifier(comp->name());
                    QString pinMacro = QString("PIN_%1").arg(name);
                    code += QString("void monitor_%1_eventAoPressionar() {\n").arg(name);
                    if (auto* custom = dynamic_cast<CustomComponentItem*>(comp)) {
                        QString customRead = custom->definition().codeReadExpression.trimmed();
                        if (!customRead.isEmpty()) {
                            QString compiledRead = replaceCustomComponentPlaceholders(customRead, custom);
                            code += QString("    int reading_%1 = %2;\n").arg(name).arg(compiledRead);
                        } else {
                            code += QString("    int reading_%1 = digitalRead(%2);\n").arg(name).arg(pinMacro);
                        }
                    } else {
                        code += QString("    int reading_%1 = digitalRead(%2);\n").arg(name).arg(pinMacro);
                    }
                    code += QString("    if (reading_%1 != lastState_aoPressionar_%2) {\n").arg(name).arg(name);
                    code += QString("        lastDebounce_aoPressionar_%1 = millis();\n").arg(name);
                    code += "    }\n";
                    code += QString("    if ((millis() - lastDebounce_aoPressionar_%1) > 50) {\n").arg(name);
                    code += QString("        if (reading_%1 != state_aoPressionar_%2) {\n").arg(name).arg(name);
                    code += QString("            state_aoPressionar_%2 = reading_%1;\n").arg(name).arg(name);
                    code += QString("            if (state_aoPressionar_%1 == LOW) {\n").arg(name);
                    code += QString("                %1_eventAoPressionar();\n").arg(name);
                    code += "            }\n";
                    code += "        }\n";
                    code += "    }\n";
                    code += QString("    lastState_aoPressionar_%1 = reading_%2;\n").arg(name).arg(name);
                    code += "}\n\n";
                }
            }
            // aoSoltar
            {
                QString eventKey = QString("%1:aoSoltar").arg(comp->id());
                if (eventBlockStorage.contains(eventKey) && !eventBlockStorage[eventKey].isEmpty()) {
                    if (!hasNativeMonitors) {
                        code += "// ── MONITORES NATIVOS ───────────────────────────────────\n";
                        hasNativeMonitors = true;
                    }
                    QString name = sanitizeIdentifier(comp->name());
                    QString pinMacro = QString("PIN_%1").arg(name);
                    code += QString("void monitor_%1_eventAoSoltar() {\n").arg(name);
                    if (auto* custom = dynamic_cast<CustomComponentItem*>(comp)) {
                        QString customRead = custom->definition().codeReadExpression.trimmed();
                        if (!customRead.isEmpty()) {
                            QString compiledRead = replaceCustomComponentPlaceholders(customRead, custom);
                            code += QString("    int reading_%1 = %2;\n").arg(name).arg(compiledRead);
                        } else {
                            code += QString("    int reading_%1 = digitalRead(%2);\n").arg(name).arg(pinMacro);
                        }
                    } else {
                        code += QString("    int reading_%1 = digitalRead(%2);\n").arg(name).arg(pinMacro);
                    }
                    code += QString("    if (reading_%1 != lastState_aoSoltar_%2) {\n").arg(name).arg(name);
                    code += QString("        lastDebounce_aoSoltar_%1 = millis();\n").arg(name);
                    code += "    }\n";
                    code += QString("    if ((millis() - lastDebounce_aoSoltar_%1) > 50) {\n").arg(name);
                    code += QString("        if (reading_%1 != state_aoSoltar_%2) {\n").arg(name).arg(name);
                    code += QString("            state_aoSoltar_%2 = reading_%1;\n").arg(name).arg(name);
                    code += QString("            if (state_aoSoltar_%1 == HIGH) {\n").arg(name);
                    code += QString("                %1_eventAoSoltar();\n").arg(name);
                    code += "            }\n";
                    code += "        }\n";
                    code += "    }\n";
                    code += QString("    lastState_aoSoltar_%1 = reading_%2;\n").arg(name).arg(name);
                    code += "}\n\n";
                }
            }
        } else if (comp->componentType() == "led") {
            QString eventKey = QString("%1:aoLigar").arg(comp->id());
            if (eventBlockStorage.contains(eventKey) && !eventBlockStorage[eventKey].isEmpty()) {
                if (!hasNativeMonitors) {
                    code += "// ── MONITORES NATIVOS ───────────────────────────────────\n";
                    hasNativeMonitors = true;
                }
                QString name = sanitizeIdentifier(comp->name());
                QString pinMacro = QString("PIN_%1").arg(name);
                code += QString("void monitor_%1_eventAoLigar() {\n").arg(name);
                code += QString("    int currentState_%1 = digitalRead(%2);\n").arg(name).arg(pinMacro);
                code += QString("    if (currentState_%1 == HIGH && lastState_%2 == LOW) {\n").arg(name).arg(name);
                code += QString("        %1_eventAoLigar();\n").arg(name);
                code += "    }\n";
                code += QString("    lastState_%1 = currentState_%2;\n").arg(name).arg(name);
                code += "}\n\n";
            }
        } else if (comp->componentType() == "potentiometer") {
            QString eventKey = QString("%1:aoGirar").arg(comp->id());
            if (eventBlockStorage.contains(eventKey) && !eventBlockStorage[eventKey].isEmpty()) {
                if (!hasNativeMonitors) {
                    code += "// ── MONITORES NATIVOS ───────────────────────────────────\n";
                    hasNativeMonitors = true;
                }
                QString name = sanitizeIdentifier(comp->name());
                QString pinMacro = QString("PIN_%1").arg(name);
                code += QString("void monitor_%1_eventAoGirar() {\n").arg(name);
                if (auto* custom = dynamic_cast<CustomComponentItem*>(comp)) {
                    QString customRead = custom->definition().codeReadExpression.trimmed();
                    if (!customRead.isEmpty()) {
                        QString compiledRead = replaceCustomComponentPlaceholders(customRead, custom);
                        code += QString("    int currentVal_%1 = %2;\n").arg(name).arg(compiledRead);
                    } else {
                        code += QString("    int currentVal_%1 = analogRead(%2);\n").arg(name).arg(pinMacro);
                    }
                } else {
                    code += QString("    int currentVal_%1 = analogRead(%2);\n").arg(name).arg(pinMacro);
                }
                code += QString("    if (lastVal_%1 == -999 || abs(currentVal_%1 - lastVal_%1) > 5) {\n").arg(name).arg(name).arg(name);
                code += QString("        %1_eventAoGirar();\n").arg(name);
                code += QString("        lastVal_%1 = currentVal_%2;\n").arg(name).arg(name);
                code += "    }\n";
                code += "}\n\n";
            }
        } else if (comp->componentType() == "buzzer") {
            QString eventKey = QString("%1:aoTocar").arg(comp->id());
            if (eventBlockStorage.contains(eventKey) && !eventBlockStorage[eventKey].isEmpty()) {
                if (!hasNativeMonitors) {
                    code += "// ── MONITORES NATIVOS ───────────────────────────────────\n";
                    hasNativeMonitors = true;
                }
                QString name = sanitizeIdentifier(comp->name());
                QString pinMacro = QString("PIN_%1").arg(name);
                code += QString("void monitor_%1_eventAoTocar() {\n").arg(name);
                code += QString("    int currentState_%1 = digitalRead(%2);\n").arg(name).arg(pinMacro);
                code += QString("    if (currentState_%1 == HIGH && lastState_%2 == LOW) {\n").arg(name).arg(name);
                code += QString("        %1_eventAoTocar();\n").arg(name);
                code += "    }\n";
                code += QString("    lastState_%1 = currentState_%2;\n").arg(name).arg(name);
                code += "}\n\n";
            }
        } else if (comp->componentType() == "dht22") {
            QString name = sanitizeIdentifier(comp->name());
            QString suffix = getNumericSuffixFromSanitized(name);
            QString humKey = QString("%1:aoCalcularUmidade").arg(comp->id());
            bool hasHum = eventBlockStorage.contains(humKey) && !eventBlockStorage[humKey].isEmpty();
            QString tempKey = QString("%1:aoCalcularTemperatura").arg(comp->id());
            bool hasTemp = eventBlockStorage.contains(tempKey) && !eventBlockStorage[tempKey].isEmpty();

            if (!hasNativeMonitors) {
                code += "// ── MONITORES NATIVOS ───────────────────────────────────\n";
                hasNativeMonitors = true;
            }
            code += QString("void monitor_%1_eventAoCalcular() {\n").arg(name);
            code += QString("    // Leitura periódica de DHT22: %1\n").arg(comp->name());
            code += QString("    static unsigned long lastDHTRead_%1 = 0;\n").arg(name);
            code += QString("    if (millis() - lastDHTRead_%1 >= 2000) {\n").arg(name);
            code += QString("        lastDHTRead_%1 = millis();\n").arg(name);
            code += QString("        float h = dht%1.readHumidity();\n").arg(name);
            code += QString("        float t = dht%1.readTemperature();\n").arg(name);
            code += QString("        if (!isnan(h)) {\n");
            code += QString("            umidade%1 = h;\n").arg(suffix);
            code += QString("            %1_eventAoCalcularUmidade();\n").arg(name);
            code += QString("        }\n");
            code += QString("        if (!isnan(t)) {\n");
            code += QString("            temperatura%1 = t;\n").arg(suffix);
            code += QString("            %1_eventAoCalcularTemperatura();\n").arg(name);
            code += QString("        }\n");
            code += QString("    }\n");
            code += "}\n\n";
        } else if (comp->componentType() == "hcsr04") {
            QString name = sanitizeIdentifier(comp->name());
            QString suffix = getNumericSuffixFromSanitized(name);

            if (!hasNativeMonitors) {
                code += "// ── MONITORES NATIVOS ───────────────────────────────────\n";
                hasNativeMonitors = true;
            }
            code += QString("void monitor_%1_eventAoMedir() {\n").arg(name);
            code += QString("    // Leitura periódica de HC-SR04: %1\n").arg(comp->name());
            code += QString("    static unsigned long lastMeasure_%1 = 0;\n").arg(name);
            code += QString("    if (millis() - lastMeasure_%1 >= 100) {\n").arg(name);
            code += QString("        lastMeasure_%1 = millis();\n").arg(name);
            code += QString("        digitalWrite(PIN_%1_TRIG, LOW);\n").arg(name);
            code += QString("        delayMicroseconds(2);\n");
            code += QString("        digitalWrite(PIN_%1_TRIG, HIGH);\n").arg(name);
            code += QString("        delayMicroseconds(10);\n");
            code += QString("        digitalWrite(PIN_%1_TRIG, LOW);\n").arg(name);
            code += QString("        long duration = pulseIn(PIN_%1_ECHO, HIGH, 30000);\n").arg(name);
            code += QString("        if (duration > 0) {\n");
            code += QString("            float d = duration * 0.034 / 2.0;\n");
            code += QString("            distancia%1 = d;\n").arg(suffix);
            code += QString("            %1_eventAoMedir();\n").arg(name);
            code += QString("        }\n");
            code += QString("    }\n");
            code += "}\n\n";
        }
    }

    // ── EVENTOS WEB DASHBOARD ─────────────────────────────────────────
    if (webPageData.value("enabled").toBool()) {
        QJsonArray elements = webPageData["elements"].toArray();
        for (int i = 0; i < elements.size(); ++i) {
            QJsonObject el = elements[i].toObject();
            QString id = el["id"].toString();
            QString type = el["type"].toString();
            
            if (type == "Button") {
                QString eventKey = QString("%1:aoClicar").arg(id);
                if (eventBlockStorage.contains(eventKey) && !eventBlockStorage[eventKey].isEmpty()) {
                    QString eventBody = compileBlocks(eventBlockStorage[eventKey], components, 4, nullptr, &sanitized, &eepromOffsets, &nextEepromOffset);
                    if (!eventBody.trimmed().isEmpty()) {
                        code += QString("void %1_eventAoClicar() {\n").arg(id);
                        code += eventBody;
                        code += "}\n\n";
                    }
                }
            } else if (type == "Text") {
                QString eventKey = QString("%1:aoAtualizar").arg(id);
                if (eventBlockStorage.contains(eventKey) && !eventBlockStorage[eventKey].isEmpty()) {
                    // Create a hidden global string for this text
                    code += QString("String val_%1 = \"\";\n").arg(id);
                    QString eventBody = compileBlocks(eventBlockStorage[eventKey], components, 4, nullptr, &sanitized, &eepromOffsets, &nextEepromOffset);
                    if (!eventBody.trimmed().isEmpty()) {
                        code += QString("void %1_eventAoAtualizar() {\n").arg(id);
                        code += QString("    String Texto = val_%1;\n").arg(id);
                        code += eventBody;
                        code += QString("    val_%1 = Texto;\n").arg(id);
                        code += "}\n\n";
                    }
                }
            } else if (type == "Input") {
                QString eventKey = QString("%1:aoAlterar").arg(id);
                if (eventBlockStorage.contains(eventKey) && !eventBlockStorage[eventKey].isEmpty()) {
                    QString eventBody = compileBlocks(eventBlockStorage[eventKey], components, 4, nullptr, &sanitized, &eepromOffsets, &nextEepromOffset);
                    if (!eventBody.trimmed().isEmpty()) {
                        code += QString("void %1_eventAoAlterar(String Valor) {\n").arg(id);
                        code += eventBody;
                        code += "}\n\n";
                    }
                }
            }
        }
    }

    // ── EEPROM SETUP ───────────────────────────────────────────
    bool hasEeprom = false;
    for (auto it = eventBlockStorage.begin(); it != eventBlockStorage.end(); ++it) {
        for (const auto& b : it.value()) {
            if (b.type == LogicBlockType::EEPROM_OP) {
                hasEeprom = true;
                break;
            }
        }
        if (hasEeprom) break;
    }
    if (hasEeprom) {
        code = "#include <EEPROM.h>\n" + code;
    }

    // ── SETUP ───────────────────────────────────────────────────
    if (webPageData.value("enabled").toBool()) {
        code += "// ── WEB SERVER E DASHBOARD (FRUTIGER AERO) ─────────────\n";
        code += "WebServer server(80);\n";
        code += "String getHtmlPage() {\n";
        code += "  String html = \"<!DOCTYPE html><html><head><meta charset='UTF-8'>\";\n";
        code += "  html += \"<meta name='viewport' content='width=device-width, initial-scale=1.0'>\";\n";
        code += "  html += \"<style>\";\n";
        code += "  html += \"body { margin: 0; padding: 20px; font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; \";\n";
        code += "  html += \"background: linear-gradient(135deg, #e0f7fa 0%, #b2ebf2 100%); min-height: 100vh; }\";\n";
        code += "  html += \".container { position: relative; width: 100%; height: 80vh; background: rgba(255, 255, 255, 0.4); \";\n";
        code += "  html += \"border-radius: 20px; box-shadow: 0 8px 32px rgba(0,0,0,0.1); backdrop-filter: blur(10px); border: 1px solid rgba(255,255,255,0.8); overflow: hidden; }\";\n";
        code += "  html += \".elem { position: absolute; }\";\n";
        code += "  html += \"button.elem { background: linear-gradient(180deg, #4fc3f7 0%, #0288d1 100%); color: white; border: none; border-radius: 30px; \";\n";
        code += "  html += \"box-shadow: 0 4px 15px rgba(2,136,209,0.4), inset 0 2px 5px rgba(255,255,255,0.5); text-shadow: 0 1px 2px rgba(0,0,0,0.2); \";\n";
        code += "  html += \"cursor: pointer; font-weight: bold; font-size: 14px; transition: all 0.2s ease; }\";\n";
        code += "  html += \"button.elem:active { transform: translateY(2px); box-shadow: 0 2px 5px rgba(2,136,209,0.4); }\";\n";
        code += "  html += \"input.elem { background: rgba(255,255,255,0.7); border: 1px solid #81d4fa; border-radius: 10px; padding: 8px 15px; \";\n";
        code += "  html += \"box-shadow: inset 0 2px 4px rgba(0,0,0,0.05); outline: none; font-size: 14px; color: #01579b; }\";\n";
        code += "  html += \"input.elem:focus { border-color: #0288d1; background: rgba(255,255,255,0.9); }\";\n";
        code += "  html += \".text.elem { font-size: 16px; color: #01579b; font-weight: 600; text-shadow: 0 1px 1px rgba(255,255,255,0.8); }\";\n";
        code += "  html += \"</style></head><body>\";\n";
        code += "  html += \"<div class='container'>\";\n";
        
        QJsonArray elements = webPageData["elements"].toArray();
        for (int i = 0; i < elements.size(); ++i) {
            QJsonObject el = elements[i].toObject();
            QString type = el["type"].toString();
            QString id = el["id"].toString();
            int x = el["x"].toInt();
            int y = el["y"].toInt();
            QString text = el["text"].toString().toHtmlEscaped();
            QString var = el["boundVar"].toString();
            
            if (type == "Text") {
                int fs = el.contains("formatSize") ? el["formatSize"].toInt() : 16;
                QString fc = el.contains("formatColor") ? el["formatColor"].toString() : "#01579b";
                bool fb = el.contains("formatBold") ? el["formatBold"].toBool() : true;
                QString fw = fb ? "bold" : "normal";
                code += QString("  html += \"<div class='elem text' style='left:%1px; top:%2px; font-size:%3px; color:%4; font-weight:%5;' id='%6'>%7 <span id='val_%6'></span></div>\";\n")
                    .arg(x).arg(y).arg(fs).arg(fc).arg(fw).arg(id).arg(text);
            } else if (type == "Button") {
                code += QString("  html += \"<button class='elem' style='left:%1px; top:%2px; width:%3px; height:%4px;' onclick='sendEvent(\\\"%5\\\")'>%6</button>\";\n")
                    .arg(x).arg(y).arg(el["width"].toInt(100)).arg(el["height"].toInt(40)).arg(id).arg(text);
            } else if (type == "Input") {
                code += QString("  html += \"<input type='text' class='elem' style='left:%1px; top:%2px; width:%3px; height:%4px;' id='%5' value='' onchange='sendVar(\\\"%5\\\", this.value)'>\";\n")
                    .arg(x).arg(y).arg(el["width"].toInt(150)).arg(el["height"].toInt(30)).arg(id);
            } else if (type == "Chart") {
                code += QString("  html += \"<div class='elem' style='left:%1px; top:%2px; width:%3px; height:%4px; background:rgba(255,255,255,0.8); border-radius:10px; border:1px solid #4fc3f7; padding:10px; display:flex; align-items:center; justify-content:center; color:#0288d1; font-weight:bold;'>[Gráfico: %5]</div>\";\n")
                    .arg(x).arg(y).arg(el["width"].toInt(300)).arg(el["height"].toInt(200)).arg(var.isEmpty() ? text : var);
            }
        }
        
        code += "  html += \"</div>\";\n";
        code += "  html += \"<script>\";\n";
        code += "  html += \"function sendEvent(btn) { fetch('/event?btn=' + btn, {method: 'POST'}); }\";\n";
        code += "  html += \"function sendVar(varName, val) { fetch('/event?var=' + varName + '&val=' + val, {method: 'POST'}); }\";\n";
        code += "  html += \"setInterval(() => { fetch('/data').then(r=>r.json()).then(d => { \";\n";
        for (int i = 0; i < elements.size(); ++i) {
            QJsonObject el = elements[i].toObject();
            QString type = el["type"].toString();
            QString id = el["id"].toString();
            if (type == "Text") {
                code += QString("  html += \"if(d.%1 !== undefined) document.getElementById('val_%1').innerText = d.%1; \";\n").arg(id);
                code += QString("  html += \"if(d.%1_color !== undefined) document.getElementById('%1').style.color = d.%1_color; \";\n").arg(id);
                code += QString("  html += \"if(d.%1_size !== undefined) document.getElementById('%1').style.fontSize = d.%1_size; \";\n").arg(id);
                code += QString("  html += \"if(d.%1_weight !== undefined) document.getElementById('%1').style.fontWeight = d.%1_weight; \";\n").arg(id);
            } else if (type == "Chart" && !el["boundVar"].toString().isEmpty()) {
                // We'll leave the chart fetching logic empty or minimal for now
            }
        }
        code += "  html += \"}); }, 1000);\";\n";
        code += "  html += \"</script></body></html>\";\n";
        code += "  return html;\n";
        code += "}\n\n";
        
        code += "void handleRoot() {\n";
        code += "  server.send(200, \"text/html\", getHtmlPage());\n";
        code += "}\n\n";
        
        code += "void handleData() {\n";
        code += "  String json = \"{\";\n";
        bool firstVar = true;
        for (int i = 0; i < elements.size(); ++i) {
            QJsonObject el = elements[i].toObject();
            QString type = el["type"].toString();
            QString id = el["id"].toString();
            if (type == "Text") {
                QString boundVar = el["boundVar"].toString();
                if (!firstVar) code += "  json += \",\";\n";
                if (!boundVar.isEmpty()) {
                    code += QString("  json += \"\\\"%1\\\":\\\"\" + String(%2) + \"\\\"\";\n").arg(id).arg(boundVar);
                } else {
                    code += QString("  json += \"\\\"%1\\\":\\\"\" + String(%1) + \"\\\"\";\n").arg(id);
                }
                code += QString("  json += \",\\\"%1_color\\\":\\\"\" + _webFormat_%1_color + \"\\\",\";\n").arg(id);
                code += QString("  json += \"\\\"%1_size\\\":\\\"\" + _webFormat_%1_size + \"\\\",\";\n").arg(id);
                code += QString("  json += \"\\\"%1_weight\\\":\\\"\" + _webFormat_%1_weight + \"\\\"\";\n").arg(id);
                firstVar = false;
            } else if (type == "Chart") {
                QString var = el["boundVar"].toString();
                if (!var.isEmpty()) {
                    QStringList vars = var.split(",");
                    for (const QString& v : vars) {
                        QString trimmed = v.trimmed();
                        if (trimmed.isEmpty()) continue;
                        if (!firstVar) code += "  json += \",\";\n";
                        code += QString("  json += \"\\\"%1\\\":\\\"\" + String(%1) + \"\\\"\";\n").arg(trimmed);
                        firstVar = false;
                    }
                }
            }
        }
        code += "  json += \"}\";\n";
        code += "  server.send(200, \"application/json\", json);\n";
        code += "}\n\n";
        
        code += "void handleEvent() {\n";
        code += "  if (server.hasArg(\"btn\")) {\n";
        code += "    String btn = server.arg(\"btn\");\n";
        // Call Web Button events
        for (int i = 0; i < elements.size(); ++i) {
            QJsonObject el = elements[i].toObject();
            if (el["type"].toString() == "Button") {
                QString id = el["id"].toString();
                QString eventKey = QString("%1:aoClicar").arg(id);
                if (eventBlockStorage.contains(eventKey) && !eventBlockStorage[eventKey].isEmpty()) {
                    code += QString("    if (btn == \"%1\") { %1_eventAoClicar(); }\n").arg(id);
                }
            }
        }
        
        // Call physical button events
        for (auto* comp : components) {
            if (comp->componentType() == "button") {
                QString name = sanitizeIdentifier(comp->name());
                code += QString("    if (btn == \"%1\") { %1_eventAoClicar(); }\n").arg(name);
            }
        }
        code += "  }\n";
        code += "  if (server.hasArg(\"var\") && server.hasArg(\"val\")) {\n";
        code += "    String varName = server.arg(\"var\");\n";
        code += "    String val = server.arg(\"val\");\n";
        
        // Call Web Input events
        for (int i = 0; i < elements.size(); ++i) {
            QJsonObject el = elements[i].toObject();
            if (el["type"].toString() == "Input") {
                QString id = el["id"].toString();
                QString eventKey = QString("%1:aoAlterar").arg(id);
                if (eventBlockStorage.contains(eventKey) && !eventBlockStorage[eventKey].isEmpty()) {
                    code += QString("    if (varName == \"%1\") { %1_eventAoAlterar(val); }\n").arg(id);
                }
            }
        }
        code += "  }\n";
        code += "  server.send(200, \"text/plain\", \"OK\");\n";
        code += "}\n\n";
    }

    code += "void setup() {\n";
    code += "    Serial.begin(115200);\n";
    if (hasEeprom) {
        code += "    EEPROM.begin(512);\n";
        if (!g_eepromKeys.isEmpty()) {
            code += "    // Carregar registros da EEPROM para as variáveis\n";
            for (const auto& key : g_eepromKeys) {
                int offset = (qHash(key) % 120) * 4;
                QString type = g_eepromKeyTypes.value(key, "int");
                if (type == "String") {
                    code += QString("    %2 = EEPROM.readString(%1);\n").arg(offset).arg(key);
                } else {
                    code += QString("    EEPROM.get(%1, %2);\n").arg(offset).arg(key);
                }
            }
            code += "\n";
        }
    }
    code += "    Serial.println(\"IDE Embedded inicializada com sucesso!\");\n\n";

    for (auto* comp : components) {
        if (comp->componentType() == "esp32") continue;
        QString name = sanitizeIdentifier(comp->name());
        QString pinMacro = QString("PIN_%1").arg(name);

        if (comp->componentType() == "led") {
            code += QString("    pinMode(%1, OUTPUT);\n").arg(pinMacro);
            code += QString("    digitalWrite(%1, LOW); // Inicia desligado\n").arg(pinMacro);
        } else if (comp->componentType() == "button") {
            code += QString("    pinMode(%1, INPUT_PULLUP); // Resistor interno pullup\n").arg(pinMacro);
        } else if (comp->componentType() == "potentiometer" || comp->componentType() == "bess") {
            code += QString("    pinMode(%1, INPUT); // Entrada analógica\n").arg(pinMacro);
        } else if (comp->componentType() == "buzzer") {
            code += QString("    pinMode(%1, OUTPUT);\n").arg(pinMacro);
            code += QString("    digitalWrite(%1, LOW); // Inicia silenciado\n").arg(pinMacro);
        } else if (comp->componentType() == "motor") {
            auto* motor = static_cast<MotorItem*>(comp);
            int t = 0;
            if (motor->motorType() == "servo360") t = 1;
            else if (motor->motorType() == "dc") t = 2;
            code += QString("    %1.attach(%2, %3);\n").arg(name).arg(pinMacro).arg(t);
        } else if (comp->componentType() == "dht22") {
            code += QString("    dht%1.begin();\n").arg(name);
        } else if (comp->componentType() == "hcsr04") {
            code += QString("    pinMode(PIN_%1_TRIG, OUTPUT);\n").arg(name);
            code += QString("    pinMode(PIN_%1_ECHO, INPUT);\n").arg(name);
        } else if (auto* custom = dynamic_cast<CustomComponentItem*>(comp)) {
            QString category = custom->category();
            if (category == "digital_actuator") {
                code += QString("    pinMode(%1, OUTPUT);\n").arg(pinMacro);
                code += QString("    digitalWrite(%1, LOW); // Inicia desligado\n").arg(pinMacro);
            } else if (category == "digital_trigger") {
                code += QString("    pinMode(%1, INPUT_PULLUP); // Resistor interno pullup\n").arg(pinMacro);
            } else if (category == "analog_input") {
                code += QString("    pinMode(%1, INPUT); // Entrada analógica\n").arg(pinMacro);
            } else if (category == "active_actuator") {
                code += QString("    pinMode(%1, OUTPUT);\n").arg(pinMacro);
                code += QString("    digitalWrite(%1, LOW); // Inicia silenciado\n").arg(pinMacro);
            }

            // Custom setup initialization lines
            QString rawSetup = custom->definition().codeSetup.trimmed();
            if (!rawSetup.isEmpty()) {
                QString compiledSetup = replaceCustomComponentPlaceholders(rawSetup, custom);
                QStringList lines = compiledSetup.split('\n');
                for (const auto& line : lines) {
                    QString trimmedLine = line.trimmed();
                    if (!trimmedLine.isEmpty()) {
                        code += QString("    %1\n").arg(trimmedLine);
                    }
                }
            }
        }
    }
    
    // Add logic mapped directly to ESP32:aoIniciar (on boot)
    if (esp32) {
        QString startKey = QString("%1:aoIniciar").arg(esp32->id());
        if (eventBlockStorage.contains(startKey) && !eventBlockStorage[startKey].isEmpty()) {
            code += "\n    // Evento aoIniciar (ESP32)\n";
            code += compileBlocks(eventBlockStorage[startKey], components, 4, nullptr, &sanitized, &eepromOffsets, &nextEepromOffset);
        }
    }
    
    if (webPageData.value("enabled").toBool()) {
        code += "    // Inicializa Web Server do Dashboard\n";
        code += "    server.on(\"/\", handleRoot);\n";
        code += "    server.on(\"/data\", handleData);\n";
        code += "    server.on(\"/event\", HTTP_POST, handleEvent);\n";
        code += "    server.begin();\n";
    }
    
    code += "}\n\n";

    // 5. Loop function (handling Button click events and LED triggers)
    code += "void loop() {\n";
    if (webPageData.value("enabled").toBool()) {
        code += "    server.handleClient();\n";
        
        QJsonArray elements = webPageData["elements"].toArray();
        for (int i = 0; i < elements.size(); ++i) {
            QJsonObject el = elements[i].toObject();
            if (el["type"].toString() == "Text") {
                QString id = el["id"].toString();
                QString eventKey = QString("%1:aoAtualizar").arg(id);
                if (eventBlockStorage.contains(eventKey) && !eventBlockStorage[eventKey].isEmpty()) {
                    code += QString("    %1_eventAoAtualizar();\n").arg(id);
                }
            }
        }
    }

    // Evento de loop principal do ESP32
    if (esp32) {
        QString loopKey = QString("%1:aoLoop").arg(esp32->id());
        if (eventBlockStorage.contains(loopKey) && !eventBlockStorage[loopKey].isEmpty()) {
            code += "    // Evento aoLoop (ESP32)\n";
            code += compileBlocks(eventBlockStorage[loopKey], components, 4, nullptr, &sanitized, &eepromOffsets, &nextEepromOffset);
            code += "\n";
        }
    }

    // Loop Declarations & Custom Loop Orchestration for Custom Components
    for (auto* comp : components) {
        if (auto* custom = dynamic_cast<CustomComponentItem*>(comp)) {
            QString compName = sanitizeIdentifier(custom->name());
            CustomComponentDef def = custom->definition();

            // Loop Declarations updates
            if (!def.loopDeclarations.isEmpty()) {
                bool hasUpdates = false;
                for (const auto& decl : def.loopDeclarations) {
                    if (!decl.updateExpression.trimmed().isEmpty()) {
                        hasUpdates = true;
                        break;
                    }
                }

                if (hasUpdates) {
                    if (def.updateIntervalMs > 0) {
                        code += QString("    // Atualização periódica de: %1\n").arg(custom->name());
                        code += QString("    static unsigned long lastUpdate_%1 = 0;\n").arg(compName);
                        code += QString("    if (millis() - lastUpdate_%1 >= %2) {\n").arg(compName).arg(def.updateIntervalMs);
                        code += QString("        lastUpdate_%1 = millis();\n").arg(compName);
                        for (const auto& decl : def.loopDeclarations) {
                            if (!decl.updateExpression.trimmed().isEmpty()) {
                                QString updateLine = QString("        %1_%2 = %3;\n").arg(compName).arg(decl.name).arg(decl.updateExpression);
                                code += replaceCustomComponentPlaceholders(updateLine, custom);
                            }
                        }
                        code += "    }\n\n";
                    } else {
                        code += QString("    // Atualização contínua de: %1\n").arg(custom->name());
                        for (const auto& decl : def.loopDeclarations) {
                            if (!decl.updateExpression.trimmed().isEmpty()) {
                                QString updateLine = QString("    %1_%2 = %3;\n").arg(compName).arg(decl.name).arg(decl.updateExpression);
                                code += replaceCustomComponentPlaceholders(updateLine, custom);
                            }
                        }
                        code += "\n";
                    }
                }
            }

            // Legacy customLoop
            QString rawLoop = def.customLoop.trimmed();
            if (!rawLoop.isEmpty()) {
                code += QString("    // Legacy Loop de %1\n").arg(custom->name());
                code += "    " + replaceCustomComponentPlaceholders(rawLoop, custom) + "\n\n";
            }
        }
    }

    // Monitores customizados: chamar funções monitor_<COMP>_<callback>() quando existirem
    for (auto* comp : components) {
        if (auto* custom = dynamic_cast<CustomComponentItem*>(comp)) {
            for (const auto& ev : custom->definition().customEvents) {
                QString eventKey = QString("%1:%2").arg(custom->id()).arg(ev.callback);
                if (!ev.condition.trimmed().isEmpty() && eventBlockStorage.contains(eventKey) && !eventBlockStorage[eventKey].isEmpty()) {
                    QString compName = sanitizeIdentifier(custom->name());
                    QString call = QString("monitor_%1_%2()").arg(compName).arg(ev.callback);
                    if (!alreadyCalled.contains(call)) {
                        code += QString("    %1;\n").arg(call);
                    }
                }
            }
        }
    }

    // Monitores nativos executados fora do corpo do loop
    for (auto* comp : components) {
        bool isLed = (comp->componentType() == "led");
        if (auto* custom = dynamic_cast<CustomComponentItem*>(comp)) {
            if (custom->category() == "digital_actuator") isLed = true;
        }
        if (isLed) {
            QString clickKey = QString("%1:aoLigar").arg(comp->id());
            if (eventBlockStorage.contains(clickKey) && !eventBlockStorage[clickKey].isEmpty()) {
                QString name = sanitizeIdentifier(comp->name());
                QString call = QString("monitor_%1_eventAoLigar()").arg(name);
                if (!alreadyCalled.contains(call)) {
                    code += QString("    %1;\n").arg(call);
                }
            }
        }
    }

    // Monitor analógico (Ao Girar) para Potenciômetro e BESS
    for (auto* comp : components) {
        bool isPot = (comp->componentType() == "potentiometer" || comp->componentType() == "bess");
        if (auto* custom = dynamic_cast<CustomComponentItem*>(comp)) {
            if (custom->category() == "analog_input") isPot = true;
        }
        if (isPot) {
            QString clickKey = QString("%1:aoGirar").arg(comp->id());
            if (eventBlockStorage.contains(clickKey) && !eventBlockStorage[clickKey].isEmpty()) {
                QString name = sanitizeIdentifier(comp->name());
                QString call = QString("monitor_%1_eventAoGirar()").arg(name);
                if (!alreadyCalled.contains(call)) {
                    code += QString("    %1;\n").arg(call);
                }
            }
        }
    }

    // Monitor de estado (Ao Tocar) para Buzzer
    for (auto* comp : components) {
        bool isBuzzer = (comp->componentType() == "buzzer");
        if (auto* custom = dynamic_cast<CustomComponentItem*>(comp)) {
            if (custom->category() == "active_actuator") isBuzzer = true;
        }
        if (isBuzzer) {
            QString clickKey = QString("%1:aoTocar").arg(comp->id());
            if (eventBlockStorage.contains(clickKey) && !eventBlockStorage[clickKey].isEmpty()) {
                QString name = sanitizeIdentifier(comp->name());
                QString call = QString("monitor_%1_eventAoTocar()").arg(name);
                if (!alreadyCalled.contains(call)) {
                    code += QString("    %1;\n").arg(call);
                }
            }
        }
    }

    bool hasButtons = false;
    for (auto* comp : components) {
        bool isButton = (comp->componentType() == "button");
        if (auto* custom = dynamic_cast<CustomComponentItem*>(comp)) {
            if (custom->category() == "digital_trigger") isButton = true;
        }
        if (isButton) {
            hasButtons = true;
            QString name = sanitizeIdentifier(comp->name());
            QString clickKey = QString("%1:aoClicar").arg(comp->id());
            if (eventBlockStorage.contains(clickKey) && !eventBlockStorage[clickKey].isEmpty()) {
                QString call = QString("monitor_%1_eventAoClicar()").arg(name);
                if (!alreadyCalled.contains(call)) {
                    code += QString("    %1;\n").arg(call);
                }
            }
            QString pressKey = QString("%1:aoPressionar").arg(comp->id());
            if (eventBlockStorage.contains(pressKey) && !eventBlockStorage[pressKey].isEmpty()) {
                QString call = QString("monitor_%1_eventAoPressionar()").arg(name);
                if (!alreadyCalled.contains(call)) {
                    code += QString("    %1;\n").arg(call);
                }
            }
            QString releaseKey = QString("%1:aoSoltar").arg(comp->id());
            if (eventBlockStorage.contains(releaseKey) && !eventBlockStorage[releaseKey].isEmpty()) {
                QString call = QString("monitor_%1_eventAoSoltar()").arg(name);
                if (!alreadyCalled.contains(call)) {
                    code += QString("    %1;\n").arg(call);
                }
            }
        }
    }

    // Leitura e atualização periódica do Sensor DHT22 (Nativo)
    for (auto* comp : components) {
        if (comp->componentType() == "dht22") {
            QString name = sanitizeIdentifier(comp->name());
            QString call = QString("monitor_%1_eventAoCalcular()").arg(name);
            if (!alreadyCalled.contains(call)) {
                code += QString("    %1;\n").arg(call);
                alreadyCalled.append(call);
            }
        }
    }

    // Leitura e atualização periódica do Sensor HC-SR04 (Nativo)
    for (auto* comp : components) {
        if (comp->componentType() == "hcsr04") {
            QString name = sanitizeIdentifier(comp->name());
            QString call = QString("monitor_%1_eventAoMedir()").arg(name);
            if (!alreadyCalled.contains(call)) {
                code += QString("    %1;\n").arg(call);
                alreadyCalled.append(call);
            }
        }
    }

    if (!hasButtons) {
        code += "    // Dica: Adicione botões e conecte-os para criar eventos aoClicar!\n";
        code += "    delay(10);\n";
    }

    code += "}\n";

    return code;
}

QString CodeGenerator::compileComponentEvents(
    ComponentItem* comp,
    const QVector<ComponentItem*>& components,
    const QMap<QString, QVector<EventLogicBlock>>& eventBlockStorage
) {
    if (!comp) return QString();

    QHash<ComponentItem*, QString> sanitized;
    for (auto* c : components) {
        sanitized[c] = sanitizeIdentifier(c->name());
    }

    QString name = sanitized.value(comp, sanitizeIdentifier(comp->name()));
    QString type = comp->componentType();
    QString code;

    // EEPROM offset tracking for variable blocks
    QMap<QString, int> eepromOffsets;
    int nextEepromOffset = 0;

    if (type == "esp32" || type == "board") {
        code += "// ========================================== \n"
                "// FUNÇÕES DE EVENTOS DO MICROCONTROLADOR ESP32\n"
                "// ========================================== \n\n";

        // aoIniciar
        QString startKey = QString("%1:aoIniciar").arg(comp->id());
        if (eventBlockStorage.contains(startKey) && !eventBlockStorage[startKey].isEmpty()) {
            code += "// Função executada uma única vez na inicialização da placa (aoIniciar)\n";
            code += "void aoIniciar() {\n";
            code += compileBlocks(eventBlockStorage[startKey], components, 4, nullptr, &sanitized, &eepromOffsets, &nextEepromOffset);
            code += "}\n\n";
        } else {
            code += "// Função executada uma única vez na inicialização da placa (aoIniciar)\n"
                    "void aoIniciar() {\n"
                    "    // Insira sua inicialização física aqui\n"
                    "}\n\n";
        }

        // aoLoop
        QString loopKey = QString("%1:aoLoop").arg(comp->id());
        if (eventBlockStorage.contains(loopKey) && !eventBlockStorage[loopKey].isEmpty()) {
            code += "// Função executada repetidamente no loop principal cooperativo (aoLoop)\n";
            code += "void aoLoop() {\n";
            code += compileBlocks(eventBlockStorage[loopKey], components, 4, nullptr, &sanitized, &eepromOffsets, &nextEepromOffset);
            code += "}\n";
        } else {
            code += "// Função executada repetidamente no loop principal cooperativo (aoLoop)\n"
                    "void aoLoop() {\n"
                    "    // Insira seu processamento cíclico aqui\n"
                    "}\n";
        }

    } else if (type == "button") {
        code += "// ========================================== \n"
                "// FUNÇÕES DE EVENTOS DO BOTÃO PULSADOR (PULL-UP)\n"
                "// ========================================== \n\n";

        // aoPressionar
        QString pressKey = QString("%1:aoPressionar").arg(comp->id());
        bool hasPress = eventBlockStorage.contains(pressKey) && !eventBlockStorage[pressKey].isEmpty();
        if (hasPress) {
            code += "// Função executada quando o botão é pressionado (aoPressionar)\n";
            code += QString("void %1_eventAoPressionar() {\n").arg(name);
            code += compileBlocks(eventBlockStorage[pressKey], components, 4, nullptr, &sanitized, &eepromOffsets, &nextEepromOffset);
            code += "}\n\n";
        } else {
            code += "// Função executada quando o botão é pressionado (aoPressionar)\n"
                    "void aoPressionar() {\n"
                    "    // Botão fechou contato com o terra (LOW)\n"
                    "}\n\n";
        }

        // aoSoltar
        QString releaseKey = QString("%1:aoSoltar").arg(comp->id());
        bool hasRelease = eventBlockStorage.contains(releaseKey) && !eventBlockStorage[releaseKey].isEmpty();
        if (hasRelease) {
            code += "// Função executada quando o botão é solto/liberado (aoSoltar)\n";
            code += QString("void %1_eventAoSoltar() {\n").arg(name);
            code += compileBlocks(eventBlockStorage[releaseKey], components, 4, nullptr, &sanitized, &eepromOffsets, &nextEepromOffset);
            code += "}\n\n";
        } else {
            code += "// Função executada quando o botão é solto/liberado (aoSoltar)\n"
                    "void aoSoltar() {\n"
                    "    // Botão abriu contato e retornou para nível alto (HIGH)\n"
                    "}\n\n";
        }

        // aoClicar
        QString clickKey = QString("%1:aoClicar").arg(comp->id());
        bool hasClick = eventBlockStorage.contains(clickKey) && !eventBlockStorage[clickKey].isEmpty();
        if (hasClick) {
            code += "// Função executada após o ciclo completo de clique (aoClicar)\n";
            code += QString("void %1_eventAoClicar() {\n").arg(name);
            code += compileBlocks(eventBlockStorage[clickKey], components, 4, nullptr, &sanitized, &eepromOffsets, &nextEepromOffset);
            code += "}\n\n";
        } else {
            code += "// Função executada após o ciclo completo de clique (aoClicar)\n"
                    "void aoClicar() {\n"
                    "    // Ocorreu um pressionar estável seguido por soltar\n"
                    "}\n\n";
        }

        code += "// ========================================== \n"
                "// MONITORES DE EVENTOS DO HARDWARE (FÍSICO)\n"
                "// ========================================== \n\n";

        // Monitor aoPressionar
        code += QString(
            "// Monitor físico para aoPressionar (Debounce & Filtro)\n"
            "void monitor_%1_eventAoPressionar() {\n"
            "    int reading_%1 = digitalRead(PIN_%1);\n"
            "    if (reading_%1 != lastState_aoPressionar_%1) {\n"
            "        lastDebounce_aoPressionar_%1 = millis();\n"
            "    }\n"
            "    if ((millis() - lastDebounce_aoPressionar_%1) > 50) {\n"
            "        if (reading_%1 != state_aoPressionar_%1) {\n"
            "            state_aoPressionar_%1 = reading_%1;\n"
            "            if (state_aoPressionar_%1 == LOW) {\n"
            "                %2;\n"
            "            }\n"
            "        }\n"
            "    }\n"
            "    lastState_aoPressionar_%1 = reading_%1;\n"
            "}\n\n"
        ).arg(name).arg(hasPress ? name + "_eventAoPressionar()" : "aoPressionar()");

        // Monitor aoSoltar
        code += QString(
            "// Monitor físico para aoSoltar (Debounce & Filtro)\n"
            "void monitor_%1_eventAoSoltar() {\n"
            "    int reading_%1 = digitalRead(PIN_%1);\n"
            "    if (reading_%1 != lastState_aoSoltar_%1) {\n"
            "        lastDebounce_aoSoltar_%1 = millis();\n"
            "    }\n"
            "    if ((millis() - lastDebounce_aoSoltar_%1) > 50) {\n"
            "        if (reading_%1 != state_aoSoltar_%1) {\n"
            "            state_aoSoltar_%1 = reading_%1;\n"
            "            if (state_aoSoltar_%1 == HIGH) {\n"
            "                %2;\n"
            "            }\n"
            "        }\n"
            "    }\n"
            "    lastState_aoSoltar_%1 = reading_%1;\n"
            "}\n\n"
        ).arg(name).arg(hasRelease ? name + "_eventAoSoltar()" : "aoSoltar()");

        // Monitor aoClicar
        code += QString(
            "// Monitor físico para aoClicar (Debounce & Filtro)\n"
            "void monitor_%1_eventAoClicar() {\n"
            "    int reading_%1 = digitalRead(PIN_%1);\n"
            "    if (reading_%1 != lastState_%1) {\n"
            "        lastDebounce_%1 = millis();\n"
            "    }\n"
            "    if ((millis() - lastDebounce_%1) > 50) {\n"
            "        if (reading_%1 != state_%1) {\n"
            "            state_%1 = reading_%1;\n"
            "            if (state_%1 == LOW) {\n"
            "                %2;\n"
            "            }\n"
            "        }\n"
            "    }\n"
            "    lastState_%1 = reading_%1;\n"
            "}\n"
        ).arg(name).arg(hasClick ? name + "_eventAoClicar()" : "aoClicar()");

    } else if (type == "led") {
        code += "// ========================================== \n"
                "// FUNÇÕES DE EVENTOS DO DIODO EMISSOR DE LUZ (LED)\n"
                "// ========================================== \n\n";

        // aoLigar
        QString eventKey = QString("%1:aoLigar").arg(comp->id());
        bool hasEvent = eventBlockStorage.contains(eventKey) && !eventBlockStorage[eventKey].isEmpty();
        if (hasEvent) {
            code += "// Função executada quando o LED liga de forma estável (aoLigar)\n";
            code += QString("void %1_eventAoLigar() {\n").arg(name);
            code += compileBlocks(eventBlockStorage[eventKey], components, 4, nullptr, &sanitized, &eepromOffsets, &nextEepromOffset);
            code += "}\n\n";
        } else {
            code += "// Função executada quando o LED liga de forma estável (aoLigar)\n"
                    "void aoLigar() {\n"
                    "    // Disparado quando corrente > 2mA é detectada no ânodo\n"
                    "}\n\n";
        }

        code += "// ========================================== \n"
                "// MONITORES DE EVENTOS DO HARDWARE (FÍSICO)\n"
                "// ========================================== \n\n";

        code += QString(
            "// Monitor físico para detecção de acionamento do LED\n"
            "void monitor_%1_eventAoLigar() {\n"
            "    int currentState_%1 = digitalRead(PIN_%1);\n"
            "    if (currentState_%1 == HIGH && lastState_%1 == LOW) {\n"
            "        %2;\n"
            "    }\n"
            "    lastState_%1 = currentState_%1;\n"
            "}\n"
        ).arg(name).arg(hasEvent ? name + "_eventAoLigar()" : "aoLigar()");

    } else if (type == "potentiometer") {
        code += "// ========================================== \n"
                "// FUNÇÕES DE EVENTOS DO POTENCIÔMETRO (Entrada Analógica)\n"
                "// ========================================== \n\n";

        // aoGirar
        QString eventKey = QString("%1:aoGirar").arg(comp->id());
        bool hasEvent = eventBlockStorage.contains(eventKey) && !eventBlockStorage[eventKey].isEmpty();
        if (hasEvent) {
            code += "// Função executada continuamente quando o potenciômetro é girado (aoGirar)\n"
                    "// O parâmetro 'valor' contém a leitura analógica quantizada no ADC (0 a 4095)\n";
            code += QString("void %1_eventAoGirar(int valor) {\n").arg(name);
            code += compileBlocks(eventBlockStorage[eventKey], components, 4, nullptr, &sanitized, &eepromOffsets, &nextEepromOffset);
            code += "}\n\n";
        } else {
            code += "// Função executada continuamente quando o potenciômetro é girado (aoGirar)\n"
                    "// O parâmetro 'valor' contém a leitura analógica quantizada no ADC (0 a 4095)\n"
                    "void aoGirar(int valor) {\n"
                    "    // Converte a leitura (0-4095) para tensão física aproximada (0-3.3V)\n"
                    "}\n\n";
        }

        code += "// ========================================== \n"
                "// MONITORES DE EVENTOS DO HARDWARE (FÍSICO)\n"
                "// ========================================== \n\n";

        code += QString(
            "// Monitor físico para amostragem do potenciômetro com histerese\n"
            "void monitor_%1_eventAoGirar() {\n"
            "    int currentVal_%1 = analogRead(PIN_%1);\n"
            "    if (lastVal_%1 == -999 || abs(currentVal_%1 - lastVal_%1) > 5) {\n"
            "        %2(currentVal_%1);\n"
            "        lastVal_%1 = currentVal_%1;\n"
            "    }\n"
            "}\n"
        ).arg(name).arg(hasEvent ? name + "_eventAoGirar" : "aoGirar");

    } else if (type == "buzzer") {
        code += "// ========================================== \n"
                "// FUNÇÕES DE EVENTOS DO BUZZER (Saída de Áudio)\n"
                "// ========================================== \n\n";

        // aoTocar
        QString eventKey = QString("%1:aoTocar").arg(comp->id());
        bool hasEvent = eventBlockStorage.contains(eventKey) && !eventBlockStorage[eventKey].isEmpty();
        if (hasEvent) {
            code += "// Função executada quando o buzzer é ativado (aoTocar)\n";
            code += QString("void %1_eventAoTocar() {\n").arg(name);
            code += compileBlocks(eventBlockStorage[eventKey], components, 4, nullptr, &sanitized, &eepromOffsets, &nextEepromOffset);
            code += "}\n\n";
        } else {
            code += "// Função executada quando o buzzer é ativado (aoTocar)\n"
                    "void aoTocar() {\n"
                    "    // Executa uma melodia ou tom no buzzer\n"
                    "}\n\n";
        }

        code += "// ========================================== \n"
                "// MONITORES DE EVENTOS DO HARDWARE (FÍSICO)\n"
                "// ========================================== \n\n";

        code += QString(
            "// Monitor físico para detecção de modulação do Buzzer\n"
            "void monitor_%1_eventAoTocar() {\n"
            "    int currentState_%1 = digitalRead(PIN_%1);\n"
            "    if (currentState_%1 == HIGH && lastState_%1 == LOW) {\n"
            "        %2;\n"
            "    }\n"
            "    lastState_%1 = currentState_%1;\n"
            "}\n"
        ).arg(name).arg(hasEvent ? name + "_eventAoTocar()" : "aoTocar()");

    } else if (type == "dht22") {
        code += "// ========================================== \n"
                "// FUNÇÕES DE EVENTOS DO SENSOR DHT22\n"
                "// ========================================== \n\n";

        // aoCalcularUmidade
        {
            QString eventKey = QString("%1:aoCalcularUmidade").arg(comp->id());
            bool hasEvent = eventBlockStorage.contains(eventKey) && !eventBlockStorage[eventKey].isEmpty();
            code += "// Função executada quando a umidade é calculada (aoCalcularUmidade)\n";
            code += QString("void %1_eventAoCalcularUmidade() {\n").arg(name);
            if (hasEvent) {
                code += compileBlocks(eventBlockStorage[eventKey], components, 4, nullptr, &sanitized, &eepromOffsets, &nextEepromOffset);
            }
            code += "}\n\n";
        }

        // aoCalcularTemperatura
        {
            QString eventKey = QString("%1:aoCalcularTemperatura").arg(comp->id());
            bool hasEvent = eventBlockStorage.contains(eventKey) && !eventBlockStorage[eventKey].isEmpty();
            code += "// Função executada quando a temperatura é calculada (aoCalcularTemperatura)\n";
            code += QString("void %1_eventAoCalcularTemperatura() {\n").arg(name);
            if (hasEvent) {
                code += compileBlocks(eventBlockStorage[eventKey], components, 4, nullptr, &sanitized, &eepromOffsets, &nextEepromOffset);
            }
            code += "}\n\n";
        }

    } else if (type == "hcsr04") {
        code += "// ========================================== \n"
                "// FUNÇÕES DE EVENTOS DO SENSOR HC-SR04\n"
                "// ========================================== \n\n";

        // aoMedir
        {
            QString eventKey = QString("%1:aoMedir").arg(comp->id());
            bool hasEvent = eventBlockStorage.contains(eventKey) && !eventBlockStorage[eventKey].isEmpty();
            code += "// Função executada ao medir a distância (aoMedir)\n";
            code += QString("void %1_eventAoMedir() {\n").arg(name);
            if (hasEvent) {
                code += compileBlocks(eventBlockStorage[eventKey], components, 4, nullptr, &sanitized, &eepromOffsets, &nextEepromOffset);
            }
            code += "}\n\n";
        }

    } else if (type == "motor") {
        code += "// ========================================== \n"
                "// FUNÇÕES DE CONTROLE DE SERVO MOTOR (PWM)\n"
                "// ========================================== \n\n"
                "// ── DRIVER NATIVO DE MOTOR ESP32 ──\n"
                "struct IdeMotorDriver {\n"
                "    int _pin = -1;\n"
                "    int _channel = 0;\n"
                "    int _type = 0; // 0=servo, 1=servo360, 2=dc\n"
                "#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3\n"
                "    void attach(int pin, int type = 0, int channel = -1) {\n"
                "        _pin = pin;\n"
                "        _type = type;\n"
                "        ledcAttach(_pin, _type == 2 ? 1000 : 50, 16);\n"
                "    }\n"
                "    void write(int val) {\n"
                "        if (_type == 2) { // DC PWM\n"
                "            int duty = map(val, 0, 100, 0, 65535);\n"
                "            ledcWrite(_pin, duty);\n"
                "        } else {\n"
                "            int pulse_us = map(val, 0, 180, 500, 2500);\n"
                "            int duty = (pulse_us * 65535) / 20000;\n"
                "            ledcWrite(_pin, duty);\n"
                "        }\n"
                "    }\n"
                "    void stop() {\n"
                "        write(_type == 1 ? 90 : 0);\n"
                "    }\n"
                "#else\n"
                "    void attach(int pin, int type = 0, int channel = 2) {\n"
                "        _pin = pin;\n"
                "        _type = type;\n"
                "        _channel = channel;\n"
                "        ledcSetup(_channel, _type == 2 ? 1000 : 50, 16);\n"
                "        ledcAttachPin(_pin, _channel);\n"
                "    }\n"
                "    void write(int val) {\n"
                "        if (_type == 2) { // DC PWM\n"
                "            int duty = map(val, 0, 100, 0, 65535);\n"
                "            ledcWrite(_channel, duty);\n"
                "        } else {\n"
                "            int pulse_us = map(val, 0, 180, 500, 2500);\n"
                "            int duty = (pulse_us * 65535) / 20000;\n"
                "            ledcWrite(_channel, duty);\n"
                "        }\n"
                "    }\n"
                "    void stop() {\n"
                "        write(_type == 1 ? 90 : 0);\n"
                "    }\n"
                "#endif\n"
                "};\n\n"
                "// Instância do driver para o motor\n"
                "IdeMotorDriver " + name + ";\n\n"
                "// Função executada para rotacionar o braço do motor (exemplo de uso)\n"
                "void moverServo(int angulo) {\n"
                "    // Envia o ângulo (0 a 180) para o driver do servo motor\n"
                "    " + name + ".write(angulo);\n"
                "}\n";

    } else if (auto* custom = dynamic_cast<CustomComponentItem*>(comp)) {
        CustomComponentDef def = custom->definition();
        code += "// ========================================== \n"
                "// FUNÇÕES DE EVENTO DO COMPONENTE CUSTOMIZADO \n"
                "// ========================================== \n\n";

        if (!def.customEvents.isEmpty()) {
            for (const auto& ev : def.customEvents) {
                QString eventKey = QString("%1:%2").arg(custom->id()).arg(ev.callback);
                bool hasEvent = eventBlockStorage.contains(eventKey) && !eventBlockStorage[eventKey].isEmpty();
                if (hasEvent) {
                    code += QString("// Evento: %1\n"
                                    "// Gatilho: %2 (Debounce: %3ms)\n").arg(ev.name).arg(ev.triggerMode).arg(ev.debounceMs);
                    code += QString("void %1_%2() {\n").arg(name).arg(ev.callback);
                    code += compileBlocks(eventBlockStorage[eventKey], components, 4, custom, &sanitized, &eepromOffsets, &nextEepromOffset);
                    code += "}\n\n";
                } else {
                    code += QString("// Evento: %1\n"
                                    "// Gatilho: %2 (Debounce: %3ms)\n"
                                    "void %4() {\n"
                                    "    // Insira o código para tratar este evento aqui\n"
                                    "}\n\n")
                            .arg(ev.name).arg(ev.triggerMode).arg(ev.debounceMs).arg(ev.callback);
                }
            }

            code += "// ========================================== \n"
                    "// MONITORES DE EVENTOS DO HARDWARE (CUSTOM)\n"
                    "// ========================================== \n\n";

            for (const auto& ev : def.customEvents) {
                QString eventKey = QString("%1:%2").arg(custom->id()).arg(ev.callback);
                bool hasEvent = eventBlockStorage.contains(eventKey) && !eventBlockStorage[eventKey].isEmpty();
                if (!ev.condition.trimmed().isEmpty()) {
                    code += QString(
                        "// Monitor de evento customizado para %1\n"
                        "void monitor_%2_%3() {\n"
                        "    bool currentCond_%2_%3 = (%4);\n"
                        "    if (currentCond_%2_%3 && !lastCond_%2_%3) {\n"
                        "        %5;\n"
                        "    }\n"
                        "    lastCond_%2_%3 = currentCond_%2_%3;\n"
                        "}\n\n"
                    ).arg(ev.name).arg(name).arg(ev.callback).arg(replaceCustomComponentPlaceholders(ev.condition, custom))
                     .arg(hasEvent ? name + "_" + ev.callback + "()" : ev.callback + "()");
                }
            }

        } else {
            QString category = def.category;
            if (category == "digital_trigger") {
                // aoClicar
                QString clickKey = QString("%1:aoClicar").arg(custom->id());
                bool hasClick = eventBlockStorage.contains(clickKey) && !eventBlockStorage[clickKey].isEmpty();
                if (hasClick) {
                    code += "// Evento disparado no clique do componente\n";
                    code += QString("void %1_eventAoClicar() {\n").arg(name);
                    code += compileBlocks(eventBlockStorage[clickKey], components, 4, custom, &sanitized, &eepromOffsets, &nextEepromOffset);
                    code += "}\n\n";
                } else {
                    code += "// Evento disparado no clique do componente\n"
                            "void aoClicar() {\n"
                            "    // Insira seu código aqui\n"
                            "}\n\n";
                }

                // aoPressionar
                QString pressKey = QString("%1:aoPressionar").arg(custom->id());
                bool hasPress = eventBlockStorage.contains(pressKey) && !eventBlockStorage[pressKey].isEmpty();
                if (hasPress) {
                    code += "// Evento disparado quando pressionado\n";
                    code += QString("void %1_eventAoPressionar() {\n").arg(name);
                    code += compileBlocks(eventBlockStorage[pressKey], components, 4, custom, &sanitized, &eepromOffsets, &nextEepromOffset);
                    code += "}\n\n";
                }

                // aoSoltar
                QString releaseKey = QString("%1:aoSoltar").arg(custom->id());
                bool hasRelease = eventBlockStorage.contains(releaseKey) && !eventBlockStorage[releaseKey].isEmpty();
                if (hasRelease) {
                    code += "// Evento disparado quando solto\n";
                    code += QString("void %1_eventAoSoltar() {\n").arg(name);
                    code += compileBlocks(eventBlockStorage[releaseKey], components, 4, custom, &sanitized, &eepromOffsets, &nextEepromOffset);
                    code += "}\n\n";
                }

                code += "// ========================================== \n"
                        "// MONITORES DE EVENTOS DO HARDWARE (FÍSICO)\n"
                        "// ========================================== \n\n";

                // Monitor aoClicar
                code += QString(
                    "void monitor_%1_eventAoClicar() {\n"
                    "    int reading_%1 = digitalRead(PIN_%1);\n"
                    "    if (reading_%1 != lastState_%1) {\n"
                    "        lastDebounce_%1 = millis();\n"
                    "    }\n"
                    "    if ((millis() - lastDebounce_%1) > 50) {\n"
                    "        if (reading_%1 != state_%1) {\n"
                    "            state_%1 = reading_%1;\n"
                    "            if (state_%1 == LOW) {\n"
                    "                %2;\n"
                    "            }\n"
                    "        }\n"
                    "    }\n"
                    "    lastState_%1 = reading_%1;\n"
                    "}\n\n"
                ).arg(name).arg(hasClick ? name + "_eventAoClicar()" : "aoClicar()");

                // Monitor aoPressionar
                if (hasPress) {
                    code += QString(
                        "void monitor_%1_eventAoPressionar() {\n"
                        "    int reading_%1 = digitalRead(PIN_%1);\n"
                        "    if (reading_%1 != lastState_aoPressionar_%1) {\n"
                        "        lastDebounce_aoPressionar_%1 = millis();\n"
                        "    }\n"
                        "    if ((millis() - lastDebounce_aoPressionar_%1) > 50) {\n"
                        "        if (reading_%1 != state_aoPressionar_%1) {\n"
                        "            state_aoPressionar_%1 = reading_%1;\n"
                        "            if (state_aoPressionar_%1 == LOW) {\n"
                        "                %2_eventAoPressionar();\n"
                        "            }\n"
                        "        }\n"
                        "    }\n"
                        "    lastState_aoPressionar_%1 = reading_%1;\n"
                        "}\n\n"
                    ).arg(name).arg(name);
                }

                // Monitor aoSoltar
                if (hasRelease) {
                    code += QString(
                        "void monitor_%1_eventAoSoltar() {\n"
                        "    int reading_%1 = digitalRead(PIN_%1);\n"
                        "    if (reading_%1 != lastState_aoSoltar_%1) {\n"
                        "        lastDebounce_aoSoltar_%1 = millis();\n"
                        "    }\n"
                        "    if ((millis() - lastDebounce_aoSoltar_%1) > 50) {\n"
                        "        if (reading_%1 != state_aoSoltar_%1) {\n"
                        "            state_aoSoltar_%1 = reading_%1;\n"
                        "            if (state_aoSoltar_%1 == HIGH) {\n"
                        "                %2_eventAoSoltar();\n"
                        "            }\n"
                        "        }\n"
                        "    }\n"
                        "    lastState_aoSoltar_%1 = reading_%1;\n"
                        "}\n\n"
                    ).arg(name).arg(name);
                }

            } else if (category == "digital_actuator") {
                QString eventKey = QString("%1:aoLigar").arg(custom->id());
                bool hasEvent = eventBlockStorage.contains(eventKey) && !eventBlockStorage[eventKey].isEmpty();
                if (hasEvent) {
                    code += "// Evento disparado quando o atuador digital liga\n";
                    code += QString("void %1_eventAoLigar() {\n").arg(name);
                    code += compileBlocks(eventBlockStorage[eventKey], components, 4, custom, &sanitized, &eepromOffsets, &nextEepromOffset);
                    code += "}\n\n";
                } else {
                    code += "// Evento disparado quando o atuador digital liga\n"
                            "void aoLigar() {\n"
                            "    // Insira seu código aqui\n"
                            "}\n\n";
                }

                code += "// ========================================== \n"
                        "// MONITORES DE EVENTOS DO HARDWARE (FÍSICO)\n"
                        "// ========================================== \n\n";

                code += QString(
                    "void monitor_%1_eventAoLigar() {\n"
                    "    int currentState_%1 = digitalRead(PIN_%1);\n"
                    "    if (currentState_%1 == HIGH && lastState_%1 == LOW) {\n"
                    "        %2;\n"
                    "    }\n"
                    "    lastState_%1 = currentState_%1;\n"
                    "}\n"
                ).arg(name).arg(hasEvent ? name + "_eventAoLigar()" : "aoLigar()");

            } else if (category == "analog_input") {
                QString eventKey = QString("%1:aoGirar").arg(custom->id());
                bool hasEvent = eventBlockStorage.contains(eventKey) && !eventBlockStorage[eventKey].isEmpty();
                if (hasEvent) {
                    code += "// Evento disparado quando a leitura analógica muda\n";
                    code += QString("void %1_eventAoGirar(int valor) {\n").arg(name);
                    code += compileBlocks(eventBlockStorage[eventKey], components, 4, custom, &sanitized, &eepromOffsets, &nextEepromOffset);
                    code += "}\n\n";
                } else {
                    code += "// Evento disparado quando a leitura analógica muda\n"
                            "void aoGirar(int valor) {\n"
                            "    // Insira seu código aqui\n"
                            "}\n\n";
                }

                code += "// ========================================== \n"
                        "// MONITORES DE EVENTOS DO HARDWARE (FÍSICO)\n"
                        "// ========================================== \n\n";

                code += QString(
                    "void monitor_%1_eventAoGirar() {\n"
                    "    int currentVal_%1 = analogRead(PIN_%1);\n"
                    "    if (lastVal_%1 == -999 || abs(currentVal_%1 - lastVal_%1) > 5) {\n"
                    "        %2(currentVal_%1);\n"
                    "        lastVal_%1 = currentVal_%1;\n"
                    "    }\n"
                    "}\n"
                ).arg(name).arg(hasEvent ? name + "_eventAoGirar" : "aoGirar");

            } else if (category == "active_actuator") {
                QString eventKey = QString("%1:aoTocar").arg(custom->id());
                bool hasEvent = eventBlockStorage.contains(eventKey) && !eventBlockStorage[eventKey].isEmpty();
                if (hasEvent) {
                    code += "// Evento disparado quando o atuador ativo é acionado\n";
                    code += QString("void %1_eventAoTocar() {\n").arg(name);
                    code += compileBlocks(eventBlockStorage[eventKey], components, 4, custom, &sanitized, &eepromOffsets, &nextEepromOffset);
                    code += "}\n\n";
                } else {
                    code += "// Evento disparado quando o atuador ativo é acionado\n"
                            "void aoTocar() {\n"
                            "    // Insira seu código aqui\n"
                            "}\n\n";
                }

                code += "// ========================================== \n"
                        "// MONITORES DE EVENTOS DO HARDWARE (FÍSICO)\n"
                        "// ========================================== \n\n";

                code += QString(
                    "void monitor_%1_eventAoTocar() {\n"
                    "    int currentState_%1 = digitalRead(PIN_%1);\n"
                    "    if (currentState_%1 == HIGH && lastState_%1 == LOW) {\n"
                    "        %2;\n"
                    "    }\n"
                    "    lastState_%1 = currentState_%1;\n"
                    "}\n"
                ).arg(name).arg(hasEvent ? name + "_eventAoTocar()" : "aoTocar()");

            } else {
                code += "// Este componente não possui eventos cadastrados.\n"
                        "// Defina eventos personalizados na criação do componente.\n";
            }
        }

    } else {
        code += "// ========================================== \n"
                "// DETALHES DO COMPONENTE PASSIVO \n"
                "// ========================================== \n\n"
                "// Componentes passivos ou genéricos não executam códigos C++ de evento.\n"
                "// Eles modificam fisicamente as tensões e correntes na simulação.\n";
    }

    return code;
}

