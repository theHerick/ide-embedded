#include "CustomComponent.h"
#include <QPainter>
#include <QPainterPath>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsSceneWheelEvent>
#include <QStyleOptionGraphicsItem>
#include <QDebug>
#include <QRegularExpression>
#include <cmath>

QJsonObject CustomComponentDef::toJson() const {
    QJsonObject obj;
    obj["type"] = type;
    obj["name"] = name;
    obj["width"] = width;
    obj["height"] = height;
    obj["color"] = color;
    obj["shape"] = shape;
    obj["category"] = category;
    obj["labelText"] = labelText;
    
    // Legacy mapping for backward compatibility
    obj["minValue"] = minValue;
    obj["maxValue"] = maxValue;
    obj["valueUnit"] = valueUnit;
    
    obj["codeIncludes"] = codeIncludes;
    obj["codeGlobals"] = codeGlobals;
    obj["codeSetup"] = codeSetup;
    obj["setupBlocks"] = EventLogicBlock::serializeVector(setupBlocks);
    obj["codeReadExpression"] = codeReadExpression;

    // Advanced fields
    obj["operatingVoltage"] = operatingVoltage;
    obj["logicLevel"] = logicLevel;
    obj["currentConsumption"] = currentConsumption;
    obj["pullupRequired"] = pullupRequired;
    obj["pulldownRequired"] = pulldownRequired;
    obj["sensorType"] = sensorType;
    obj["actuatorType"] = actuatorType;
    obj["communicationType"] = communicationType;
    obj["technology"] = technology;
    obj["capabilities"] = QJsonArray::fromStringList(capabilities);
    obj["simDefaultValue"] = simDefaultValue;
    obj["simBehavior"] = simBehavior;
    obj["simWaveform"] = simWaveform;
    obj["handlesOwnPinSetup"] = handlesOwnPinSetup;
    obj["customLoop"] = customLoop;
    obj["loopBlocks"] = EventLogicBlock::serializeVector(loopBlocks);
    obj["readType"] = readType;
    obj["measurementMethod"] = measurementMethod;
    obj["protocol"] = protocol;
    obj["readCost"] = readCost;
    obj["cacheReadValue"] = cacheReadValue;
    obj["updatePolicy"] = updatePolicy;
    obj["warnings"] = QJsonArray::fromStringList(warnings);
    obj["incompatibilities"] = QJsonArray::fromStringList(incompatibilities);
    obj["requiredAdapters"] = QJsonArray::fromStringList(requiredAdapters);

    QJsonArray outputsArr;
    for (const auto& out : outputs) {
        QJsonObject outObj;
        outObj["name"] = out.name;
        outObj["type"] = out.type;
        outObj["unit"] = out.unit;
        outObj["min"] = out.min;
        outObj["max"] = out.max;
        outObj["expression"] = out.expression;
        outObj["blocks"] = EventLogicBlock::serializeVector(out.blocks);
        outObj["cacheable"] = out.cacheable;
        outputsArr.append(outObj);
    }
    obj["outputs"] = outputsArr;

    QJsonArray relationsArr;
    for (const auto& rel : pinRelations) {
        QJsonObject relObj;
        relObj["sourcePin"] = rel.sourcePin;
        relObj["targetPin"] = rel.targetPin;
        relObj["relationType"] = rel.relationType;
        relationsArr.append(relObj);
    }
    obj["pinRelations"] = relationsArr;

    obj["updateIntervalMs"] = updateIntervalMs;
    obj["requiresStateCache"] = requiresStateCache;

    QJsonArray pinsArr;
    for (const auto& pin : pins) {
        QJsonObject pinObj;
        pinObj["name"] = pin.name;
        pinObj["side"] = pin.side;
        pinObj["color"] = pin.color;
        pinObj["isOutput"] = pin.isOutput;
        pinObj["generateCode"] = pin.generateCode;
        pinObj["role"] = pin.role;
        pinObj["direction"] = pin.direction;
        pinObj["signalType"] = pin.signalType;
        pinObj["electricalType"] = pin.electricalType;
        pinsArr.append(pinObj);
    }
    obj["pins"] = pinsArr;

    QJsonArray eventsArr;
    for (const auto& ev : customEvents) {
        QJsonObject evObj;
        evObj["name"] = ev.name;
        evObj["callback"] = ev.callback;
        evObj["condition"] = ev.condition;
        evObj["conditionBlocks"] = EventLogicBlock::serializeVector(ev.conditionBlocks);
        evObj["debounceMs"] = ev.debounceMs;
        evObj["cooldownMs"] = ev.cooldownMs;
        evObj["stateful"] = ev.stateful;
        evObj["triggerMode"] = ev.triggerMode;
        evObj["eventData"] = ev.eventData;
        evObj["executionMode"] = ev.executionMode;
        eventsArr.append(evObj);
    }
    obj["customEvents"] = eventsArr;

    QJsonArray loopDeclsArr;
    for (const auto& decl : loopDeclarations) {
        QJsonObject declObj;
        declObj["type"] = decl.type;
        declObj["name"] = decl.name;
        declObj["initialValue"] = decl.initialValue;
        declObj["updateExpression"] = decl.updateExpression;
        loopDeclsArr.append(declObj);
    }
    obj["loopDeclarations"] = loopDeclsArr;

    QJsonArray customFuncsArr;
    for (const auto& func : customFunctions) {
        QJsonObject funcObj;
        funcObj["name"] = func.name;
        funcObj["code"] = func.code;
        funcObj["blocks"] = EventLogicBlock::serializeVector(func.blocks);
        customFuncsArr.append(funcObj);
    }
    obj["customFunctions"] = customFuncsArr;

    return obj;
}

CustomComponentDef CustomComponentDef::fromJson(const QJsonObject& obj) {
    CustomComponentDef def;
    def.type = obj["type"].toString();
    def.name = obj["name"].toString();
    def.width = obj["width"].toInt(80);
    def.height = obj["height"].toInt(80);
    def.color = obj["color"].toString("#3B82F6");
    def.shape = obj["shape"].toString("rectangle");
    def.category = obj["category"].toString("digital_actuator");
    def.labelText = obj["labelText"].toString();
    
    // Legacy
    def.minValue = obj["minValue"].toDouble(0.0);
    def.maxValue = obj["maxValue"].toDouble(100.0);
    def.valueUnit = obj["valueUnit"].toString("");
    
    def.codeIncludes = obj["codeIncludes"].toString("");
    def.codeGlobals = obj["codeGlobals"].toString("");
    def.codeSetup = obj["codeSetup"].toString("");
    def.setupBlocks = EventLogicBlock::deserializeArray(obj["setupBlocks"].toArray());
    def.codeReadExpression = obj["codeReadExpression"].toString("");

    // Advanced fields (with fallbacks for legacy JSONs)
    def.operatingVoltage = obj["operatingVoltage"].toString("3V3");
    def.logicLevel = obj["logicLevel"].toString("3V3");
    def.currentConsumption = obj["currentConsumption"].toDouble(0.0);
    def.pullupRequired = obj["pullupRequired"].toBool(false);
    def.pulldownRequired = obj["pulldownRequired"].toBool(false);
    def.sensorType = obj["sensorType"].toString("");
    def.actuatorType = obj["actuatorType"].toString("");
    def.communicationType = obj["communicationType"].toString("");
    def.technology = obj["technology"].toString("");
    
    QJsonArray caps = obj["capabilities"].toArray();
    for (int i=0; i<caps.size(); ++i) def.capabilities.append(caps[i].toString());
    
    def.simDefaultValue = obj["simDefaultValue"].toDouble(0.0);
    def.simBehavior = obj["simBehavior"].toString("static");
    def.simWaveform = obj["simWaveform"].toString("none");
    def.handlesOwnPinSetup = obj["handlesOwnPinSetup"].toBool(false);
    def.customLoop = obj["customLoop"].toString("");
    def.loopBlocks = EventLogicBlock::deserializeArray(obj["loopBlocks"].toArray());
    def.updateIntervalMs = obj["updateIntervalMs"].toInt(0);
    def.requiresStateCache = obj["requiresStateCache"].toBool(false);
    def.readType = obj["readType"].toString("polling");
    def.measurementMethod = obj["measurementMethod"].toString("adc");
    def.protocol = obj["protocol"].toString("none");
    def.readCost = obj["readCost"].toInt(1);
    def.cacheReadValue = obj["cacheReadValue"].toBool(false);
    def.updatePolicy = obj["updatePolicy"].toString("on_demand");
    
    QJsonArray warns = obj["warnings"].toArray();
    for (int i=0; i<warns.size(); ++i) def.warnings.append(warns[i].toString());
    QJsonArray incompats = obj["incompatibilities"].toArray();
    for (int i=0; i<incompats.size(); ++i) def.incompatibilities.append(incompats[i].toString());
    QJsonArray adapters = obj["requiredAdapters"].toArray();
    for (int i=0; i<adapters.size(); ++i) def.requiredAdapters.append(adapters[i].toString());

    if (obj.contains("pinRelations")) {
        QJsonArray rels = obj["pinRelations"].toArray();
        for (int i = 0; i < rels.size(); ++i) {
            QJsonObject rObj = rels[i].toObject();
            PinRelation rel;
            rel.sourcePin = rObj["sourcePin"].toString();
            rel.targetPin = rObj["targetPin"].toString();
            rel.relationType = rObj["relationType"].toString();
            def.pinRelations.append(rel);
        }
    }

    if (obj.contains("outputs")) {
        QJsonArray outArr = obj["outputs"].toArray();
        for (int i = 0; i < outArr.size(); ++i) {
            QJsonObject oObj = outArr[i].toObject();
            CustomComponentOutput out;
            out.name = oObj["name"].toString();
            out.type = oObj["type"].toString("int");
            out.unit = oObj["unit"].toString();
            out.min = oObj["min"].toDouble(0.0);
            out.max = oObj["max"].toDouble(100.0);
            out.expression = oObj["expression"].toString();
            out.blocks = EventLogicBlock::deserializeArray(oObj["blocks"].toArray());
            out.cacheable = oObj["cacheable"].toBool(true);
            def.outputs.append(out);
        }
    }

    QJsonArray pinsArr = obj["pins"].toArray();
    for (int i = 0; i < pinsArr.size(); ++i) {
        QJsonObject pinObj = pinsArr[i].toObject();
        CustomComponentPin pin;
        pin.name = pinObj["name"].toString();
        pin.side = pinObj["side"].toString();
        pin.color = pinObj["color"].toString("#4F46E5");
        pin.isOutput = pinObj["isOutput"].toBool(false);
        pin.generateCode = pinObj["generateCode"].toBool(true);
        pin.role = pinObj["role"].toString("signal");
        pin.direction = pinObj["direction"].toString("bidir");
        pin.signalType = pinObj["signalType"].toString("digital");
        pin.electricalType = pinObj["electricalType"].toString("generic");
        def.pins.append(pin);
    }

    QJsonArray eventsArr = obj["customEvents"].toArray();
    for (int i = 0; i < eventsArr.size(); ++i) {
        QJsonObject evObj = eventsArr[i].toObject();
        CustomEventDef ev;
        ev.name = evObj["name"].toString();
        ev.callback = evObj["callback"].toString();
        ev.condition = evObj["condition"].toString();
        ev.conditionBlocks = EventLogicBlock::deserializeArray(evObj["conditionBlocks"].toArray());
        ev.debounceMs = evObj["debounceMs"].toInt(0);
        ev.cooldownMs = evObj["cooldownMs"].toInt(0);
        ev.stateful = evObj["stateful"].toBool(false);
        ev.triggerMode = evObj["triggerMode"].toString("rising");
        ev.eventData = evObj["eventData"].toString("");
        ev.executionMode = evObj["executionMode"].toString("async");
        def.customEvents.append(ev);
    }

    if (obj.contains("loopDeclarations")) {
        QJsonArray loopArr = obj["loopDeclarations"].toArray();
        for (int i = 0; i < loopArr.size(); ++i) {
            QJsonObject lObj = loopArr[i].toObject();
            LoopDeclaration decl;
            decl.type = lObj["type"].toString("float");
            decl.name = lObj["name"].toString();
            decl.initialValue = lObj["initialValue"].toString("0");
            decl.updateExpression = lObj["updateExpression"].toString();
            def.loopDeclarations.append(decl);
        }
    }

    if (obj.contains("customFunctions")) {
        QJsonArray funcArr = obj["customFunctions"].toArray();
        for (int i = 0; i < funcArr.size(); ++i) {
            QJsonObject fObj = funcArr[i].toObject();
            CustomFunction func;
            func.name = fObj["name"].toString();
            func.code = fObj["code"].toString();
            func.blocks = EventLogicBlock::deserializeArray(fObj["blocks"].toArray());
            def.customFunctions.append(func);
        }
    }

    // Dynamic Bridge to populate outputs and customEvents for backward compatibility
    QStringList detected = def.detectEventSlots();
    for (const QString& evName : detected) {
        bool exists = false;
        for (const auto& existing : def.customEvents) {
            if (existing.name == evName) {
                exists = true;
                break;
            }
        }
        if (!exists) {
            CustomEventDef ev;
            ev.name = evName;
            ev.callback = "event" + evName;
            ev.condition = ""; // Empty condition as it's triggered from C++ function call
            ev.debounceMs = 0;
            ev.cooldownMs = 0;
            ev.stateful = false;
            ev.triggerMode = "change";
            ev.executionMode = "async";
            def.customEvents.append(ev);
        }
    }

    for (const auto& decl : def.loopDeclarations) {
        bool exists = false;
        for (const auto& existing : def.outputs) {
            if (existing.name == decl.name) {
                exists = true;
                break;
            }
        }
        if (!exists) {
            CustomComponentOutput out;
            out.name = decl.name;
            out.type = decl.type;
            out.expression = decl.name; // In the new system, expression evaluates to variable name
            out.min = 0.0;
            out.max = 100.0;
            out.unit = "";
            out.cacheable = true;
            def.outputs.append(out);
        }
    }

    return def;
}

CustomComponentItem::CustomComponentItem(const QString& id, const CustomComponentDef& def, QGraphicsItem* parent)
    : ComponentItem(id, def.name, def.type, parent), m_def(def) {
    m_value = def.minValue;
    distributePins();
}

QRectF CustomComponentItem::boundingRect() const {
    double W = m_def.width;
    double H = m_def.height;
    return QRectF(-W/2.0 - 8, -H/2.0 - 8, W + 16, H + 16);
}

void CustomComponentItem::distributePins() {
    m_pins.clear();

    QVector<int> leftIndices, rightIndices, topIndices, bottomIndices;
    for (int i = 0; i < m_def.pins.size(); ++i) {
        QString s = m_def.pins[i].side.trimmed().toLower();
        if (s == "left" || s == "esquerda") leftIndices.append(i);
        else if (s == "right" || s == "direita") rightIndices.append(i);
        else if (s == "top" || s == "topo") topIndices.append(i);
        else if (s == "bottom" || s == "fundo" || s == "baixo") bottomIndices.append(i);
        else leftIndices.append(i); // Default Left
    }

    double W = m_def.width;
    double H = m_def.height;
    
    // Standard pin pitch is 2.54mm. At 10px/mm, this is 25.4px.
    // To maintain 10px grid alignment for wires, we use exactly 25.4px in coordinates.
    const double pitch = 25.4;

    auto placePins = [&](QVector<int>& indices, const QString& side) {
        int n = indices.size();
        if (n == 0) return;

        double start = -((n - 1) / 2.0) * pitch;
        for (int k = 0; k < n; ++k) {
            int idx = indices[k];
            QPointF pos;
            if (side == "left")       pos = QPointF(-W/2.0, start + k*pitch);
            else if (side == "right")  pos = QPointF(W/2.0,  start + k*pitch);
            else if (side == "top")    pos = QPointF(start + k*pitch, -H/2.0);
            else if (side == "bottom") pos = QPointF(start + k*pitch,  H/2.0);
            
            m_pins.append({m_def.pins[idx].name, pos, m_def.pins[idx].isOutput, "", "", QColor(m_def.pins[idx].color), m_def.pins[idx].generateCode});
        }
    };

    placePins(leftIndices, "left");
    placePins(rightIndices, "right");
    placePins(topIndices, "top");
    placePins(bottomIndices, "bottom");
}

void CustomComponentItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget*) {
    painter->setRenderHint(QPainter::Antialiasing);

    double W = m_def.width;
    double H = m_def.height;

    // 1. Glow outline if selected
    if (option->state & QStyle::State_Selected) {
        painter->setPen(QPen(QColor(99, 102, 241, 180), 3, Qt::SolidLine));
        painter->setBrush(Qt::NoBrush);
        if (m_def.shape == "circle") {
            painter->drawEllipse(QRectF(-W/2.0 - 3, -H/2.0 - 3, W + 6, H + 6));
        } else if (m_def.shape == "capsule") {
            painter->drawRoundedRect(QRectF(-W/2.0 - 3, -H/2.0 - 3, W + 6, H + 6), qMin(W, H)/2.0, qMin(W, H)/2.0);
        } else {
            painter->drawRoundedRect(QRectF(-W/2.0 - 3, -H/2.0 - 3, W + 6, H + 6), 8, 8);
        }
    }

    // 2. Shadow
    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor(15, 23, 42, 60));
    if (m_def.shape == "circle") {
        painter->drawEllipse(QRectF(-W/2.0 + 3, -H/2.0 + 3, W, H));
    } else if (m_def.shape == "capsule") {
        painter->drawRoundedRect(QRectF(-W/2.0 + 3, -H/2.0 + 3, W, H), qMin(W, H)/2.0, qMin(W, H)/2.0);
    } else {
        painter->drawRoundedRect(QRectF(-W/2.0 + 3, -H/2.0 + 3, W, H), 8, 8);
    }

    // 3. Main body color with linear gradient (and glowing colors if active/pressed/on)
    QColor baseColor = QColor(m_def.color);
    bool glowing = m_isOn || m_isPressed || m_isActive;
    if (glowing) {
        baseColor = baseColor.lighter(120);
    }

    QLinearGradient bodyGrad(-W/2.0, -H/2.0, W/2.0, H/2.0);
    bodyGrad.setColorAt(0.0, baseColor.lighter(108));
    bodyGrad.setColorAt(1.0, baseColor.darker(115));
    painter->setBrush(bodyGrad);

    painter->setPen(QPen(baseColor.darker(130), 1.5));
    if (m_def.shape == "circle") {
        painter->drawEllipse(QRectF(-W/2.0, -H/2.0, W, H));
    } else if (m_def.shape == "capsule") {
        painter->drawRoundedRect(QRectF(-W/2.0, -H/2.0, W, H), qMin(W, H)/2.0, qMin(W, H)/2.0);
    } else {
        painter->drawRoundedRect(QRectF(-W/2.0, -H/2.0, W, H), 8, 8);
    }

    // 4. Glossy Bezel
    painter->setPen(QPen(QColor(255, 255, 255, 40), 1));
    painter->setBrush(Qt::NoBrush);
    if (m_def.shape == "circle") {
        painter->drawEllipse(QRectF(-W/2.0 + 2, -H/2.0 + 2, W - 4, H - 4));
    } else if (m_def.shape == "capsule") {
        painter->drawRoundedRect(QRectF(-W/2.0 + 2, -H/2.0 + 2, W - 4, H - 4), qMin(W, H)/2.0 - 2, qMin(W, H)/2.0 - 2);
    } else {
        painter->drawRoundedRect(QRectF(-W/2.0 + 2, -H/2.0 + 2, W - 4, H - 4), 6, 6);
    }

    // 5. Active vibrating soundwaves for active buzzers
    if (m_def.category == "active_actuator" && m_isActive) {
        painter->setPen(QPen(QColor(m_def.color).lighter(120), 1.5, Qt::DashLine));
        static int waveOffset = 0;
        waveOffset = (waveOffset + 1) % 15;
        painter->drawEllipse(QRectF(-W/2.0 - 5 - waveOffset, -H/2.0 - 5 - waveOffset, W + 10 + 2*waveOffset, H + 10 + 2*waveOffset));
        painter->drawEllipse(QRectF(-W/2.0 - 15 - waveOffset, -H/2.0 - 15 - waveOffset, W + 30 + 2*waveOffset, H + 30 + 2*waveOffset));
    }

    // 6. Draw central label text
    QString text = m_def.labelText.isEmpty() ? m_def.name : m_def.labelText;
    if (m_def.category == "analog_input") {
        QString unit = m_def.valueUnit;
        if (unit.isEmpty()) unit = "%";
        double range = m_def.maxValue - m_def.minValue;
        if (range <= 10.0 && range > 0.0) {
            text = QString("%1\n%2 %3").arg(text).arg(m_value, 0, 'f', 1).arg(unit);
        } else {
            text = QString("%1\n%2 %3").arg(text).arg(qRound(m_value)).arg(unit);
        }
    }

    painter->setPen(QColor(255, 255, 255));
    QFont font = painter->font();
    font.setPointSize(W > 70 ? 8 : 7);
    font.setBold(true);
    painter->setFont(font);
    painter->drawText(QRectF(-W/2.0 + 5, -H/2.0 + 5, W - 10, H - 10), Qt::AlignCenter, text);

    // 7. Draw pin pads (gold/colored circles)
    for (const auto& pin : m_pins) {
        painter->setBrush(pin.color);
        painter->setPen(QPen(pin.color.darker(130), 1));
        painter->drawEllipse(pin.localPos, 4, 4);

        painter->setPen(QColor(148, 163, 184));
        font.setPointSize(6);
        font.setBold(false);
        painter->setFont(font);

        QPointF labelPos = pin.localPos;
        int align = Qt::AlignCenter;
        if (pin.localPos.x() < -W/2.0 + 5) {
            labelPos.setX(pin.localPos.x() + 8);
            align = Qt::AlignLeft | Qt::AlignVCenter;
        } else if (pin.localPos.x() > W/2.0 - 5) {
            labelPos.setX(pin.localPos.x() - 40);
            align = Qt::AlignRight | Qt::AlignVCenter;
        } else if (pin.localPos.y() < -H/2.0 + 5) {
            labelPos.setY(pin.localPos.y() + 8);
            align = Qt::AlignHCenter | Qt::AlignTop;
        } else if (pin.localPos.y() > H/2.0 - 5) {
            labelPos.setY(pin.localPos.y() - 15);
            align = Qt::AlignHCenter | Qt::AlignBottom;
        }

        painter->drawText(QRectF(labelPos.x() - 15, labelPos.y() - 8, 30, 16), align, pin.name);
    }
}

void CustomComponentItem::mousePressEvent(QGraphicsSceneMouseEvent* event) {
    if (m_def.category == "digital_trigger") {
        setPressed(true);
    }
    ComponentItem::mousePressEvent(event);
}

void CustomComponentItem::mouseReleaseEvent(QGraphicsSceneMouseEvent* event) {
    if (m_def.category == "digital_trigger") {
        setPressed(false);
    }
    ComponentItem::mouseReleaseEvent(event);
}

void CustomComponentItem::wheelEvent(QGraphicsSceneWheelEvent* event) {
    if (m_def.category == "analog_input") {
        double range = m_def.maxValue - m_def.minValue;
        if (range <= 0.0) range = 100.0;
        double step = range / 50.0; // 50 steps from min to max
        double delta = event->delta() > 0 ? step : -step;
        double newVal = m_value + delta;
        setValue(qBound(m_def.minValue, newVal, m_def.maxValue));
        event->accept();
    } else {
        ComponentItem::wheelEvent(event);
    }
}

QStringList CustomComponentDef::detectEventSlots() const {
    QStringList events;
    // Match function calls/definitions in format e.g. eventAoAlgo()
    QRegularExpression regex("\\bevent([A-Za-z0-9_]+)\\b");

    QStringList codeSources;
    for (const auto& func : customFunctions) {
        codeSources.append(func.code);
    }
    codeSources.append(codeGlobals);
    codeSources.append(codeSetup);
    codeSources.append(customLoop);
    codeSources.append(codeReadExpression);
    for (const auto& out : outputs) {
        codeSources.append(out.expression);
    }

    for (const auto& source : codeSources) {
        if (source.trimmed().isEmpty()) continue;
        QRegularExpressionMatchIterator it = regex.globalMatch(source);
        while (it.hasNext()) {
            QRegularExpressionMatch match = it.next();
            QString eventName = match.captured(1);
            if (!eventName.isEmpty() && !events.contains(eventName)) {
                events.append(eventName);
            }
        }
    }
    return events;
}
