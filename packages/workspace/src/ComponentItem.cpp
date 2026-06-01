#include "ComponentItem.h"
#include "WorkspaceScene.h"
#include <QPainter>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsSceneWheelEvent>
#include <QGraphicsScene>
#include <QStyleOptionGraphicsItem>
#include <QMenu>
#include <QInputDialog>
#include <QMessageBox>
#include <cmath>
#include <QDialog>
#include <QVBoxLayout>
#include <QTableWidget>
#include <QHeaderView>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QHBoxLayout>
#include <QTableWidgetItem>
#include <QAbstractItemView>
#include <QListWidget>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <algorithm>

namespace {
QRectF esp32BodyRectForPins(const ComponentItem* item, const QVector<Pin>& pins) {
    double widthMm = 18.0; 
    double heightMm = 22.5; 
    const double mmToPx = 10.0;

    if (item) {
        QVariant configVar = item->property("microcontrollerConfig");
        if (configVar.isValid() && configVar.canConvert<QString>()) {
            QJsonDocument doc = QJsonDocument::fromJson(configVar.toString().toUtf8());
            if (doc.isObject()) {
                QJsonObject obj = doc.object();
                if (obj.contains("board_size") && obj["board_size"].isObject()) {
                    QJsonObject bs = obj["board_size"].toObject();
                    widthMm = bs.value("width_mm").toDouble(widthMm);
                    heightMm = bs.value("height_mm").toDouble(heightMm);
                }
            }
        }
    }

    qreal bodyWidth = widthMm * mmToPx;
    qreal bodyHeight = heightMm * mmToPx;

    QRectF rect(-bodyWidth * 0.5, -bodyHeight * 0.5, bodyWidth, bodyHeight);

    if (item) {
        QVariant configVar = item->property("microcontrollerConfig");
        if (configVar.isValid() && configVar.canConvert<QString>()) {
            if (configVar.toString().contains("esp32-c3-wroom-02")) {
                // EPAD is at (0,0). Top edge is -12.75mm, bottom edge is +7.25mm.
                rect = QRectF(-bodyWidth * 0.5, -127.5, bodyWidth, bodyHeight);
            }
        }
    }

    return rect;
}

QRectF esp32BoundsForPins(const ComponentItem* item, const QVector<Pin>& pins) {
    QRectF body = esp32BodyRectForPins(item, pins);
    return body.adjusted(-16, -12, 16, 12);
}

QString extractIndexFromId(const QString& id) {
    int sep = id.lastIndexOf('-');
    if (sep == -1) sep = id.lastIndexOf('_');
    if (sep != -1) {
        bool ok = false;
        int num = id.mid(sep + 1).toInt(&ok);
        if (ok) return QString::number(num);
    }
    return "";
}
}

ComponentItem::ComponentItem(const QString& id, const QString& name, const QString& type, QGraphicsItem* parent)
    : QGraphicsObject(parent), m_id(id), m_name(name), m_type(type) {
    setFlags(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsFocusable | QGraphicsItem::ItemSendsGeometryChanges);
    setAcceptHoverEvents(true);
}

void ComponentItem::setPins(QVector<Pin> pins) {
    prepareGeometryChange();
    m_pins = std::move(pins);
    update();
}

Pin* ComponentItem::findPinAt(const QPointF& scenePos, double threshold) {
    QPointF local = mapFromScene(scenePos);
    Pin* nearest = nullptr;
    double minDist = threshold + 1.0; // Start above threshold so first valid match wins
    for (auto& pin : m_pins) {
        double dx = pin.localPos.x() - local.x();
        double dy = pin.localPos.y() - local.y();
        double dist = std::sqrt(dx * dx + dy * dy);
        if (dist <= threshold && dist < minDist) {
            minDist = dist;
            nearest = &pin;
        }
    }
    return nearest;
}

QPointF ComponentItem::getPinScenePos(const Pin& pin) const {
    return mapToScene(pin.localPos);
}

static QString extractDigits(const QString& s) {
    QString res;
    res.reserve(s.size());
    for (QChar c : s) {
        if (c.isDigit()) {
            res.append(c);
        }
    }
    return res;
}

static bool isPowerOrGround(const QString& s) {
    QString ls = s.toLower();
    return ls.contains("gnd") || ls.contains("vcc") || ls.contains("5v") || ls.contains("3v3") || ls.contains("vin") || ls.contains("power");
}

Pin* ComponentItem::getPinByName(const QString& name) {
    // 1. Exact match
    for (auto& pin : m_pins) {
        if (pin.name == name) return &pin;
    }

    // 2. Normalized match (lowercase, trimmed, space removed)
    QString normTarget = name.trimmed().toLower().remove(' ');
    for (auto& pin : m_pins) {
        if (pin.name.trimmed().toLower().remove(' ') == normTarget) return &pin;
    }

    // 3. Match based on numeric equivalence (extract all digits), excluding power/ground pins
    QString targetDigits = extractDigits(normTarget);
    if (!targetDigits.isEmpty() && !isPowerOrGround(normTarget)) {
        for (auto& pin : m_pins) {
            if (isPowerOrGround(pin.name)) continue;
            QString pinDigits = extractDigits(pin.name);
            if (pinDigits == targetDigits) {
                return &pin;
            }
        }
    }

    return nullptr;
}

bool ComponentItem::isSimulating() const {
    auto* ws = dynamic_cast<WorkspaceScene*>(scene());
    return ws ? ws->isSimulating() : false;
}

void ComponentItem::mousePressEvent(QGraphicsSceneMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        if (isSimulating()) {
            // Block dragging but allow event to be accepted by overrides (buttons/pots)
            event->ignore(); 
            return;
        }
        m_dragging = true;
        setSelected(true);
        event->accept();
    } else {
        QGraphicsObject::mousePressEvent(event);
    }
}

void ComponentItem::mouseMoveEvent(QGraphicsSceneMouseEvent* event) {
    if (isSimulating()) {
        event->ignore();
        return;
    }
    if (m_dragging) {
        QPointF newPos = event->scenePos() - (event->buttonDownPos(Qt::LeftButton));
        // Magnetic snap-to-grid: 10px grid (matches background dots)
        double snappedX = std::round(newPos.x() / 10.0) * 10.0;
        double snappedY = std::round(newPos.y() / 10.0) * 10.0;
        QPointF candidatePos(snappedX, snappedY);

        QPointF originalPos = pos();
        setPos(candidatePos);

        // Verify if candidate position results in overlap with other components
        bool collides = false;
        if (scene()) {
            QRectF mySceneRect = mapToScene(boundingRect()).boundingRect();
            // Shrink check boundary slightly to allow touching edge placement without false alarms
            QRectF myShrunkRect = mySceneRect.adjusted(3, 3, -3, -3);

            QList<QGraphicsItem*> items = scene()->items();
            for (auto* item : items) {
                if (item == this || item->parentItem() == this) continue;
                auto* comp = dynamic_cast<ComponentItem*>(item);
                if (comp) {
                    QRectF otherSceneRect = comp->mapToScene(comp->boundingRect()).boundingRect();
                    QRectF otherShrunkRect = otherSceneRect.adjusted(3, 3, -3, -3);
                    if (myShrunkRect.intersects(otherShrunkRect)) {
                        collides = true;
                        break;
                    }
                }
            }
        }

        if (collides) {
            // Restore previous non-colliding position
            setPos(originalPos);
        } else {
            emit componentMoved();
        }
        event->accept();
    } else {
        QGraphicsObject::mouseMoveEvent(event);
    }
}

void ComponentItem::mouseReleaseEvent(QGraphicsSceneMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        m_dragging = false;
        event->accept();
    } else {
        QGraphicsObject::mouseReleaseEvent(event);
    }
}

void ComponentItem::mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        emit doubleClicked(event->screenPos());
        event->accept();
    } else {
        QGraphicsObject::mouseDoubleClickEvent(event);
    }
}

void ComponentItem::contextMenuEvent(QGraphicsSceneContextMenuEvent* event) {
    if (isSimulating()) {
        event->accept();
        return;
    }
    // Delegate all right-clicks (including on pins) to the scene/main window
    // so only the component-level menu is shown (events + Edit Microcontrolador).
    emit rightClicked(event->screenPos());
    event->accept();
}

void ComponentItem::openPinEditor() {
    QDialog dialog;
    dialog.setWindowTitle(QString("Editor de Pinos - %1").arg(m_name));
    dialog.resize(520, 420);
    dialog.setStyleSheet(
        "QDialog { background: #FFFFFF; border: 1px solid #E2E8F0; border-radius: 12px; }"
        "QLabel { color: #334155; font-family: 'Segoe UI', Arial, sans-serif; font-size: 11px; font-weight: 600; }"
        "QDoubleSpinBox { background: #FFFFFF; border: 1px solid #CBD5E1; border-radius: 6px; color: #0F172A; padding: 4px 8px; font-size: 12px; }"
        "QDoubleSpinBox:focus { border-color: #3B82F6; }"
        "QListWidget { background: #FFFFFF; border: 1px solid #CBD5E1; border-radius: 6px; color: #0F172A; font-size: 12px; }"
        "QListWidget::item { padding: 4px 8px; }"
        "QListWidget::item:selected { background: #DBEAFE; color: #1E40AF; }"
        "QPushButton { background: #2563EB; border: none; border-radius: 6px; color: #FFFFFF; padding: 6px 12px; font-weight: 600; font-size: 12px; }"
        "QPushButton:hover { background: #1D4ED8; }"
        "QPushButton:pressed { background: #1E40AF; }"
    );

    auto* layout = new QVBoxLayout(&dialog);

    // Spacing input (mm) - default 2.54 mm
    auto* spacingLayout = new QHBoxLayout();
    spacingLayout->addWidget(new QLabel("Espaçamento entre pinos (mm):"));
    auto* spacingSpin = new QDoubleSpinBox();
    spacingSpin->setRange(0.1, 50.0);
    spacingSpin->setDecimals(2);
    spacingSpin->setValue(2.54);
    spacingLayout->addWidget(spacingSpin);
    spacingLayout->addStretch();
    layout->addLayout(spacingLayout);

    // Create a prettier layout: left pins list, central board preview, right pins list
    auto* hArea = new QHBoxLayout();

    auto* leftList = new QListWidget(&dialog);
    leftList->setMinimumWidth(140);
    leftList->setSelectionMode(QAbstractItemView::SingleSelection);

    QFrame* boardFrame = new QFrame(&dialog);
    boardFrame->setFixedSize(220, 260);
    boardFrame->setFrameShape(QFrame::Box);
    boardFrame->setStyleSheet("QFrame { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #F7FBFF, stop:1 #E2EEF8); border: 2px solid #BFD3EA; border-radius: 8px; }");

    auto* rightList = new QListWidget(&dialog);
    rightList->setMinimumWidth(140);
    rightList->setSelectionMode(QAbstractItemView::SingleSelection);

    hArea->addWidget(leftList);
    hArea->addWidget(boardFrame, 0, Qt::AlignCenter);
    hArea->addWidget(rightList);
    layout->addLayout(hArea);

    // Populate left/right lists from pins sorted by Y
    QVector<Pin> leftPins, rightPins;
    for (const auto& p : m_pins) {
        if (p.localPos.x() < 0) leftPins.append(p);
        else if (p.localPos.x() > 0) rightPins.append(p);
        else {
            // y-based decide left/right by side proximity
            if (p.localPos.y() < 0) leftPins.append(p); else rightPins.append(p);
        }
    }
    auto sortByY = [](const Pin&a, const Pin&b){ return a.localPos.y() < b.localPos.y(); };
    std::sort(leftPins.begin(), leftPins.end(), sortByY);
    std::sort(rightPins.begin(), rightPins.end(), sortByY);
    for (const auto& p : leftPins) leftList->addItem(p.name + QString(" (%1,%2)").arg(int(p.localPos.x())).arg(int(p.localPos.y())));
    for (const auto& p : rightPins) rightList->addItem(p.name + QString(" (%1,%2)").arg(int(p.localPos.x())).arg(int(p.localPos.y())));

    // Buttons under lists
    auto* btnLayout = new QHBoxLayout();
    auto* addBtn = new QPushButton("Adicionar");
    auto* removeBtn = new QPushButton("Remover");
    auto* upBtn = new QPushButton("↑");
    auto* downBtn = new QPushButton("↓");
    btnLayout->addWidget(addBtn);
    btnLayout->addWidget(removeBtn);
    btnLayout->addWidget(upBtn);
    btnLayout->addWidget(downBtn);
    btnLayout->addStretch();
    layout->addLayout(btnLayout);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(buttons);

    // Handlers
    connect(addBtn, &QPushButton::clicked, [&]() {
        bool ok;
        QString pinName = QInputDialog::getText(nullptr, "Adicionar Pino", "Nome do pino:", QLineEdit::Normal, "D0", &ok);
        if (!ok || pinName.isEmpty()) return;
        // default to right side
        Pin newPin; newPin.name = pinName; newPin.localPos = QPointF(40, 0); newPin.isOutput = false; newPin.generateCode = true; newPin.color = QColor(59,130,246);
        m_pins.append(newPin);
        rightList->addItem(newPin.name + QString(" (%1,%2)").arg(int(newPin.localPos.x())).arg(int(newPin.localPos.y())));
    });

    connect(removeBtn, &QPushButton::clicked, [&]() {
        if (leftList->currentRow() >= 0) {
            int r = leftList->currentRow();
            // find and remove from m_pins by name+pos
            leftList->takeItem(r);
        } else if (rightList->currentRow() >= 0) {
            int r = rightList->currentRow();
            rightList->takeItem(r);
        }
    });

    connect(upBtn, &QPushButton::clicked, [&]() {
        // move selected up in either list
        if (leftList->currentRow() > 0) { int r = leftList->currentRow(); auto it = leftList->takeItem(r); leftList->insertItem(r-1, it); leftList->setCurrentRow(r-1); }
        else if (rightList->currentRow() > 0) { int r = rightList->currentRow(); auto it = rightList->takeItem(r); rightList->insertItem(r-1, it); rightList->setCurrentRow(r-1); }
    });

    connect(downBtn, &QPushButton::clicked, [&]() {
        if (leftList->currentRow() >= 0 && leftList->currentRow() < leftList->count()-1) { int r = leftList->currentRow(); auto it = leftList->takeItem(r); leftList->insertItem(r+1, it); leftList->setCurrentRow(r+1); }
        else if (rightList->currentRow() >= 0 && rightList->currentRow() < rightList->count()-1) { int r = rightList->currentRow(); auto it = rightList->takeItem(r); rightList->insertItem(r+1, it); rightList->setCurrentRow(r+1); }
    });

    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() == QDialog::Accepted) {
        // Read back lists into m_pins preserving left/right order
        QVector<Pin> newPins;
        for (int i=0;i<leftList->count();++i) {
            auto it = leftPins.value(i);
            // Parse name and coords from display text if user reordered
            QString text = leftList->item(i)->text();
            QString name = text.split(" ").first();
            Pin p = it; p.name = name; newPins.append(p);
        }
        for (int i=0;i<rightList->count();++i) {
            auto it = rightPins.value(i);
            QString text = rightList->item(i)->text();
            QString name = text.split(" ").first();
            Pin p = it; p.name = name; newPins.append(p);
        }

        // Apply spacing using spacingSpin
        double spacing_mm = spacingSpin->value();
        const double MM_TO_PX = 10.0; 
        double spacing_px = std::round((spacing_mm * MM_TO_PX) / 10.0) * 10.0;
        if (spacing_px < 10.0) spacing_px = 10.0;

        auto applySpacingForSide = [&](double sideX){
            QVector<int> idx;
            for (int i=0;i<newPins.size();++i) if ((sideX<0 && newPins[i].localPos.x()<0) || (sideX>0 && newPins[i].localPos.x()>0)) idx.append(i);
            if (idx.isEmpty()) return;
            int n = idx.size();
            double start = -((n-1)/2.0) * spacing_px;
            start = std::round(start / 10.0) * 10.0; // Snap to 10px grid
            for (int i=0;i<n;++i) {
                newPins[idx[i]].localPos.setX(std::round(sideX / 10.0) * 10.0);
                newPins[idx[i]].localPos.setY(start + i*spacing_px);
            }
        };
        applySpacingForSide(-30);
        applySpacingForSide(30);

        setPins(std::move(newPins));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// ESP32 ITEM IMPLEMENTATION
// ─────────────────────────────────────────────────────────────────────────────
void ComponentItem::applyMicrocontrollerConfig(const QJsonObject& cfg) {
    Q_UNUSED(cfg);
}

void ESP32Item::applyMicrocontrollerConfig(const QJsonObject& cfgObj) {
    prepareGeometryChange();
    QJsonArray pinsArray = cfgObj.value("pins").toArray();
    double pitchMm = cfgObj.value("pin_pitch_mm").toDouble(2.54);
    const double mmToPx = 10.0;
    double spacingPx = pitchMm * mmToPx;

    double widthMm = 18.0; 
    double heightMm = 22.52; 
    double pinWidthMm = 15.24;
    double pinHeightMm = 15.24;
    if (cfgObj.contains("board_size") && cfgObj["board_size"].isObject()) {
        QJsonObject bs = cfgObj["board_size"].toObject();
        widthMm = bs.value("width_mm").toDouble(widthMm);
        heightMm = bs.value("height_mm").toDouble(heightMm);
        pinWidthMm = bs.value("pin_width_mm").toDouble(widthMm);
        pinHeightMm = bs.value("pin_height_mm").toDouble(heightMm);
    }
    // Standard pin pitch is 2.54mm (0.1"). At 10px/mm, this is exactly 25.4px.
    // However, to maintain alignment with the IDE's 10px grid, we MUST use 2.54mm accurately
    // in the PCB exporter, while in the workspace we snap to multiples of 10px (1mm) for usability.
    // For the PCB image to be perfect, the 'actualSpacing' MUST be exactly pitchMm * mmToPx.
    QString boardId = cfgObj.value("board").toString().toLower();
    bool bypassSnap = (boardId == "esp32-s3-devkitc-1" || boardId == "esp32-c3-devkitm-1" || boardId == "esp32-c3-wroom-02");

    double sideX = (pinWidthMm * mmToPx) * 0.5;
    double sideY = (pinHeightMm * mmToPx) * 0.5;

    // Snap to 10px grid for pin alignment
    if (!bypassSnap) {
        sideX = std::round(sideX / 10.0) * 10.0;
        sideY = std::round(sideY / 10.0) * 10.0;
    }
    double actualSpacing = spacingPx;
    if (!bypassSnap) {
        actualSpacing = std::round(spacingPx / 10.0) * 10.0;
        if (actualSpacing < 10.0) actualSpacing = 10.0;
    }

    struct Entry {
        Pin pin;
        int order = 0;
        QString side;
    };

    QVector<Entry> leftEntries;
    QVector<Entry> rightEntries;
    QVector<Entry> topEntries;
    QVector<Entry> bottomEntries;

    auto makeColor = [](const QString& name, const QString& role) {
        const QString upperRole = role.toLower();
        if (upperRole.contains("power") || name == "3V3" || name == "5V" || name == "VIN") return QColor(220, 38, 38);
        if (upperRole.contains("ground") || name.contains("GND")) return QColor(75, 85, 99);
        if (upperRole.contains("control") || name == "EN" || name == "RST" || name == "CHIP_EN" || name.contains("XTAL")) return QColor(234, 179, 8);
        return QColor(59, 130, 246);
    };

    for (int i = 0; i < pinsArray.size(); ++i) {
        if (!pinsArray.at(i).isObject()) continue;
        QJsonObject pj = pinsArray.at(i).toObject();
        Pin pin;
        pin.name = pj.value("name").toString();
        pin.isOutput = false;
        pin.generateCode = true;
        pin.color = makeColor(pin.name, pj.value("role").toString());

        Entry entry;
        entry.pin = pin;
        entry.order = pj.value("position").toInt(i);
        entry.side = pj.value("side").toString().toLower();
        if (entry.side.isEmpty()) entry.side = (i < pinsArray.size() / 2) ? "left" : "right";

        if (entry.side == "left") leftEntries.append(entry);
        else if (entry.side == "right") rightEntries.append(entry);
        else if (entry.side == "top") topEntries.append(entry);
        else if (entry.side == "bottom") bottomEntries.append(entry);
        else rightEntries.append(entry);
    }

    auto sortByOrder = [](const Entry& a, const Entry& b) { return a.order < b.order; };
    std::sort(leftEntries.begin(), leftEntries.end(), sortByOrder);
    std::sort(rightEntries.begin(), rightEntries.end(), sortByOrder);
    std::sort(topEntries.begin(), topEntries.end(), sortByOrder);
    std::sort(bottomEntries.begin(), bottomEntries.end(), sortByOrder);

    auto placeSide = [&](QVector<Entry>& entries, double sX) {
        if (entries.isEmpty()) return;
        double startY = -((entries.size() - 1) / 2.0) * actualSpacing;
        QVariant cfg = property("microcontrollerConfig");
        bool isESP12E = cfg.isValid() && cfg.toString().contains("esp-12e");
        if (!bypassSnap) {
            startY = std::round(startY / 10.0) * 10.0; // Snap to 10px grid
        }
        for (int i = 0; i < entries.size(); ++i) {
            entries[i].pin.localPos = QPointF(sX, startY + i * actualSpacing);
        }
    };

    auto placeTopBottom = [&](QVector<Entry>& entries, double sY) {
        if (entries.isEmpty()) return;
        double startX = -((entries.size() - 1) / 2.0) * actualSpacing;
        if (!bypassSnap) {
            startX = std::round(startX / 10.0) * 10.0; // Snap to 10px grid
        }
        for (int i = 0; i < entries.size(); ++i) {
            entries[i].pin.localPos = QPointF(startX + i * actualSpacing, sY);
        }
    };

    placeSide(leftEntries, -sideX);
    placeSide(rightEntries, sideX);
    placeTopBottom(topEntries, -sideY);
    placeTopBottom(bottomEntries, sideY);

    if (boardId == "esp32-c3-wroom-02") {
        int epadIdx = 0;
        for (auto& entry : bottomEntries) {
            if (entry.pin.name.startsWith("GND.EPAD")) {
                int r = epadIdx / 3 - 1; // -1, 0, 1
                int c = epadIdx % 3 - 1; // -1, 0, 1
                entry.pin.localPos = QPointF(c * 7.0, r * 7.0);
                epadIdx++;
            }
        }
    }

    QVector<Pin> updatedPins;
    updatedPins.reserve(leftEntries.size() + rightEntries.size() + topEntries.size() + bottomEntries.size());
    for (const auto& entry : leftEntries) updatedPins.append(entry.pin);
    for (const auto& entry : rightEntries) updatedPins.append(entry.pin);
    for (const auto& entry : topEntries) updatedPins.append(entry.pin);
    for (const auto& entry : bottomEntries) updatedPins.append(entry.pin);
    setPins(std::move(updatedPins));
}

ESP32Item::ESP32Item(const QString& id, const QString& name, QGraphicsItem* parent)
    : ComponentItem(id, name, "esp32", parent) {

    // Pre-initialize default microcontroller configuration (ESP32-C3 Mini is now the default)
    QJsonObject defaultCfg;
    defaultCfg["board"] = "esp32-c3-devkitm-1";
    defaultCfg["core"] = "arduino";
    defaultCfg["upload_port"] = "Auto-Detect";
    defaultCfg["upload_speed"] = "Auto";
    
    QJsonObject boardSize;
    boardSize["width_mm"] = 18.0;
    boardSize["height_mm"] = 22.52;
    boardSize["pin_width_mm"] = 15.24;
    boardSize["pin_height_mm"] = 22.52;
    defaultCfg["board_size"] = boardSize;
    defaultCfg["pin_pitch_mm"] = 2.54;
    
    QJsonArray pinsArray;
    // Left side (8 pins)
    pinsArray.append(QJsonObject{{"name", "5V"},     {"pin", "5V"},   {"role", "5V"},     {"side", "left"}, {"position", 0}});
    pinsArray.append(QJsonObject{{"name", "GND.1"},  {"pin", "GND"},  {"role", "GND"},    {"side", "left"}, {"position", 1}});
    pinsArray.append(QJsonObject{{"name", "3V3"},    {"pin", "3V3"},  {"role", "3V3"},    {"side", "left"}, {"position", 2}});
    pinsArray.append(QJsonObject{{"name", "GPIO4"},  {"pin", "4"},    {"role", "GPIO4"},  {"side", "left"}, {"position", 3}});
    pinsArray.append(QJsonObject{{"name", "GPIO3"},  {"pin", "3"},    {"role", "GPIO3"},  {"side", "left"}, {"position", 4}});
    pinsArray.append(QJsonObject{{"name", "GPIO2"},  {"pin", "2"},    {"role", "GPIO2"},  {"side", "left"}, {"position", 5}});
    pinsArray.append(QJsonObject{{"name", "GPIO1"},  {"pin", "1"},    {"role", "GPIO1"},  {"side", "left"}, {"position", 6}});
    pinsArray.append(QJsonObject{{"name", "GPIO0"},  {"pin", "0"},    {"role", "GPIO0"},  {"side", "left"}, {"position", 7}});

    // Right side (8 pins)
    pinsArray.append(QJsonObject{{"name", "GPIO5"},  {"pin", "5"},    {"role", "GPIO5"},  {"side", "right"}, {"position", 0}});
    pinsArray.append(QJsonObject{{"name", "GPIO6"},  {"pin", "6"},    {"role", "GPIO6"},  {"side", "right"}, {"position", 1}});
    pinsArray.append(QJsonObject{{"name", "GPIO7"},  {"pin", "7"},    {"role", "GPIO7"},  {"side", "right"}, {"position", 2}});
    pinsArray.append(QJsonObject{{"name", "GPIO8"},  {"pin", "8"},    {"role", "GPIO8"},  {"side", "right"}, {"position", 3}});
    pinsArray.append(QJsonObject{{"name", "GPIO9"},  {"pin", "9"},    {"role", "GPIO9"},  {"side", "right"}, {"position", 4}});
    pinsArray.append(QJsonObject{{"name", "GPIO10"}, {"pin", "10"},   {"role", "GPIO10"}, {"side", "right"}, {"position", 5}});
    pinsArray.append(QJsonObject{{"name", "GPIO20"}, {"pin", "20"},   {"role", "GPIO20"}, {"side", "right"}, {"position", 6}});
    pinsArray.append(QJsonObject{{"name", "GPIO21"}, {"pin", "21"},   {"role", "GPIO21"}, {"side", "right"}, {"position", 7}});
    defaultCfg["pins"] = pinsArray;
    
    setProperty("microcontrollerConfig", QString::fromUtf8(QJsonDocument(defaultCfg).toJson(QJsonDocument::Compact)));
    
    // Apply configuration immediately so that pins are calculated correctly at startup
    applyMicrocontrollerConfig(defaultCfg);
}

QRectF ESP32Item::boundingRect() const {
    return esp32BoundsForPins(this, m_pins);
}

void ESP32Item::mousePressEvent(QGraphicsSceneMouseEvent* event) {
    if (m_btnResetRect.contains(event->pos())) {
        m_resetPressed = true;
        update();
        event->accept();
        return;
    }
    if (m_btnBootRect.contains(event->pos())) {
        m_bootPressed = true;
        emit bootPressed(true);
        update();
        event->accept();
        return;
    }
    if (isSimulating()) {
        event->ignore();
        return;
    }
    ComponentItem::mousePressEvent(event);
}

void ESP32Item::mouseReleaseEvent(QGraphicsSceneMouseEvent* event) {
    if (m_resetPressed) {
        m_resetPressed = false;
        emit resetTriggered();
        update();
        event->accept();
        return;
    }
    if (m_bootPressed) {
        m_bootPressed = false;
        emit bootPressed(false);
        update();
        event->accept();
        return;
    }
    if (isSimulating()) {
        event->ignore();
        return;
    }
    ComponentItem::mouseReleaseEvent(event);
}

void ESP32Item::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget*) {
    painter->setRenderHint(QPainter::Antialiasing);

    QFont font = painter->font();
    QRectF body = esp32BodyRectForPins(this, m_pins);

    // Reset button rects
    m_btnResetRect = QRectF(body.left() + 5, body.bottom() - 15, 20, 10);
    m_btnBootRect = QRectF(body.right() - 25, body.bottom() - 15, 20, 10);

    // Selection Glow
    if (option->state & QStyle::State_Selected) {
        painter->setPen(QPen(QColor(99, 102, 241, 150), 2.5));
        painter->setBrush(Qt::NoBrush);
        painter->drawRoundedRect(body.adjusted(-2, -2, 2, 2), 6, 6);
    }

    // Shadow for depth and visibility on light backgrounds (shifted left and down)
    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor(15, 23, 42, 35)); // Elegant drop shadow
    painter->drawRoundedRect(body.translated(-3, 3), 5, 5);

    QString boardId = "";
    QVariant configVar = property("microcontrollerConfig");
    if (configVar.isValid() && configVar.canConvert<QString>()) {
        QJsonDocument doc = QJsonDocument::fromJson(configVar.toString().toUtf8());
        if (doc.isObject()) {
            boardId = doc.object().value("board").toString();
        }
    }

    if (boardId == "esp32-c3-wroom-02") {
        // ESP32-C3-WROOM-02: Premium Espressif Shielded Module
        // Module body (18x20mm) - Clean light gray
        painter->setPen(QPen(QColor(186, 195, 204), 1.0));
        painter->setBrush(QColor(241, 245, 249)); // Clean slate-100 (light gray)
        painter->drawRoundedRect(body, 2, 2);

        double bw = body.width();
        double bh = body.height();

        // Antenna area at top (~25% of module height = 5mm) - PCB trace pattern
        double antH = bh * 0.25;
        QRectF antArea(body.left() + 1.5, body.top() + 1.5, bw - 3, antH);
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(226, 232, 240)); // Light gray PCB (matching C3 Mini)
        painter->drawRect(antArea);

        // Antenna copper zigzag trace
        painter->setPen(QPen(QColor(148, 163, 184), 1.2)); // Slate trace
        painter->setBrush(Qt::NoBrush);
        double traceY = antArea.top() + 6;
        double traceLeft = antArea.left() + 8;
        double traceRight = antArea.right() - 8;
        for (int i = 0; i < 4 && traceY < antArea.bottom() - 3; i++) {
            painter->drawLine(QPointF(traceLeft, traceY), QPointF(traceRight, traceY));
            if (i < 3) {
                double nextY = traceY + 6;
                if (i % 2 == 0)
                    painter->drawLine(QPointF(traceRight, traceY), QPointF(traceRight, nextY));
                else
                    painter->drawLine(QPointF(traceLeft, traceY), QPointF(traceLeft, nextY));
                traceY = nextY;
            }
        }

        // Metal shield (brushed steel, covers remaining ~70%)
        double shieldTop = body.top() + antH + 3;
        double shieldH = body.bottom() - shieldTop - 3;
        QLinearGradient metalGrad(body.left(), shieldTop, body.right(), shieldTop + shieldH);
        metalGrad.setColorAt(0.0, QColor(220, 222, 228));
        metalGrad.setColorAt(0.3, QColor(200, 202, 208));
        metalGrad.setColorAt(0.7, QColor(190, 192, 198));
        metalGrad.setColorAt(1.0, QColor(175, 177, 183));
        painter->setBrush(metalGrad);
        painter->setPen(QPen(QColor(140, 142, 148), 0.5));
        painter->drawRoundedRect(QRectF(body.left() + 5, shieldTop, bw - 10, shieldH), 2.5, 2.5);

        // Espressif logo and ESP32-C3-WROOM-02 text engraved in the metal
        painter->setPen(QColor(60, 62, 68));
        QFont f = painter->font();
        f.setPointSizeF(5.5);
        f.setBold(true);
        painter->setFont(f);
        painter->drawText(QRectF(body.left(), shieldTop + 10, bw, 10), Qt::AlignCenter, "ESPRESSIF");

        f.setPointSizeF(5.0);
        f.setBold(false);
        painter->setFont(f);
        painter->drawText(QRectF(body.left(), shieldTop + 22, bw, 10), Qt::AlignCenter, "ESP32-C3-WROOM-02");
    } else if (boardId == "esp32-c3-devkitm-1" || boardId == "esp32-c3-devkitm-1") {
        // ESP32-C3 Mini / SuperMini: Clean modern light-gray board matching the platform
        painter->setPen(QPen(QColor(186, 195, 204), 1));
        painter->setBrush(QColor(241, 245, 249)); // Clean slate-100 (light gray)
        painter->drawRoundedRect(body, 4, 4);

        // Silk screen elegant border line (inset by 3px)
        painter->setPen(QPen(QColor(148, 163, 184, 80), 0.75));
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(body.adjusted(3, 3, -3, -3));

        // Center ESP32-C3 Chip (matte black QFN square, centered at y=-10)
        QRectF chipRect(-20, -25, 40, 40);
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(30, 30, 32));
        painter->drawRoundedRect(chipRect, 2, 2);

        // Gold pins/pads on edges of the QFN chip
        painter->setPen(QPen(QColor(210, 180, 80), 0.5));
        // Top and bottom tiny lines
        for (int i = -15; i <= 15; i += 6) {
            painter->drawLine(QPointF(i, -26), QPointF(i, -24));
            painter->drawLine(QPointF(i, 14), QPointF(i, 16));
            painter->drawLine(QPointF(-21, i - 5), QPointF(-19, i - 5));
            painter->drawLine(QPointF(19, i - 5), QPointF(21, i - 5));
        }

        // ESP32-C3 text on chip
        painter->setPen(QColor(220, 220, 225)); // Clean silver/white text on dark chip
        QFont f = painter->font();
        f.setPointSizeF(5.0);
        f.setBold(true);
        painter->setFont(f);
        painter->drawText(chipRect, Qt::AlignCenter, "ESP32-C3");

        // USB-C Connector (top-center, solid rounded rectangle without inner slot)
        QRectF usbConn(-18, body.top() - 3, 36, 12);
        QLinearGradient usbGrad(usbConn.left(), usbConn.top(), usbConn.right(), usbConn.bottom());
        usbGrad.setColorAt(0.0, QColor(226, 232, 240));
        usbGrad.setColorAt(1.0, QColor(190, 195, 200));
        painter->setBrush(usbGrad);
        painter->setPen(QPen(QColor(160, 165, 170), 0.75));
        painter->drawRoundedRect(usbConn, 1.5, 1.5);

        // Brand Label silkscreen (clean slate-500 text)
        painter->setPen(QColor(100, 116, 139));
        QFont f2 = painter->font();
        f2.setPointSizeF(6.0);
        f2.setBold(true);
        painter->setFont(f2);
        painter->drawText(QRectF(-body.width()/2, body.bottom() - 25, body.width(), 12), Qt::AlignCenter, "ESP32-C3 Mini");
    } else {
        // PCB board body: Deep premium matte black / charcoal (DevKitC-1 style)
        painter->setPen(QPen(QColor(40, 40, 45), 1));
        painter->setBrush(QColor(20, 20, 22));
        painter->drawRoundedRect(body, 6, 6);

        // Silk screen golden border line (inset by 3px)
        painter->setPen(QPen(QColor(180, 150, 80, 80), 0.75));
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(body.adjusted(5, 5, -5, -5));

        // Silver mounting holes at the four corners
        painter->setPen(QPen(QColor(160, 160, 165), 1.0));
        painter->setBrush(QColor(220, 220, 225));
        double holeR = 15.0; // 1.5mm radius
        painter->drawEllipse(QPointF(body.left() + 20, body.top() + 20), holeR, holeR);
        painter->drawEllipse(QPointF(body.right() - 20, body.top() + 20), holeR, holeR);
        painter->drawEllipse(QPointF(body.left() + 20, body.bottom() - 20), holeR, holeR);
        painter->drawEllipse(QPointF(body.right() - 20, body.bottom() - 20), holeR, holeR);

        // ESP32 WROOM module (shielded silver cover with antenna area)
        // Antenna area at top (~18% of board height)
        double antH = body.height() * 0.18;
        QRectF modAnt(body.left() + 35, body.top() + 35, body.width() - 70, antH);
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(12, 12, 12)); // dark charcoal antenna
        painter->drawRect(modAnt);

        // Metal shield cover (brushed silver gradient)
        QRectF modShield(body.left() + 35, body.top() + 35 + antH, body.width() - 70, 120);
        QLinearGradient metal(modShield.left(), modShield.top(), modShield.right(), modShield.bottom());
        metal.setColorAt(0.0, QColor(220, 220, 225));
        metal.setColorAt(0.5, QColor(190, 190, 195));
        metal.setColorAt(1.0, QColor(170, 170, 175));
        painter->setBrush(metal);
        painter->setPen(QPen(QColor(130, 130, 135), 0.5));
        painter->drawRoundedRect(modShield, 2, 2);

        // Wi-Fi logo and ESP32 text on metal shield
        painter->setPen(QColor(50, 50, 55));
        QFont f = painter->font();
        f.setPointSizeF(8);
        f.setBold(true);
        painter->setFont(f);
        painter->drawText(modShield.adjusted(0, 25, 0, 0), Qt::AlignHCenter | Qt::AlignTop, "WiFi ESP-WROOM-32");
        f.setPointSizeF(6.5);
        f.setBold(false);
        painter->setFont(f);
        painter->drawText(modShield.adjusted(0, 55, 0, 0), Qt::AlignHCenter | Qt::AlignTop, "FCC ID: 2AC7Z-ESP32");

        // Small black CP2102 chip in center-left
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(25, 25, 25));
        painter->drawRect(QRectF(-45, 60, 30, 30));

        // Voltage regulator (black SOT-223 chip in center-right)
        painter->setBrush(QColor(30, 30, 30));
        painter->drawRect(QRectF(20, 40, 25, 30));

        // EN button (bottom-left)
        QRectF btnEn(body.left() + 30, body.bottom() - 95, 35, 35);
        painter->setBrush(QColor(80, 80, 80));
        painter->setPen(QPen(QColor(50, 50, 50), 1.0));
        painter->drawRoundedRect(btnEn, 2, 2);
        painter->setBrush(QColor(20, 20, 20));
        painter->drawEllipse(btnEn.center(), 10, 10);

        // BOOT button (bottom-right)
        QRectF btnBoot(body.right() - 65, body.bottom() - 95, 35, 35);
        painter->setBrush(QColor(80, 80, 80));
        painter->setPen(QPen(QColor(50, 50, 50), 1.0));
        painter->drawRoundedRect(btnBoot, 2, 2);
        painter->setBrush(QColor(20, 20, 20));
        painter->drawEllipse(btnBoot.center(), 10, 10);

        // Micro-USB connector (bottom-center)
        QRectF usbConn(-25, body.bottom() - 25, 50, 25);
        QLinearGradient usbGrad(usbConn.left(), usbConn.top(), usbConn.right(), usbConn.bottom());
        usbGrad.setColorAt(0.0, QColor(200, 200, 205));
        usbGrad.setColorAt(0.5, QColor(230, 230, 235));
        usbGrad.setColorAt(1.0, QColor(180, 180, 185));
        painter->setBrush(usbGrad);
        painter->setPen(QPen(QColor(130, 130, 135), 1.0));
        painter->drawRoundedRect(usbConn, 3, 3);
    }

    // --- Interaction Buttons (Reset / Boot) for ALL variants ---
    auto drawBtn = [&](const QRectF& r, const QString& label, bool pressed) {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setPen(QPen(QColor(100, 116, 139), 0.8));
        painter->setBrush(pressed ? QColor(203, 213, 225) : QColor(241, 245, 249));
        painter->drawRoundedRect(r, 2, 2);
        
        // Button shadow for depth
        if (!pressed) {
            painter->setPen(QPen(QColor(148, 163, 184), 0.5));
            painter->drawLine(r.left() + 1, r.bottom() + 1, r.right() + 1, r.bottom() + 1);
        }

        // Label
        QFont btnFont = painter->font();
        btnFont.setPointSizeF(5.5);
        btnFont.setBold(true);
        painter->setFont(btnFont);
        painter->setPen(QColor(51, 65, 85));
        painter->drawText(r, Qt::AlignCenter, label);
        painter->restore();
    };

    drawBtn(m_btnResetRect, "RST", m_resetPressed);
    drawBtn(m_btnBootRect, "BOOT", m_bootPressed);

    // Pin markers and labels
    for (const auto& pin : m_pins) {
        QString displayName = pin.name;
        if (displayName.startsWith("GND.EPAD")) {
            // Draw the big gold thermal EPAD once at the center when rendering GND.EPAD1
            if (displayName == "GND.EPAD1" || displayName == "GND.EPAD") {
                painter->save();
                painter->setBrush(QColor(204, 170, 100)); // gold color
                painter->setPen(QPen(QColor(180, 150, 80), 0.5));
                QRectF epadRect(-14.5, -14.5, 29.0, 29.0);
                painter->drawRoundedRect(epadRect, 2, 2);

                painter->setPen(QColor(60, 60, 65));
                QFont epadFont = painter->font();
                epadFont.setPointSizeF(5.5);
                epadFont.setBold(true);
                painter->setFont(epadFont);
                painter->drawText(epadRect, Qt::AlignCenter, "GND\nEPAD");
                painter->restore();
            }

            // Draw a small copper/drill via for the functional pin connection point
            painter->save();
            painter->setPen(QPen(QColor(130, 100, 40), 0.5));
            painter->setBrush(QColor(190, 160, 70));
            painter->drawEllipse(pin.localPos, 2.2, 2.2); // gold ring
            painter->setBrush(QColor(15, 15, 18));
            painter->drawEllipse(pin.localPos, 0.8, 0.8); // black drill center
            painter->restore();
            continue; // Skip normal pin drawing and label drawing!
        }
        QString pinNumStr = "";
        if (displayName.contains("|")) {
            QStringList parts = displayName.split("|");
            pinNumStr = parts[0];
            displayName = parts[1];
        }

        if (displayName.startsWith('G') && displayName.size() > 1) {
            bool ok = false;
            int num = displayName.mid(1).toInt(&ok);
            if (ok) displayName = QString("GPIO%1").arg(num);
        }
        if (displayName == "GND_L" || displayName == "GND_R" || displayName.startsWith("GND.")) {
            displayName = "GND";
        }

        if (boardId == "esp32-c3-wroom-02") {
            // ESP32-C3-WROOM-02: 1.5mm length x 0.9mm width, mathematically centered on pin position
            painter->setBrush(QColor(204, 170, 100)); // gold color
            painter->setPen(QPen(QColor(180, 150, 80), 0.5));
            double padW = 15.0; // pad length (in X)
            double padH = 9.0;  // pad width (in Y)
            QRectF padRect(pin.localPos.x() - padW/2.0, pin.localPos.y() - padH/2.0, padW, padH);
            painter->drawRoundedRect(padRect, 1.5, 1.5);
        } else if (boardId == "esp-12e") {
            // ESP-12E castellated pads (gold rectangles at module edge)
            // Pad: 1.0mm wide x 1.8mm long = 10px x 18px at mmToPx=10
            painter->setBrush(QColor(204, 170, 100)); // gold color
            painter->setPen(QPen(QColor(180, 150, 80), 0.5));
            
            QRectF padRect;
            if (pin.localPos.y() > 110.0) {
                // Bottom pad - vertical orientation (extends upwards from bottom edge)
                double padW = 10.0; // pad width (in X)
                double padH = 18.0; // pad length (in Y)
                padRect = QRectF(pin.localPos.x() - padW/2.0, pin.localPos.y() - padH * 0.7, padW, padH);
            } else if (pin.localPos.x() < 0) {
                // Left side pad - horizontal orientation (extends rightwards from left edge)
                double padW = 18.0; // pad length (in X)
                double padH = 10.0; // pad width (in Y)
                padRect = QRectF(pin.localPos.x() - padW * 0.3, pin.localPos.y() - padH/2.0, padW, padH);
            } else {
                // Right side pad - horizontal orientation (extends leftwards from right edge)
                double padW = 18.0; // pad length (in X)
                double padH = 10.0; // pad width (in Y)
                padRect = QRectF(pin.localPos.x() - padW * 0.7, pin.localPos.y() - padH/2.0, padW, padH);
            }
            painter->drawRoundedRect(padRect, 1.5, 1.5);
        } else {
            bool isC3Mini = (boardId == "esp32-c3-devkitm-1" || boardId == "esp32-c3-devkitm-1");
            if (isC3Mini) {
                // Solid colors for modern aesthetic
                QColor pinColor = QColor(59, 130, 246); // Blue (GPIO/Signal)
                if (displayName == "GND") {
                    pinColor = QColor(30, 30, 32); // Black
                } else if (displayName == "VCC" || displayName == "5V" || displayName == "3V3" || displayName == "VIN") {
                    pinColor = QColor(239, 68, 68); // Red (Power)
                }

                painter->setPen(QPen(pinColor.darker(115), 1.0));
                painter->setBrush(pinColor);
                painter->drawEllipse(pin.localPos, 6.0, 6.0);

                // Small white dot in center for depth/aesthetic
                painter->setPen(Qt::NoPen);
                painter->setBrush(QColor(255, 255, 255, 90));
                painter->drawEllipse(pin.localPos, 1.2, 1.2);
            } else {
                // Gold THT via ring
                painter->setPen(QPen(QColor(130, 100, 40), 1.0));
                QRadialGradient goldGrad(pin.localPos, 6.0);
                goldGrad.setColorAt(0.0, QColor(240, 210, 110));
                goldGrad.setColorAt(0.7, QColor(190, 160, 70));
                goldGrad.setColorAt(1.0, QColor(150, 120, 50));
                painter->setBrush(goldGrad);
                painter->drawEllipse(pin.localPos, 6.5, 6.5);

                // Black drill center
                painter->setPen(Qt::NoPen);
                painter->setBrush(QColor(15, 15, 18));
                painter->drawEllipse(pin.localPos, 2.5, 2.5);
            }
        }

        // Draw pin label
        if (boardId == "esp32-s3-devkitc-1") {
            painter->setPen(QPen(QColor(230, 230, 230)));
        } else {
            painter->setPen(QPen(QColor(80, 80, 80)));
        }
        QFont font = painter->font();
        font.setPointSizeF((boardId == "esp-12e" || boardId == "esp32-c3-wroom-02") ? 7 : 10); // Smaller font for modules
        painter->setFont(font);

        bool isTopPin = false;
        bool isBottomPin = false;

        if (boardId == "esp-12e") {
            if (pin.localPos.y() > 110.0) {
                isBottomPin = true;
            }
        }

        double txtOffset = (boardId == "esp-12e" || boardId == "esp32-c3-wroom-02") ? 6 : 8;
        double labelWidth = (boardId == "esp-12e" || boardId == "esp32-c3-wroom-02") ? 60 : 70;
        double labelHeight = (boardId == "esp-12e" || boardId == "esp32-c3-wroom-02") ? 14 : 12;

        if (isTopPin) {
            // top side (labels outside, above pin, vertical)
            painter->save();
            painter->translate(pin.localPos.x(), pin.localPos.y() - txtOffset);
            painter->rotate(-90);
            painter->drawText(QRectF(0, -labelHeight/2, labelWidth, labelHeight), Qt::AlignLeft | Qt::AlignVCenter, displayName);
            painter->restore();
        } else if (isBottomPin) {
            // bottom side (labels outside, below pin, vertical)
            painter->save();
            painter->translate(pin.localPos.x(), pin.localPos.y() + txtOffset);
            painter->rotate(-90);
            painter->drawText(QRectF(-labelWidth, -labelHeight/2, labelWidth, labelHeight), Qt::AlignRight | Qt::AlignVCenter, displayName);
            painter->restore();
        } else if (pin.localPos.x() < 0) {
            if (boardId == "esp-12e" || boardId == "esp32-c3-wroom-02") {
                // left side (labels outside)
                painter->drawText(QRectF(pin.localPos.x() - labelWidth - txtOffset + 5, pin.localPos.y() - labelHeight/2, labelWidth, labelHeight), Qt::AlignRight | Qt::AlignVCenter, displayName);
            } else {
                // left side (labels inside)
                painter->drawText(QRectF(pin.localPos.x() + 10, pin.localPos.y() - 6, 65, 12), Qt::AlignLeft | Qt::AlignVCenter, displayName);
            }
        } else {
            if (boardId == "esp-12e" || boardId == "esp32-c3-wroom-02") {
                // right side (labels outside)
                painter->drawText(QRectF(pin.localPos.x() + txtOffset - 2, pin.localPos.y() - labelHeight/2, labelWidth, labelHeight), Qt::AlignLeft | Qt::AlignVCenter, displayName);
            } else {
                // right side (labels inside)
                painter->drawText(QRectF(pin.localPos.x() - 75, pin.localPos.y() - 6, 65, 12), Qt::AlignRight | Qt::AlignVCenter, displayName);
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// LED ITEM IMPLEMENTATION
// ─────────────────────────────────────────────────────────────────────────────
LEDItem::LEDItem(const QString& id, const QString& name, QGraphicsItem* parent)
    : ComponentItem(id, name, "led", parent) {
    m_pins.append({"Anode",   QPointF(-10, 30), false, "", "", QColor(239, 68, 68)});
    m_pins.append({"Cathode", QPointF(10,  30), false, "", "", QColor(75, 85, 99)});
    
    QString idx = extractIndexFromId(id);
    m_name = "LED " + (idx.isEmpty() ? "1" : idx);
}

QRectF LEDItem::boundingRect() const {

    if (property("isSMD").toBool()) {
        QString size = property("smdSize").toString();
        double w = 20.0, h = 12.5;
        if (size == "0603") { w = 16; h = 8; }
        else if (size == "0805") { w = 20; h = 12.5; }
        else if (size == "1206") { w = 32; h = 16; }
        else if (size == "5050") { w = 50; h = 50; }
        return QRectF(-w/2.0 - 5, -h/2.0 - 5, w + 10, h + 10);
    }
    return QRectF(-25, -25, 50, 65);

}

void LEDItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget*) {

    painter->setRenderHint(QPainter::Antialiasing);

    if (property("isSMD").toBool()) {
        QString size = property("smdSize").toString();
        double w = 20.0, h = 12.5;
        if (size == "0603") { w = 16; h = 8; }
        else if (size == "0805") { w = 20; h = 12.5; }
        else if (size == "1206") { w = 32; h = 16; }
        else if (size == "5050") { w = 50; h = 50; }

        if (option->state & QStyle::State_Selected) {
            painter->setPen(QPen(QColor(99, 102, 241, 150), 2.5, Qt::SolidLine));
            painter->setBrush(Qt::NoBrush);
            painter->drawRoundedRect(-w/2.0 - 2, -h/2.0 - 2, w + 4, h + 4, 2, 2);
        }

        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(250, 250, 250));
        painter->drawRect(-w/2.0, -h/2.0, w, h);

        painter->setBrush(QColor(200, 200, 200));
        double termW = w * 0.15;
        if (size == "5050") {
            painter->drawRect(-w/2.0, -h/2.0 + 2, termW, h/3 - 2);
            painter->drawRect(-w/2.0, h/6, termW, h/3 - 2);
            painter->drawRect(w/2.0 - termW, -h/2.0 + 2, termW, h/3 - 2);
            painter->drawRect(w/2.0 - termW, h/6, termW, h/3 - 2);
        } else {
            painter->drawRect(-w/2.0, -h/2.0, termW, h);
            painter->drawRect(w/2.0 - termW, -h/2.0, termW, h);
        }

        QColor ledColor = m_isOn ? QColor(255, 255, 0) : QColor(240, 230, 140);
        painter->setBrush(ledColor);
        painter->setPen(QPen(QColor(200, 200, 100), 1));
        if (size == "5050") {
            painter->drawEllipse(QPointF(0, 0), w*0.4, w*0.4);
        } else {
            painter->drawEllipse(QPointF(0, 0), w*0.3, h*0.4);
        }
    } else {
        if (option->state & QStyle::State_Selected) {
            painter->setPen(QPen(QColor(99, 102, 241, 150), 2, Qt::SolidLine));
            painter->drawEllipse(QPointF(0, 0), 22, 22);
        }
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(15, 23, 42, 40));
        painter->drawEllipse(QPointF(0, 4), 18, 18);
        painter->setPen(QPen(QColor(156, 163, 175), 2));
        painter->drawLine(-10, 10, -10, 30);
        painter->drawLine(10,  10, 10,  30);
        painter->setBrush(QColor(234, 179, 8)); 
        painter->setPen(QPen(QColor(161, 98, 7), 1));
        painter->drawEllipse(QPointF(-10, 30), 3, 3);
        painter->drawEllipse(QPointF(10,  30), 3, 3);
        QRadialGradient grad(0, 0, 18);
        QPen domePen;
        if (m_isOn) {
            grad.setColorAt(0.0, QColor(255, 220, 220));
            grad.setColorAt(0.3, QColor(239, 68, 68));
            grad.setColorAt(1.0, QColor(185, 28, 28));
            domePen = QPen(QColor(185, 28, 28), 1);
        } else {
            grad.setColorAt(0.0, QColor(255, 255, 255, 240)); 
            grad.setColorAt(0.6, QColor(254, 226, 226, 200)); 
            grad.setColorAt(1.0, QColor(248, 113, 113, 120)); 
            domePen = QPen(QColor(239, 68, 68, 140), 1);      
        }
        painter->setBrush(grad);
        painter->setPen(domePen);
        painter->drawEllipse(QPointF(0, 0), 18, 18);
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(255, 255, 255, 120));
        painter->drawEllipse(QPointF(-6, -6), 6, 4);
    }

}

// ─────────────────────────────────────────────────────────────────────────────
// BUTTON ITEM IMPLEMENTATION
// ─────────────────────────────────────────────────────────────────────────────
ButtonItem::ButtonItem(const QString& id, const QString& name, QGraphicsItem* parent)
    : ComponentItem(id, name, "button", parent) {
    m_pins.append({"A1", QPointF(-30, -20), false, "", "", QColor(59, 130, 246)});
    m_pins.append({"A2", QPointF(-30,  20), false, "", "", QColor(59, 130, 246)});
    m_pins.append({"B1", QPointF( 30, -20), false, "", "", QColor(59, 130, 246)});
    m_pins.append({"B2", QPointF( 30,  20), false, "", "", QColor(59, 130, 246)});
}

QRectF ButtonItem::boundingRect() const {
    return QRectF(-35, -30, 70, 65);
}

void ButtonItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget*) {
    painter->setRenderHint(QPainter::Antialiasing);

    // Selection Glow
    if (option->state & QStyle::State_Selected) {
        painter->setPen(QPen(QColor(99, 102, 241, 150), 2, Qt::SolidLine));
        painter->setBrush(Qt::NoBrush);
        painter->drawRoundedRect(-24, -24, 48, 48, 4, 4);
    }

    // Shadow
    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor(15, 23, 42, 50));
    painter->drawRoundedRect(-21, -19, 42, 42, 4, 4);

    // Metal tabs (Legs) — stretched to reach 10px-aligned pin positions
    painter->setPen(QPen(QColor(156, 163, 175), 2));
    painter->drawLine(-20, -20, -30, -20);
    painter->drawLine(-20,  20, -30,  20);
    painter->drawLine( 20, -20,  30, -20);
    painter->drawLine( 20,  20,  30,  20);

    // Pin Gold Pads
    for (const auto& pin : m_pins) {
        painter->setBrush(QColor(234, 179, 8));
        painter->setPen(QPen(QColor(161, 98, 7), 1));
        painter->drawEllipse(pin.localPos, 3, 3);
    }

    // Mechanical Base (Premium Brushed Black Plastic)
    // Premium light gray matte plastic base
    QLinearGradient baseGrad(-20, -20, 20, 20);
    baseGrad.setColorAt(0.0, QColor(241, 245, 249)); // slate-100
    baseGrad.setColorAt(1.0, QColor(203, 213, 225)); // slate-300
    painter->setBrush(baseGrad);
    painter->setPen(QPen(QColor(148, 163, 184), 1));  // slate-400
    painter->drawRoundedRect(-20, -20, 40, 40, 4, 4);

    // Metal Ring Cap (lighter, polished silver chrome)
    painter->setBrush(QColor(226, 232, 240));        // slate-200
    painter->setPen(QPen(QColor(148, 163, 184), 1)); // slate-400
    painter->drawEllipse(QPointF(0, 0), 12, 12);

    // Central Plunger (Lighter, vibrant Red Button Cap)
    QRadialGradient buttonGrad(0, 0, 9);
    if (m_isPressed) {
        buttonGrad.setColorAt(0.0, QColor(220, 38, 38));
        buttonGrad.setColorAt(1.0, QColor(153, 27, 27));
    } else {
        buttonGrad.setColorAt(0.0, QColor(252, 165, 165)); // soft vibrant coral-300
        buttonGrad.setColorAt(1.0, QColor(239, 68, 68));   // vibrant red
    }
    painter->setBrush(buttonGrad);
    painter->setPen(QPen(QColor(153, 27, 27), 1));
    painter->drawEllipse(QPointF(0, 0), 9, 9);

    // Reflection on plunger
    if (!m_isPressed) {
        painter->setBrush(QColor(255, 255, 255, 80));
        painter->setPen(Qt::NoPen);
        painter->drawEllipse(QPointF(-3, -3), 3, 1.5);
    }

    // Name Label
    painter->setPen(QColor(148, 163, 184));
    QFont fLabelIdx = painter->font();
    fLabelIdx.setPointSize(5);
    fLabelIdx.setBold(true);
    painter->setFont(fLabelIdx);
    painter->drawText(QRectF(-25, 22, 50, 10), Qt::AlignCenter, m_name.toUpper());
}

void ButtonItem::mousePressEvent(QGraphicsSceneMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        // Check if user clicked the plunger area (radius 15)
        QPointF local = event->pos();
        if (local.x()*local.x() + local.y()*local.y() <= 225) {
            m_isPressed = true;
            emit stateChanged(true);
            update();
            event->accept();
            return;
        }
    }
    if (isSimulating()) {
        event->ignore();
        return;
    }
    ComponentItem::mousePressEvent(event);
}

void ButtonItem::mouseReleaseEvent(QGraphicsSceneMouseEvent* event) {
    if (m_isPressed) {
        m_isPressed = false;
        emit stateChanged(false);
        update();
        event->accept();
        return;
    }
    if (isSimulating()) {
        event->ignore();
        return;
    }
    ComponentItem::mouseReleaseEvent(event);
}

// ─────────────────────────────────────────────────────────────────────────────
// RESISTOR ITEM IMPLEMENTATION
// ─────────────────────────────────────────────────────────────────────────────
static QString formatResValue(double val) {
    if (val >= 1000000.0) {
        double m = val / 1000000.0;
        return QString("%1M").arg(QString::number(m, 'g', 4));
    } else if (val >= 1000.0) {
        double k = val / 1000.0;
        return QString("%1k").arg(QString::number(k, 'g', 4));
    } else {
        return QString("%1R").arg(QString::number(val, 'g', 4));
    }
}

ResistorItem::ResistorItem(const QString& id, const QString& name, QGraphicsItem* parent)
    : ComponentItem(id, name, "resistor", parent), m_resistance(10000.0) {
    m_pins.append({"1", QPointF(-30, 0), false, "", "", QColor(239, 68, 68)});
    m_pins.append({"2", QPointF( 30, 0), false, "", "", QColor(75, 85, 99)});

    QString lowerName = name.toLower();
    if (lowerName.contains("220")) {
        m_resistance = 220.0;
    } else if (lowerName.contains("1k")) {
        m_resistance = 1000.0;
    } else if (lowerName.contains("10k")) {
        m_resistance = 10000.0;
    } else if (lowerName.contains("100k")) {
        m_resistance = 100000.0;
    }

    m_name = QString("Resistor %1").arg(formatResValue(m_resistance));
}

void ResistorItem::setResistance(double value) {
    m_resistance = value;
    m_name = QString("Resistor %1").arg(formatResValue(m_resistance));
    update();
}

void ResistorItem::setCustomLabels(const QString& left, const QString& right) {
    m_customTextL = left;
    m_customTextR = right;
    update();
}

QRectF ResistorItem::boundingRect() const {

    if (property("isSMD").toBool()) {
        QString size = property("smdSize").toString();
        double w = 32.0, h = 16.0;
        if (size == "0402") { w = 10; h = 5; }
        else if (size == "0603") { w = 16; h = 8; }
        else if (size == "0805") { w = 20; h = 12.5; }
        else if (size == "1206") { w = 32; h = 16; }
        return QRectF(-w/2.0 - 5, -h/2.0 - 5, w + 10, h + 10);
    }
    return QRectF(-35, -20, 70, 40);

}

void ResistorItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget*) {

    painter->setRenderHint(QPainter::Antialiasing);

    if (property("isSMD").toBool()) {
        QString size = property("smdSize").toString();
        double w = 32.0, h = 16.0;
        if (size == "0402") { w = 10; h = 5; }
        else if (size == "0603") { w = 16; h = 8; }
        else if (size == "0805") { w = 20; h = 12.5; }
        else if (size == "1206") { w = 32; h = 16; }

        if (option->state & QStyle::State_Selected) {
            painter->setPen(QPen(QColor(99, 102, 241, 150), 2.5, Qt::SolidLine));
            painter->setBrush(Qt::NoBrush);
            painter->drawRoundedRect(-w/2.0 - 2, -h/2.0 - 2, w + 4, h + 4, 2, 2);
        }

        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(30, 30, 30));
        painter->drawRect(-w/2.0, -h/2.0, w, h);

        painter->setBrush(QColor(200, 200, 200));
        double termW = w * 0.2;
        painter->drawRect(-w/2.0, -h/2.0, termW, h);
        painter->drawRect(w/2.0 - termW, -h/2.0, termW, h);

        if (size == "0805" || size == "1206") {
            double val = m_resistance;
            QString text;
            if (val >= 10 && val < 100) text = QString("%1R0").arg(int(val));
            else if (val >= 100) {
                int exp = 0;
                while (val >= 100) { val /= 10.0; exp++; }
                text = QString("%1%2").arg(int(val)).arg(exp);
            } else text = QString("%1").arg(val);

            painter->setPen(QColor(255, 255, 255));
            QFont f = painter->font();
            f.setPixelSize(h * 0.6);
            f.setBold(true);
            painter->setFont(f);
            painter->drawText(QRectF(-w/2.0 + termW, -h/2.0, w - 2*termW, h), Qt::AlignCenter, text);
        }
    } else {
        if (option->state & QStyle::State_Selected) {
            painter->setPen(QPen(QColor(99, 102, 241, 150), 2.5, Qt::SolidLine));
            painter->setBrush(Qt::NoBrush);
            painter->drawRoundedRect(-18, -10, 36, 20, 4, 4);
        }
        painter->setPen(QPen(QColor(156, 163, 175), 2));
        painter->drawLine(-30, 0, -15, 0);
        painter->drawLine( 15, 0,  30, 0);
        painter->setBrush(QColor(234, 179, 8)); // Gold
        painter->setPen(QPen(QColor(161, 98, 7), 1));
        painter->drawEllipse(QPointF(-30, 0), 3, 3);
        painter->drawEllipse(QPointF( 30, 0), 3, 3);
        QLinearGradient bodyGrad(-15, -8, 15, 8);
        bodyGrad.setColorAt(0.0, QColor(219, 234, 254));
        bodyGrad.setColorAt(1.0, QColor(147, 197, 253));
        painter->setBrush(bodyGrad);
        painter->setPen(QPen(QColor(96, 165, 250), 1));
        painter->drawRoundedRect(-15, -8, 30, 16, 3, 3);

        double val = m_resistance;
        int exponent = 0;
        double temp = val;
        if (temp > 0) {
            while (temp >= 100.0) { temp /= 10.0; exponent++; }
            while (temp < 10.0 && exponent > -2) { temp *= 10.0; exponent--; }
        }
        int sigDigits = int(temp + 0.5);
        if (sigDigits >= 100) { sigDigits /= 10; exponent++; }
        int d1 = sigDigits / 10;
        int d2 = sigDigits % 10;
        int mult = exponent;
        auto getBandColor = [](int bandVal) -> QColor {
            switch (bandVal) {
                case -2: return QColor(209, 213, 219);
                case -1: return QColor(234, 179, 8);
                case 0: return QColor(15, 23, 42);
                case 1: return QColor(120, 53, 4);
                case 2: return QColor(239, 68, 68);
                case 3: return QColor(249, 115, 22);
                case 4: return QColor(253, 224, 71);
                case 5: return QColor(34, 197, 94);
                case 6: return QColor(59, 130, 246);
                case 7: return QColor(168, 85, 247);
                case 8: return QColor(148, 163, 184);
                case 9: return QColor(255, 255, 255);
                default: return QColor(148, 163, 184);
            }
        };
        painter->setPen(Qt::NoPen);
        painter->setBrush(getBandColor(d1)); painter->drawRect(-10, -8, 3, 16);
        painter->setBrush(getBandColor(d2)); painter->drawRect(-5, -8, 3, 16);
        painter->setBrush(getBandColor(mult)); painter->drawRect(1, -8, 3, 16);
        painter->setBrush(QColor(234, 179, 8)); painter->drawRect(8, -8, 3, 16);
    }

}

// ─────────────────────────────────────────────────────────────────────────────
// CAPACITOR ITEM IMPLEMENTATION
// ─────────────────────────────────────────────────────────────────────────────
CapacitorItem::CapacitorItem(const QString& id, const QString& name, QGraphicsItem* parent)
    : ComponentItem(id, name, "capacitor", parent) {
    m_pins.append({"1", QPointF(-10, 20), false, "", "", QColor(239, 68, 68)});
    m_pins.append({"2", QPointF(10, 20), false, "", "", QColor(75, 85, 99)});
}

QRectF CapacitorItem::boundingRect() const {

    if (property("isSMD").toBool()) {
        QString size = property("smdSize").toString();
        double w = 32.0, h = 16.0;
        if (size == "0402") { w = 10; h = 5; }
        else if (size == "0603") { w = 16; h = 8; }
        else if (size == "0805") { w = 20; h = 12.5; }
        else if (size == "1206") { w = 32; h = 16; }
        return QRectF(-w/2.0 - 5, -h/2.0 - 5, w + 10, h + 10);
    }
    return QRectF(-25, -25, 50, 45);

}

void CapacitorItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget*) {

    painter->setRenderHint(QPainter::Antialiasing);

    if (property("isSMD").toBool()) {
        QString size = property("smdSize").toString();
        double w = 32.0, h = 16.0;
        if (size == "0402") { w = 10; h = 5; }
        else if (size == "0603") { w = 16; h = 8; }
        else if (size == "0805") { w = 20; h = 12.5; }
        else if (size == "1206") { w = 32; h = 16; }

        if (option->state & QStyle::State_Selected) {
            painter->setPen(QPen(QColor(99, 102, 241, 150), 2.5, Qt::SolidLine));
            painter->setBrush(Qt::NoBrush);
            painter->drawRoundedRect(-w/2.0 - 2, -h/2.0 - 2, w + 4, h + 4, 2, 2);
        }

        // SMD Body
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(186, 140, 99));
        painter->drawRect(-w/2.0, -h/2.0, w, h);

        // Silver Terminals
        painter->setBrush(QColor(200, 200, 200));
        double termW = w * 0.2;
        painter->drawRect(-w/2.0, -h/2.0, termW, h);
        painter->drawRect(w/2.0 - termW, -h/2.0, termW, h);
    } else {
        if (option->state & QStyle::State_Selected) {
            painter->setPen(QPen(QColor(99, 102, 241, 150), 2.5, Qt::SolidLine));
            painter->setBrush(Qt::NoBrush);
            painter->drawEllipse(QPointF(0, -6), 14, 14);
        }
        painter->setPen(QPen(QColor(156, 163, 175), 2));
        painter->drawLine(-10, -6, -20, -6);
        painter->drawLine(-20, -6, -20, 10);
        painter->drawLine( 10, -6,  20, -6);
        painter->drawLine( 20, -6,  20, 10);
        painter->setBrush(QColor(234, 179, 8)); 
        painter->setPen(QPen(QColor(161, 98, 7), 1));
        painter->drawEllipse(QPointF(-20, 10), 3, 3);
        painter->drawEllipse(QPointF( 20, 10), 3, 3);
        QRadialGradient capGrad(0, -6, 12);
        capGrad.setColorAt(0.0, QColor(253, 230, 138));
        capGrad.setColorAt(1.0, QColor(217, 119, 6));
        painter->setBrush(capGrad);
        painter->setPen(QPen(QColor(180, 83, 9), 1));
        painter->drawEllipse(QPointF(0, -6), 12, 12);
        painter->setPen(QPen(QColor(120, 53, 4)));
        QFont font = painter->font();
        font.setPixelSize(7);
        font.setBold(true);
        painter->setFont(font);
        QString val = m_name.split(" ").last();
        val.replace("F", "");
        painter->drawText(QRectF(-10, -12, 20, 12), Qt::AlignCenter, val);
    }

}

// ─────────────────────────────────────────────────────────────────────────────
// POTENTIOMETER ITEM IMPLEMENTATION
// ─────────────────────────────────────────────────────────────────────────────
PotentiometerItem::PotentiometerItem(const QString& id, const QString& name, QGraphicsItem* parent)
    : ComponentItem(id, name, "potentiometer", parent), m_value(50.0) {
    m_pins.append({"1", QPointF(-20, 30), false, "", "", QColor(59, 130, 246)});
    m_pins.append({"2", QPointF(  0, 30), false, "", "", QColor(234, 179, 8)});
    m_pins.append({"3", QPointF( 20, 30), false, "", "", QColor(75, 85, 99)});
    m_name = QString("Potenciometro 50%");
}

void PotentiometerItem::setValue(double val) {
    m_value = qMax(0.0, qMin(100.0, val));
    m_name = QString("Potenciômetro %1%").arg(static_cast<int>(m_value));
    update();
}

void PotentiometerItem::wheelEvent(QGraphicsSceneWheelEvent* event) {
    double delta = event->delta() > 0 ? 5.0 : -5.0;
    setValue(m_value + delta);
    emit valueChanged(m_value);
    event->accept();
}

QRectF PotentiometerItem::boundingRect() const {
    return QRectF(-25, -25, 50, 60);
}

void PotentiometerItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget*) {
    painter->setRenderHint(QPainter::Antialiasing);

    // Selection Glow
    if (option->state & QStyle::State_Selected) {
        painter->setPen(QPen(QColor(99, 102, 241, 150), 2.5, Qt::SolidLine));
        painter->setBrush(Qt::NoBrush);
        painter->drawRoundedRect(-22, -22, 44, 34, 5, 5);
    }

    // Metal leads — extended to reach 10px-aligned pins at y=30, x=±20
    painter->setPen(QPen(QColor(156, 163, 175), 2));
    painter->drawLine(-20, 10, -20, 30);
    painter->drawLine(  0, 10,   0, 30);
    painter->drawLine( 20, 10,  20, 30);

    // Gold pads
    painter->setBrush(QColor(234, 179, 8));
    painter->setPen(QPen(QColor(161, 98, 7), 1));
    painter->drawEllipse(QPointF(-20, 30), 3, 3);
    painter->drawEllipse(QPointF(  0, 30), 3, 3);
    painter->drawEllipse(QPointF( 20, 30), 3, 3);

    // Blue casing body
    QLinearGradient bodyGrad(-20, -20, 20, 10);
    bodyGrad.setColorAt(0.0, QColor(2, 132, 199));
    bodyGrad.setColorAt(1.0, QColor(3, 105, 161));
    painter->setBrush(bodyGrad);
    painter->setPen(QPen(QColor(2, 132, 199), 1));
    painter->drawRoundedRect(-20, -20, 40, 30, 4, 4);

    // Rotary dial
    painter->setBrush(QColor(241, 245, 249));
    painter->setPen(QPen(QColor(148, 163, 184), 1));
    painter->drawEllipse(QPointF(0, -5), 10, 10);

    // Indent mark rotated by m_value (0 to 100 mapped to -135 to 135 degrees)
    double angleDegrees = -135.0 + (m_value / 100.0) * 270.0;
    double angleRad = angleDegrees * M_PI / 180.0;
    double dx = 8.0 * std::sin(angleRad);
    double dy = -8.0 * std::cos(angleRad);

    painter->setPen(QPen(QColor(71, 85, 105), 2));
    painter->drawLine(QPointF(0, -5), QPointF(dx, -5 + dy));

    // Name Label
    painter->setPen(QColor(148, 163, 184));
    QFont fLabelIdx = painter->font();
    fLabelIdx.setPointSize(5);
    fLabelIdx.setBold(true);
    painter->setFont(fLabelIdx);
    painter->drawText(QRectF(-25, 22, 50, 10), Qt::AlignCenter, m_name.toUpper());
}

// ─────────────────────────────────────────────────────────────────────────────
// BUZZER ITEM IMPLEMENTATION
// ─────────────────────────────────────────────────────────────────────────────
BuzzerItem::BuzzerItem(const QString& id, const QString& name, QGraphicsItem* parent)
    : ComponentItem(id, name, "buzzer", parent) {
    m_pins.append({"1", QPointF(-10, 20), false, "", "", QColor(239, 68, 68)});
    m_pins.append({"2", QPointF( 10, 20), false, "", "", QColor(75, 85, 99)});
    
    QString idx = extractIndexFromId(id);
    m_name = "Buzzer-" + (idx.isEmpty() ? "1" : idx);
}

QRectF BuzzerItem::boundingRect() const {
    return QRectF(-45, -25, 90, 62);
}

void BuzzerItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget*) {
    painter->setRenderHint(QPainter::Antialiasing);

    // Draw sound waves if active
    if (m_isActive) {
        painter->setPen(QPen(QColor(245, 158, 11, 200), 2, Qt::SolidLine));
        // Draw waves on the left
        painter->drawArc(QRectF(-32, -18, 10, 30), -60 * 16, 120 * 16);
        painter->drawArc(QRectF(-39, -23, 12, 40), -60 * 16, 120 * 16);

        // Draw waves on the right
        painter->drawArc(QRectF(22, -18, 10, 30), 120 * 16, 120 * 16);
        painter->drawArc(QRectF(27, -23, 12, 40), 120 * 16, 120 * 16);
    }

    // Selection Glow
    if (option->state & QStyle::State_Selected) {
        painter->setPen(QPen(QColor(99, 102, 241, 150), 2.5, Qt::SolidLine));
        painter->setBrush(Qt::NoBrush);
        painter->drawEllipse(QPointF(0, -3), 21, 21);
    }

    // Metal leads — aligned to 10px-grid pins at x=±10
    painter->setPen(QPen(QColor(156, 163, 175), 2));
    painter->drawLine(-10, 5, -10, 20);
    painter->drawLine( 10, 5,  10, 20);

    // Gold pads
    painter->setBrush(QColor(234, 179, 8));
    painter->setPen(QPen(QColor(161, 98, 7), 1));
    painter->drawEllipse(QPointF(-10, 20), 3, 3);
    painter->drawEllipse(QPointF( 10, 20), 3, 3);

    // Buzzer Body (Black circular capsule)
    QRadialGradient bodyGrad(0, -3, 18);
    bodyGrad.setColorAt(0.0, QColor(71, 85, 105));
    bodyGrad.setColorAt(0.8, QColor(30, 41, 59));
    bodyGrad.setColorAt(1.0, QColor(15, 23, 42));
    painter->setBrush(bodyGrad);
    painter->setPen(QPen(QColor(51, 65, 85), 1));
    painter->drawEllipse(QPointF(0, -3), 18, 18);

    // Sound release hole
    painter->setBrush(QColor(2, 6, 23));
    painter->setPen(Qt::NoPen);
    painter->drawEllipse(QPointF(0, -3), 4, 4);

    // Polarity (+) sign
    painter->setPen(QPen(QColor(239, 68, 68), 1));
    painter->drawLine(-14, -8, -10, -8);
    painter->drawLine(-12, -10, -12, -6);

    // Draw active/passive type indicator
    painter->setPen(QColor(148, 163, 184));
    QFont fontType = painter->font();
    fontType.setPointSize(5);
    fontType.setBold(true);
    painter->setFont(fontType);
    painter->drawText(QRectF(-18, -25, 36, 10), Qt::AlignCenter, m_isPassive ? "PASSIVE" : "ACTIVE");

    if (m_isPassive) {
        painter->drawText(QRectF(-30, 10, 60, 10), Qt::AlignCenter, QString("%1 Hz").arg(m_frequency));
    }

    // Name Label
    painter->setPen(QColor(148, 163, 184));
    QFont fLabelB = painter->font();
    fLabelB.setPointSize(5);
    fLabelB.setBold(true);
    painter->setFont(fLabelB);
    painter->drawText(QRectF(-25, 22, 50, 10), Qt::AlignCenter, m_name.toUpper());
}

void BuzzerItem::contextMenuEvent(QGraphicsSceneContextMenuEvent* event) {
    QMenu menu;
    QAction* typeAct = menu.addAction(m_isPassive ? "Alternar para Buzzer Ativo" : "Alternar para Buzzer Passivo");
    QAction* freqAct = m_isPassive ? menu.addAction("Ajustar Frequência Padrão...") : nullptr;
    
    QAction* selected = menu.exec(event->screenPos());
    if (selected == typeAct) {
        setPassive(!m_isPassive);
    } else if (freqAct && selected == freqAct) {
        bool ok;
        int newFreq = QInputDialog::getInt(nullptr, "Ajustar Frequência", "Frequência (Hz):", m_frequency, 50, 10000, 100, &ok);
        if (ok) {
            setFrequency(newFreq);
        }
    }
    event->accept();
}

// ─────────────────────────────────────────────────────────────────────────────
// MOTOR ITEM IMPLEMENTATION
// ─────────────────────────────────────────────────────────────────────────────
MotorItem::MotorItem(const QString& id, const QString& name, QGraphicsItem* parent)
    : ComponentItem(id, name, "motor", parent), m_motorType("servo180"), m_currentAngle(0.0) {
    m_pins.append({"GND", QPointF(-20, 30), false, "", "", QColor(75, 85, 99)});
    m_pins.append({"VCC", QPointF(  0, 30), false, "", "", QColor(239, 68, 68)});
    m_pins.append({"PWM", QPointF( 20, 30), false, "", "", QColor(59, 130, 246)});
    
    QString idx = extractIndexFromId(id);
    m_name = "Motor-" + (idx.isEmpty() ? "1" : idx);
}

void MotorItem::setMotorType(const QString& type) {
    m_motorType = type;
    update();
}

void MotorItem::setCurrentAngle(double angle) {
    m_currentAngle = angle;
    update();
}

QRectF MotorItem::boundingRect() const {
    return QRectF(-40, -40, 80, 80);
}

void MotorItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget*) {
    painter->setRenderHint(QPainter::Antialiasing);

    // Selection Glow
    if (option->state & QStyle::State_Selected) {
        painter->setPen(QPen(QColor(99, 102, 241, 150), 2.5, Qt::SolidLine));
        painter->setBrush(Qt::NoBrush);
        painter->drawRoundedRect(-27, -27, 54, 44, 6, 6);
    }

    // Metal leads — extended to reach 10px-aligned pins at y=30
    painter->setPen(QPen(QColor(156, 163, 175), 2));
    painter->drawLine(-20, 15, -20, 30);
    painter->drawLine(  0, 15,   0, 30);
    painter->drawLine( 20, 15,  20, 30);

    // Gold pads
    painter->setBrush(QColor(234, 179, 8));
    painter->setPen(QPen(QColor(161, 98, 7), 1));
    painter->drawEllipse(QPointF(-20, 30), 3, 3);
    painter->drawEllipse(QPointF(  0, 30), 3, 3);
    painter->drawEllipse(QPointF( 20, 30), 3, 3);

    // Pin labels — moved down to y=34 to clear pads
    QFont pinFont = painter->font();
    pinFont.setPointSize(5);
    pinFont.setBold(true);
    painter->setFont(pinFont);
    
    painter->setPen(QColor(75, 85, 99)); // GND color
    painter->drawText(QRectF(-30, 34, 20, 10), Qt::AlignCenter, "GND");
    
    painter->setPen(QColor(239, 68, 68)); // VCC color
    painter->drawText(QRectF(-10, 34, 20, 10), Qt::AlignCenter, "VCC");
    
    painter->setPen(QColor(59, 130, 246)); // PWM color
    painter->drawText(QRectF(10, 34, 20, 10), Qt::AlignCenter, "PWM");

    // Motor Base (Premium Brushed Aluminum look)
    QLinearGradient baseGrad(-25, -25, 25, 15);
    baseGrad.setColorAt(0.0, QColor(241, 245, 249)); // light gray
    baseGrad.setColorAt(1.0, QColor(203, 213, 225)); // slate
    painter->setBrush(baseGrad);
    painter->setPen(QPen(QColor(148, 163, 184), 1));
    painter->drawRoundedRect(-25, -25, 50, 40, 5, 5);

    // Motor Type Label engraved on body
    painter->setPen(QColor(100, 116, 139));
    QFont font = painter->font();
    font.setPointSize(6);
    font.setBold(true);
    painter->setFont(font);
    
    QString typeLabel = "SERVO";
    if (m_motorType == "dc") typeLabel = "MOTOR DC";
    else if (m_motorType == "stepper") typeLabel = "STEPPER";
    painter->drawText(QRectF(-25, 2, 50, 15), Qt::AlignCenter, typeLabel);

    // Rotating Shaft / Hélice
    painter->save();
    painter->translate(0, -10); // Center of the shaft
    
    // Animate based on current angle
    painter->rotate(m_currentAngle);

    if (m_motorType.contains("servo")) {
        // Draw a typical servo horn (cross shape in white plastic)
        painter->setBrush(QColor(255, 255, 255));
        painter->setPen(QPen(QColor(148, 163, 184), 1));
        painter->drawRoundedRect(-2, -12, 4, 24, 2, 2);
        painter->drawRoundedRect(-12, -2, 24, 4, 2, 2);
        
        // Center screw
        painter->setBrush(QColor(148, 163, 184));
        painter->setPen(Qt::NoPen);
        painter->drawEllipse(QPointF(0, 0), 2, 2);
    } else {
        // Draw a gear/shaft for DC/Stepper
        painter->setBrush(QColor(226, 232, 240));
        painter->setPen(QPen(QColor(148, 163, 184), 1));
        painter->drawEllipse(QPointF(0, 0), 8, 8);
        
        // Red dot to clearly show rotation speed/angle
        painter->setBrush(QColor(239, 68, 68));
        painter->setPen(Qt::NoPen);
        painter->drawEllipse(QPointF(0, -5), 2, 2);
    }
    painter->restore();

    // Component Name Label (below the body)
    painter->setPen(QColor(100, 116, 139));
    QFont fNameM = painter->font();
    fNameM.setPointSize(5);
    fNameM.setBold(true);
    painter->setFont(fNameM);
    painter->drawText(QRectF(-40, 20, 80, 10), Qt::AlignCenter, m_name.toUpper());
}


// =======================================================
// BESS ITEM IMPLEMENTATION
// =======================================================
BessItem::BessItem(const QString& id, const QString& name, QGraphicsItem* parent)
    : ComponentItem(id, name, "bess", parent), m_chargeLevel(100.0) {
    // VCC pin (red wire) exits from right side, bottom — 10px aligned
    m_pins.append({"VCC",   QPointF(60,  20), false, "", "", QColor(239, 68, 68)});
    // GND pin (black wire) exits from right side, top
    m_pins.append({"GND",   QPointF(60, -20), false, "", "", QColor(30, 30, 30)});
}

void BessItem::setChargeLevel(double level) {
    m_chargeLevel = qBound(0.0, level, 100.0);
    update();
}

QRectF BessItem::boundingRect() const {
    // Expanded to accommodate 10px-aligned wire leads at x=60
    return QRectF(-35, -55, 100, 110);
}

void BessItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget*) {
    painter->setRenderHint(QPainter::Antialiasing);

    // Selected state glow
    if (option->state & QStyle::State_Selected) {
        painter->setPen(QPen(QColor(99, 102, 241, 150), 2.5, Qt::SolidLine));
        painter->setBrush(Qt::NoBrush);
        painter->drawRoundedRect(-28, -53, 56, 106, 5, 5);
    }

    // --- BATTERY METAL CAPS (Flat Vector Style) ---
    QLinearGradient capGrad(-10, 0, 10, 0);
    capGrad.setColorAt(0.0, QColor(203, 213, 225));
    capGrad.setColorAt(1.0, QColor(148, 163, 184));

    // Positive Cap (Bottom)
    painter->setPen(QPen(QColor(100, 116, 139), 1));
    painter->setBrush(capGrad);
    painter->drawRoundedRect(-8, 44, 16, 6, 1, 1);

    // Negative Cap (Top)
    painter->drawRoundedRect(-12, -50, 24, 6, 1, 1);

    // --- BATTERY BODY (Flat Vector Style) ---
    painter->setPen(QPen(QColor(29, 78, 216), 2));
    painter->setBrush(QColor(37, 99, 235));
    painter->drawRoundedRect(-25, -45, 50, 90, 5, 5);

    // --- SILK SCREEN DECORATIONS ON WRAP ---
    painter->setPen(QColor(191, 219, 254));
    QFont fL = painter->font();
    fL.setPointSize(6);
    fL.setBold(true);
    fL.setFamily("Segoe UI");
    painter->setFont(fL);
    painter->drawText(QRectF(-25, -40, 50, 12), Qt::AlignCenter, "BESS");

    // --- FLAT CHARGE VISUALIZER ---
    // Clean battery outline
    painter->setPen(QPen(QColor(255, 255, 255, 200), 1.5));
    painter->setBrush(Qt::NoBrush);
    painter->drawRoundedRect(-12, -26, 24, 42, 3, 3);
    painter->drawRect(-5, -29, 10, 3); // top cap of outline

    // Flat fill based on level
    painter->setPen(Qt::NoPen);
    double fillH = (m_chargeLevel / 100.0) * 38.0;
    QColor chargeColor = m_chargeLevel > 20 ? QColor(34, 197, 94) : QColor(239, 68, 68);
    painter->setBrush(chargeColor);
    painter->drawRoundedRect(-10, 14 - fillH, 20, fillH, 1, 1);

    // Digital percentage
    painter->setPen(Qt::white);
    QFont f = painter->font();
    f.setPointSize(7.5);
    f.setBold(true);
    f.setFamily("Segoe UI");
    painter->setFont(f);
    painter->drawText(QRectF(-25, 20, 50, 15), Qt::AlignCenter, QString("%1%").arg(static_cast<int>(m_chargeLevel)));

    // --- WIRE LEADS AND PHYSICAL PINS ---
    // 1. Red Wire (VCC) to Pin (60, 20)
    painter->setPen(QPen(QColor(239, 68, 68), 2, Qt::SolidLine, Qt::RoundCap));
    painter->drawLine(QPointF(25, 20), QPointF(60, 20));

    // 2. Black Wire (GND) to Pin (60, -20)
    painter->setPen(QPen(QColor(55, 65, 81), 2, Qt::SolidLine, Qt::RoundCap));
    painter->drawLine(QPointF(25, -20), QPointF(60, -20));

    // Render connection header pins
    QPointF pinPos[] = { QPointF(60, -20), QPointF(60, 20) };
    for (const auto& pt : pinPos) {
        // Outer copper pad (Gold)
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(218, 165, 32));
        painter->drawEllipse(pt, 4.5, 4.5);
        
        // Inner hole / Silver header pin
        painter->setPen(QPen(QColor(50, 50, 50), 0.5));
        painter->setBrush(QColor(230, 230, 230));
        painter->drawEllipse(pt, 2.5, 2.5);
        
        // Reflection
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(255, 255, 255, 150));
        painter->drawEllipse(QPointF(pt.x() - 0.5, pt.y() - 0.5), 1.0, 1.0);
    }

    // --- LABELS ON WIRE LEADS ---
    painter->setPen(QColor(239, 68, 68));
    QFont fWire = painter->font();
    fWire.setPointSize(6.5);
    fWire.setBold(true);
    painter->setFont(fWire);
    painter->drawText(QRectF(30, 22, 24, 10), Qt::AlignCenter, "+");

    painter->setPen(QColor(156, 163, 175));
    painter->drawText(QRectF(30, -32, 24, 10), Qt::AlignCenter, "-");
}

// =======================================================
// BESS CHARGER ITEM IMPLEMENTATION
// =======================================================
BessChargerItem::BessChargerItem(const QString& id, const QString& name, QGraphicsItem* parent)
    : ComponentItem(id, name, "bess_charger", parent), m_isPluggedIn(false) {
    // Inputs (Solar/USB) — 10px aligned at x=-40
    m_pins.append({"IN+",  QPointF(-40, -20), false, "", "", QColor(239, 68, 68)});
    m_pins.append({"IN-",  QPointF(-40,  20), false, "", "", QColor(75, 85, 99)});
    
    // Battery connections — 10px aligned at x=40
    m_pins.append({"BAT+", QPointF(40, -10), false, "", "", QColor(239, 68, 68)});
    m_pins.append({"BAT-", QPointF(40,  10), false, "", "", QColor(75, 85, 99)});
    
    // Output loads — 10px aligned at x=40
    m_pins.append({"OUT+", QPointF(40, -30), false, "", "", QColor(245, 158, 11)});
    m_pins.append({"OUT-", QPointF(40,  30), false, "", "", QColor(107, 114, 128)});
}

void BessChargerItem::setPluggedIn(bool plugged) {
    m_isPluggedIn = plugged;
    update();
}

QRectF BessChargerItem::boundingRect() const {
    return QRectF(-45, -35, 90, 75);
}

void BessChargerItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget*) {
    painter->setRenderHint(QPainter::Antialiasing);

    if (option->state & QStyle::State_Selected) {
        painter->setPen(QPen(QColor(99, 102, 241, 150), 3, Qt::SolidLine));
        painter->setBrush(Qt::NoBrush);
        painter->drawRoundedRect(-48, -38, 96, 78, 5, 5);
    }

    // Premium Blue PCB Body (TP4056 Type-C style)
    painter->setPen(QPen(QColor(15, 60, 110), 1.5));
    painter->setBrush(QColor(25, 90, 170));
    painter->drawRoundedRect(-45, -35, 90, 70, 4, 4);

    // PCB Traces Mock (for premium look)
    painter->setPen(QPen(QColor(40, 110, 190), 1));
    painter->drawLine(-30, -20, -10, -10);
    painter->drawLine(-30, 20, -10, 10);
    painter->drawLine(30, -10, 10, -5);
    painter->drawLine(30, 10, 10, 5);

    // TP4056 Main IC
    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor(30, 35, 45)); // Matte black
    painter->drawRoundedRect(-8, -12, 16, 12, 1, 1);
    
    // IC pins (silver)
    painter->setBrush(QColor(180, 185, 190));
    for (int i = 0; i < 4; i++) {
        painter->drawRect(-5 + i*3.5, -14, 1.5, 2); // Top pins
        painter->drawRect(-5 + i*3.5, 0, 1.5, 2);   // Bottom pins
    }
    
    // Protection ICs (DW01A & 8205A)
    painter->setBrush(QColor(30, 35, 45));
    painter->drawRoundedRect(-15, 10, 12, 8, 1, 1); // 8205A
    painter->drawRoundedRect(5, 12, 8, 5, 1, 1);    // DW01A
    
    // Type-C USB Port mock
    painter->setBrush(QColor(210, 215, 220)); // Silver metal
    painter->setPen(QPen(QColor(130, 140, 150), 1));
    painter->drawRoundedRect(-48, -8, 12, 16, 3, 3); // Extends out slightly on the left
    // Inner port black piece
    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor(20, 20, 20));
    painter->drawRoundedRect(-48, -4, 4, 8, 1, 1);

    // Draw connection holes & metallic male header pins at exactly the m_pins coordinates!
    QPointF pinPos[] = {
        QPointF(-40, -20), QPointF(-40, 20),
        QPointF(40, -10), QPointF(40, 10),
        QPointF(40, -30), QPointF(40, 30)
    };
    for (const auto& pt : pinPos) {
        // Outer copper pad (Gold/Silver)
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(218, 165, 32)); // Golden pad
        painter->drawEllipse(pt, 4.5, 4.5);
        
        // Inner hole / Male header pin inserted
        painter->setPen(QPen(QColor(50, 50, 50), 0.5));
        painter->setBrush(QColor(230, 230, 230)); // Silver header pin
        painter->drawEllipse(pt, 2.5, 2.5);
        
        // Pin highlight/reflection for 3D effect
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(255, 255, 255, 150));
        painter->drawEllipse(QPointF(pt.x() - 0.5, pt.y() - 0.5), 1.0, 1.0);
    }

    // Text labels for pins
    painter->setPen(Qt::white);
    QFont f2 = painter->font();
    f2.setPointSize(5);
    f2.setBold(true);
    painter->setFont(f2);
    
    // Left side labels (IN+, IN-)
    painter->drawText(QRectF(-33, -25, 20, 10), Qt::AlignLeft | Qt::AlignVCenter, "IN+");
    painter->drawText(QRectF(-33, 15, 20, 10), Qt::AlignLeft | Qt::AlignVCenter, "IN-");
    
    // Right side labels (B+, B-, OUT+, OUT-)
    painter->drawText(QRectF(15, -15, 18, 10), Qt::AlignRight | Qt::AlignVCenter, "B+");
    painter->drawText(QRectF(15, 5, 18, 10), Qt::AlignRight | Qt::AlignVCenter, "B-");
    painter->drawText(QRectF(15, -35, 18, 10), Qt::AlignRight | Qt::AlignVCenter, "OUT+");
    painter->drawText(QRectF(15, 25, 18, 10), Qt::AlignRight | Qt::AlignVCenter, "OUT-");

    // LEDs
    if (m_isPluggedIn) {
        // Red LED (Charging)
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(239, 68, 68));
        painter->drawEllipse(QPointF(-10, -22), 2.5, 2.5);
        // Glow
        painter->setBrush(QColor(239, 68, 68, 120));
        painter->drawEllipse(QPointF(-10, -22), 5, 5);
        
        // Blue LED off
        painter->setBrush(QColor(30, 40, 80));
        painter->drawEllipse(QPointF(0, -22), 2.0, 2.0);
    } else {
        // Red LED off
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(80, 20, 20));
        painter->drawEllipse(QPointF(-10, -22), 2.0, 2.0);

        // Blue LED (Standby/Full)
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(59, 130, 246));
        painter->drawEllipse(QPointF(0, -22), 2.5, 2.5);
        // Glow
        painter->setBrush(QColor(59, 130, 246, 120));
        painter->drawEllipse(QPointF(0, -22), 5, 5);
    }

    // Title / Silk Screen Name
    painter->setPen(QColor(200, 210, 220));
    QFont f3 = painter->font();
    f3.setPointSize(6);
    f3.setBold(true);
    painter->setFont(f3);
    painter->drawText(QRectF(-30, 22, 60, 10), Qt::AlignCenter, "TP4056");
}

void BessChargerItem::contextMenuEvent(QGraphicsSceneContextMenuEvent* event) {
    QMenu menu;
    QAction* toggleAct = menu.addAction(m_isPluggedIn ? "Desconectar Energia (Tomada)" : "Ligar na Tomada (5V)");
    QAction* selected = menu.exec(event->screenPos());
    if (selected == toggleAct) {
        setPluggedIn(!m_isPluggedIn);
    }
    // Call parent so it also emits rightClicked if needed, but here we consumed it.
    event->accept();
}

// =======================================================
// DHT22 SENSOR ITEM IMPLEMENTATION
// =======================================================
DHT22Item::DHT22Item(const QString& id, const QString& name, QGraphicsItem* parent)
    : ComponentItem(id, name, "dht22", parent), m_humidity(50.0), m_temperature(25.0) {
    m_pins.append({"VCC",  QPointF(-20,  50), false, "", "", QColor(239, 68, 68)});
    m_pins.append({"DATA", QPointF(-10,  50), true,  "", "", QColor(245, 158, 11)});
    m_pins.append({"NC",   QPointF(  0,  50), false, "", "", QColor(156, 163, 175)});
    m_pins.last().generateCode = false; // NC has no code generation
    m_pins.append({"GND",  QPointF( 10,  50), false, "", "", QColor(75, 85, 99)});
    
    QString idx = extractIndexFromId(id);
    m_name = QString("DHT22-%1 (U:50%, T:25ºC)").arg(idx.isEmpty() ? "1" : idx);
}

void DHT22Item::setHumidity(double h) {
    m_humidity = qMax(0.0, qMin(100.0, h));
    QString idx = extractIndexFromId(m_id);
    m_name = QString("DHT22-%1 (U:%2%, T:%3ºC)").arg(idx.isEmpty()?"1":idx).arg(static_cast<int>(m_humidity)).arg(static_cast<int>(m_temperature));
    update();
}

void DHT22Item::setTemperature(double t) {
    m_temperature = qMax(-40.0, qMin(80.0, t));
    QString idx = extractIndexFromId(m_id);
    m_name = QString("DHT22-%1 (U:%2%, T:%3ºC)").arg(idx.isEmpty()?"1":idx).arg(static_cast<int>(m_humidity)).arg(static_cast<int>(m_temperature));
    update();
}

void DHT22Item::wheelEvent(QGraphicsSceneWheelEvent* event) {
    double delta = event->delta() > 0 ? 1.0 : -1.0;
    if (event->pos().x() < 0) {
        setHumidity(m_humidity + delta * 2.0);
    } else {
        setTemperature(m_temperature + delta * 1.0);
    }
    emit valueChanged();
    event->accept();
}

QRectF DHT22Item::boundingRect() const {
    return QRectF(-45, -50, 90, 115);
}

void DHT22Item::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget*) {
    painter->setRenderHint(QPainter::Antialiasing);

    if (option->state & QStyle::State_Selected) {
        painter->setPen(QPen(QColor(99, 102, 241, 150), 3, Qt::SolidLine));
        painter->setBrush(Qt::NoBrush);
        painter->drawRoundedRect(-43, -48, 86, 96, 6, 6);
    }

    // Chassis Body (Premium Emerald Green representing a modern DHT22/electronic sensor)
    painter->setPen(QPen(QColor(4, 120, 87), 2)); // dark green border
    painter->setBrush(QColor(16, 185, 129)); // emerald green body
    painter->drawRoundedRect(-40, -45, 80, 90, 6, 6);

    // Front sensor slots / grid pattern (typical of DHT22 white/blue shell, but here in styled colors)
    painter->setPen(QPen(QColor(4, 120, 87, 80), 1));
    painter->setBrush(QColor(6, 95, 70, 40));
    painter->drawRoundedRect(-30, -35, 60, 40, 4, 4);

    // Horizontal & vertical grille slots
    painter->setPen(QPen(QColor(16, 185, 129, 150), 1));
    for (int y = -30; y <= -5; y += 8) {
        painter->drawLine(-25, y, 25, y);
    }
    for (int x = -20; x <= 20; x += 10) {
        painter->drawLine(x, -30, x, -5);
    }

    // Elegant text labels on body
    painter->setPen(Qt::white);
    QFont f = painter->font();
    f.setPointSize(7);
    f.setBold(true);
    painter->setFont(f);
    
    QString baseName = m_id.toUpper();
    // Transformar dht22-1 em DHT22-1
    if (baseName.contains("DHT22")) baseName = "DHT22-" + extractIndexFromId(m_id);
    
    painter->drawText(QRectF(-40, 3, 80, 12), Qt::AlignCenter, baseName);

    // Value readings shown prominently below the sensor label (no dials)
    QFont fSub = painter->font();
    fSub.setPointSize(6);
    fSub.setBold(true);
    painter->setFont(fSub);
    painter->setPen(QColor(209, 250, 229)); // light mint green
    painter->drawText(QRectF(-40, 17, 80, 12), Qt::AlignCenter, QString("%1%  |  %2°C").arg(static_cast<int>(m_humidity)).arg(static_cast<int>(m_temperature)));

    // Small U: / T: labels above values
    QFont fLabel2 = painter->font();
    fLabel2.setPointSize(4.5);
    fLabel2.setBold(false);
    painter->setFont(fLabel2);
    painter->setPen(QColor(167, 243, 208));
    painter->drawText(QRectF(-40, 12, 35, 7), Qt::AlignCenter, "Umidade");
    painter->drawText(QRectF(5, 12, 35, 7), Qt::AlignCenter, "Temp");

    // Draw the connection pad circles (bolinhas) on the board (at Y = 45)
    for (const auto& pin : m_pins) {
        painter->setBrush(pin.color.isValid() ? pin.color : QColor(234, 179, 8));
        QColor borderCol = pin.color.isValid() ? pin.color.darker(115) : QColor(161, 98, 7);
        painter->setPen(QPen(borderCol, 1));
        painter->drawEllipse(pin.localPos, 3.5, 3.5);
    }

    // 3. Vertical text labels on the PCB above the pins
    QStringList pinNames = {"VCC", "DATA", "NC", "GND"};
    QVector<double> pinX = {-20, -10, 0, 10};
    QFont fLabel = painter->font();
    fLabel.setPointSize(4.5);
    fLabel.setBold(true);
    painter->setFont(fLabel);
    painter->setPen(QColor(209, 250, 229)); // light soft mint blue
    for (int i = 0; i < 4; ++i) {
        painter->save();
        painter->translate(pinX[i], 42); // pivot near Y = 42 above the pins (aligned to 50px pin position)
        painter->rotate(-90);
        painter->drawText(QRectF(-15, -4, 15, 8), Qt::AlignRight | Qt::AlignVCenter, pinNames[i]);
        painter->restore();
    }
}

// =======================================================
// HC-SR04 SENSOR ITEM IMPLEMENTATION
// =======================================================
HCSR04Item::HCSR04Item(const QString& id, const QString& name, QGraphicsItem* parent)
    : ComponentItem(id, name, "hcsr04", parent), m_distance(100.0) {
    m_pins.append({"VCC",  QPointF(-20,  30), false, "", "", QColor(239, 68, 68)});
    m_pins.append({"TRIG", QPointF(-10,  30), false, "", "", QColor(245, 158, 11)});
    m_pins.append({"ECHO", QPointF(  0,  30), true,  "", "", QColor(59, 130, 246)});
    m_pins.append({"GND",  QPointF( 10,  30), false, "", "", QColor(75, 85, 99)});
    
    QString idx = extractIndexFromId(id);
    m_name = "HC-SR04-" + (idx.isEmpty() ? "1" : idx);
}

void HCSR04Item::setDistance(double d) {
    m_distance = qMax(2.0, qMin(400.0, d));
    update();
}

void HCSR04Item::wheelEvent(QGraphicsSceneWheelEvent* event) {
    double delta = event->delta() > 0 ? 5.0 : -5.0;
    setDistance(m_distance + delta);
    emit valueChanged();
    event->accept();
}

QRectF HCSR04Item::boundingRect() const {
    return QRectF(-55, -35, 110, 85);
}

void HCSR04Item::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget*) {
    painter->setRenderHint(QPainter::Antialiasing);

    if (option->state & QStyle::State_Selected) {
        painter->setPen(QPen(QColor(99, 102, 241, 150), 3, Qt::SolidLine));
        painter->setBrush(Qt::NoBrush);
        painter->drawRoundedRect(-53, -33, 106, 66, 6, 6);
    }

    // Board Body (Premium Royal Blue for HC-SR04 PCB)
    painter->setPen(QPen(QColor(29, 78, 216), 2)); // dark blue border
    painter->setBrush(QColor(37, 99, 235)); // royal blue body
    painter->drawRoundedRect(-50, -30, 100, 60, 6, 6);

    // Two big ultrasonic grilles/lenses (T and R cylinders)
    // Left eye (Trigger / T)
    painter->setPen(QPen(QColor(148, 163, 184), 2)); // silver metal rim
    QLinearGradient rimL(-41, -16, -9, 16);
    rimL.setColorAt(0.0, QColor(226, 232, 240));
    rimL.setColorAt(1.0, QColor(100, 116, 139));
    painter->setBrush(rimL);
    painter->drawEllipse(QPointF(-25, 0), 16, 16);

    // Right eye (Echo / R)
    painter->setPen(QPen(QColor(148, 163, 184), 2));
    QLinearGradient rimR(9, -16, 41, 16);
    rimR.setColorAt(0.0, QColor(226, 232, 240));
    rimR.setColorAt(1.0, QColor(100, 116, 139));
    painter->setBrush(rimR);
    painter->drawEllipse(QPointF(25, 0), 16, 16);

    // Dark mesh inner circles for both transducers
    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor(15, 23, 42)); // dark slate/black inner
    painter->drawEllipse(QPointF(-25, 0), 13, 13);
    painter->drawEllipse(QPointF(25, 0), 13, 13);

    // Fine grid mesh pattern on the transducer eyes
    painter->setPen(QPen(QColor(71, 85, 105, 120), 1));
    for (int y = -12; y <= 12; y += 4) {
        // Left eye lines
        double wL = sqrt(169.0 - y*y);
        painter->drawLine(-25.0 - wL, y, -25.0 + wL, y);
        // Right eye lines
        painter->drawLine(25.0 - wL, y, 25.0 + wL, y);
    }
    for (int x = -12; x <= 12; x += 4) {
        // Left eye lines
        double hL = sqrt(169.0 - x*x);
        painter->drawLine(-25.0 + x, -hL, -25.0 + x, hL);
        // Right eye lines
        painter->drawLine(25.0 + x, -hL, 25.0 + x, hL);
    }

    // Letters 'T' and 'R' on the cylinders
    painter->setPen(QColor(241, 245, 249, 200));
    QFont fT = painter->font();
    fT.setPointSize(8);
    fT.setBold(true);
    painter->setFont(fT);
    painter->drawText(QRectF(-35, -8, 20, 16), Qt::AlignCenter, "T");
    painter->drawText(QRectF(15, -8, 20, 16), Qt::AlignCenter, "R");

    // Elegant text labels on PCB body
    painter->setPen(Qt::white);
    QFont f = painter->font();
    f.setPointSize(6);
    f.setBold(true);
    painter->setFont(f);
    painter->drawText(QRectF(-50, -29, 100, 8), Qt::AlignCenter, m_name.toUpper());

    // Draw active distance value reading in light blue text
    QFont fSub = painter->font();
    fSub.setPointSize(5);
    fSub.setBold(false);
    painter->setFont(fSub);
    painter->setPen(QColor(191, 219, 254)); // light soft blue
    painter->drawText(QRectF(-30, -20, 60, 8), Qt::AlignCenter, QString("%1 cm").arg(static_cast<int>(m_distance)));

    // (Dial/potentiometer removed — no knob on HC-SR04)

    // Draw the connection pad circles (bolinhas) on the board (at Y = 30)
    for (const auto& pin : m_pins) {
        painter->setBrush(pin.color.isValid() ? pin.color : QColor(234, 179, 8));
        QColor borderCol = pin.color.isValid() ? pin.color.darker(115) : QColor(161, 98, 7);
        painter->setPen(QPen(borderCol, 1));
        painter->drawEllipse(pin.localPos, 3.5, 3.5);
    }

    // 3. Vertical text labels on the PCB above the pins
    QStringList pinNames = {"VCC", "TRIG", "ECHO", "GND"};
    QVector<double> pinX = {-20, -10, 0, 10};
    QFont fLabel = painter->font();
    fLabel.setPointSize(4.5);
    fLabel.setBold(true);
    painter->setFont(fLabel);
    painter->setPen(QColor(191, 219, 254)); // light soft blue
    for (int i = 0; i < 4; ++i) {
        painter->save();
        painter->translate(pinX[i], 22); // pivot near Y = 22 above the pins
        painter->rotate(-90);
        painter->drawText(QRectF(-15, -4, 15, 8), Qt::AlignRight | Qt::AlignVCenter, pinNames[i]);
        painter->restore();
    }
}


// ---------------------------------------------------------
// Ground (Terra) Item
// ---------------------------------------------------------

GndItem::GndItem(const QString& id, const QString& name, QGraphicsItem* parent)
    : ComponentItem(id, name, "gnd", parent) {
    m_pins.push_back({"GND", QPointF(0, 0), false, "", "", QColor(75, 85, 99), false});
}

QRectF GndItem::boundingRect() const {
    return QRectF(-15, -5, 30, 35);
}

void GndItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) {
    Q_UNUSED(option);
    Q_UNUSED(widget);
    painter->setRenderHint(QPainter::Antialiasing);

    // Pin
    painter->setPen(QPen(QColor(75, 85, 99), 1.5));
    painter->setBrush(QColor(229, 231, 235));
    painter->drawEllipse(m_pins[0].localPos, 3.5, 3.5);

    // GND Symbol
    painter->setPen(QPen(QColor(50, 50, 50), 2.0, Qt::SolidLine, Qt::RoundCap));
    painter->drawLine(0, 4, 0, 12);
    painter->drawLine(-10, 12, 10, 12);
    painter->drawLine(-6, 16, 6, 16);
    painter->drawLine(-2, 20, 2, 20);
}




void ResistorItem::updateLayoutForSMD(const QString& smdSize) {
    prepareGeometryChange();
    double w = 32.0, h = 16.0;
    if (smdSize == "0402") { w = 10; h = 5; }
    else if (smdSize == "0603") { w = 16; h = 8; }
    else if (smdSize == "0805") { w = 20; h = 12.5; }
    else if (smdSize == "1206") { w = 32; h = 16; }
    
    m_pins[0].localPos = QPointF(-w * 0.4, 0);
    m_pins[1].localPos = QPointF(w * 0.4, 0);
    update();
}

void CapacitorItem::updateLayoutForSMD(const QString& smdSize) {
    prepareGeometryChange();
    double w = 32.0, h = 16.0;
    if (smdSize == "0402") { w = 10; h = 5; }
    else if (smdSize == "0603") { w = 16; h = 8; }
    else if (smdSize == "0805") { w = 20; h = 12.5; }
    else if (smdSize == "1206") { w = 32; h = 16; }
    
    m_pins[0].localPos = QPointF(-w * 0.4, 0);
    m_pins[1].localPos = QPointF(w * 0.4, 0);
    update();
}

void LEDItem::updateLayoutForSMD(const QString& smdSize) {
    prepareGeometryChange();
    double w = 20.0, h = 12.5;
    if (smdSize == "0603") { w = 16; h = 8; }
    else if (smdSize == "0805") { w = 20; h = 12.5; }
    else if (smdSize == "1206") { w = 32; h = 16; }
    else if (smdSize == "5050") { w = 50; h = 50; }
    
    m_pins[0].localPos = QPointF(-w * 0.425, 0);
    m_pins[1].localPos = QPointF(w * 0.425, 0);
    update();
}
