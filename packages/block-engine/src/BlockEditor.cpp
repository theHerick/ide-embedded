#include "BlockEditor.h"
#include "CustomComponent.h"
#include <QUuid>
#include <QListWidgetItem>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QPushButton>
#include <QDialog>
#include <QTextBrowser>
#include <QSpinBox>
#include <QStyle>
#include <QPalette>
#include <QHeaderView>
#include <QSplitter>
#include <QLineEdit>
#include <QCompleter>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QEvent>
#include <QTimer>
#include <QDropEvent>
#include <QDragEnterEvent>
#include <QMimeData>
#include <QPointer>
#include <QPainter>
#include <QPainterPath>
#include <QInputDialog>
#include <QScrollArea>
#include <QRegularExpression>

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

VariableSlotEdit::VariableSlotEdit(QWidget* parent) : QLineEdit(parent) {
    setAcceptDrops(true);
    setPlaceholderText("Arraste uma variável...");
    
    auto updateStyle = [this]() {
        QString txt = text().trimmed();
        if (txt.isEmpty()) {
            setStyleSheet(
                "QLineEdit { "
                "  background: #FBFCFD; "
                "  border: 1.5px dashed #CBD5E1; "
                "  border-radius: 10px; "
                "  color: #64748B; "
                "  font-size: 11px; "
                "  font-weight: 600; "
                "  padding: 4px 10px; "
                "}"
            );
        } else {
            // Style like Sketchware's Green Operator Block if it has mathematical or logical operators
            bool isOp = txt.contains("+") || txt.contains("-") || txt.contains("*") || txt.contains("/") ||
                        txt.contains(">") || txt.contains("<") || txt.contains("=") || txt.contains("!") ||
                        txt.contains("&") || txt.contains("|");
            if (isOp) {
                setStyleSheet(
                    "QLineEdit { "
                    "  background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #10B981, stop:1 #059669); " // Emerald gradient
                    "  border: 1.5px solid #047857; "
                    "  border-radius: 12px; " // Capsule shape
                    "  color: #FFFFFF; "
                    "  font-size: 11px; "
                    "  font-weight: bold; "
                    "  padding: 4px 10px; "
                    "}"
                );
            } else {
                // Style like a variable pill (Blue theme)
                setStyleSheet(
                    "QLineEdit { "
                    "  background: #EFF6FF; "
                    "  border: 1.5px solid #93C5FD; "
                    "  border-radius: 10px; "
                    "  color: #1D4ED8; "
                    "  font-size: 11px; "
                    "  font-weight: bold; "
                    "  padding: 4px 10px; "
                    "}"
                );
            }
        }
    };
    
    connect(this, &QLineEdit::textChanged, this, updateStyle);
    updateStyle();
}

void VariableSlotEdit::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasText()) {
        event->acceptProposedAction();
        setStyleSheet(
            "QLineEdit { "
            "  background: #FFFFFF; "
            "  border: 1.5px solid #34D399; "
            "  border-radius: 10px; "
            "  color: #059669; "
            "  font-size: 11px; "
            "  font-weight: bold; "
            "  padding: 4px 10px; "
            "}"
        );
    } else {
        QLineEdit::dragEnterEvent(event);
    }
}

void VariableSlotEdit::dropEvent(QDropEvent* event) {
    if (event->mimeData()->hasText()) {
        QString dropped = event->mimeData()->text().trimmed();
        QString current = text().trimmed();
        
        bool isSerialSlot = property("isSerial").toBool();
        if (isSerialSlot) {
            if (current.isEmpty()) {
                setText(dropped);
            } else {
                if (current.endsWith("<<") || current.endsWith("<< ")) {
                    if (current.endsWith(" ")) {
                        setText(text() + dropped);
                    } else {
                        setText(text() + " " + dropped);
                    }
                } else {
                    setText(text() + " << " + dropped);
                }
            }
        } else {
            if (current.isEmpty()) {
                setText(dropped);
            } else {
                // Smart concatenation of variables and operators
                bool isOp = (dropped == "+" || dropped == "-" || dropped == "*" || dropped == "/" || 
                             dropped == ">" || dropped == "<" || dropped == "==" || dropped == "!=" ||
                             dropped == "&&" || dropped == "||" || dropped == "!");
                if (isOp) {
                    setText(current + " " + dropped + " ");
                } else {
                    bool endsWithOp = (current.endsWith("+") || current.endsWith("-") || current.endsWith("*") || current.endsWith("/") ||
                                       current.endsWith(">") || current.endsWith("<") || current.endsWith("=") || current.endsWith("!") ||
                                       current.endsWith("&") || current.endsWith("|"));
                    if (endsWithOp) {
                        setText(text() + " " + dropped);
                    } else {
                        setText(text() + " " + dropped);
                    }
                }
            }
        }
        emit variableDropped(text());
        event->acceptProposedAction();
    } else {
        QLineEdit::dropEvent(event);
    }
}


class SketchwareBlockWidget : public QWidget {
public:
    explicit SketchwareBlockWidget(const QColor& color, bool isHat = false, bool isFim = false, QWidget* parent = nullptr)
        : QWidget(parent), m_color(color), m_isHatBlock(isHat), m_isFimBlock(isFim), m_hovered(false) {
        setAttribute(Qt::WA_StyledBackground, false);
    }

    void setColor(const QColor& color) {
        m_color = color;
        update();
    }

protected:
    bool event(QEvent* e) override {
        if (e->type() == QEvent::Enter) {
            m_hovered = true;
            update();
        } else if (e->type() == QEvent::Leave) {
            m_hovered = false;
            update();
        }
        return QWidget::event(e);
    }

    void mouseDoubleClickEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton && !m_isHatBlock) {
            // Find our index in the BlockEditor
            QWidget* p = parentWidget();
            while (p) {
                BlockEditor* editor = qobject_cast<BlockEditor*>(p);
                if (editor) {
                    // We need to find which row we are in the list
                    QListWidget* list = editor->findChild<QListWidget*>("blockListWidget");
                    if (list) {
                        for (int i = 0; i < list->count(); ++i) {
                            if (list->itemWidget(list->item(i)) == this || 
                                list->itemWidget(list->item(i)) == parentWidget()) {
                                list->setCurrentRow(i);
                                editor->removeSelectedBlock();
                                return;
                            }
                        }
                    }
                }
                p = p->parentWidget();
            }
        }
        QWidget::mouseDoubleClickEvent(event);
    }

    void mousePressEvent(QMouseEvent* event) override {
        // Propagate focus and selection to parent QListWidget
        QWidget* p = parentWidget();
        while (p) {
            QListWidget* list = qobject_cast<QListWidget*>(p);
            if (list) {
                list->setFocus(Qt::MouseFocusReason);
                for (int i = 0; i < list->count(); ++i) {
                    if (list->itemWidget(list->item(i)) == this || 
                        list->itemWidget(list->item(i)) == parentWidget()) {
                        list->setCurrentRow(i);
                        list->item(i)->setSelected(true);
                        break;
                    }
                }
                break;
            }
            p = p->parentWidget();
        }
        QWidget::mousePressEvent(event);
    }

    void paintEvent(QPaintEvent* event) override {
        Q_UNUSED(event);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        int W = width();
        int H = height();

        // Determine if this block is selected or is the current row in the QListWidget
        bool isSelected = false;
        QWidget* p = parentWidget();
        while (p) {
            QListWidget* list = qobject_cast<QListWidget*>(p);
            if (list) {
                for (int i = 0; i < list->count(); ++i) {
                    if (list->itemWidget(list->item(i)) == this || 
                        list->itemWidget(list->item(i)) == parentWidget()) {
                        isSelected = list->item(i)->isSelected() || list->currentRow() == i;
                        break;
                    }
                }
                break;
            }
            p = p->parentWidget();
        }

        QPainterPath path;
        if (m_isHatBlock) {
            // Hat Block: curved dome top, bottom projection tab
            path.moveTo(0, H - 12);
            path.quadTo(0, H - 6, 6, H - 6);
            path.lineTo(20, H - 6);
            path.lineTo(20, H);
            path.lineTo(36, H);
            path.lineTo(36, H - 6);
            path.lineTo(W - 6, H - 6);
            path.quadTo(W, H - 6, W, H - 12);
            path.lineTo(W, 24);
            path.quadTo(W / 2, 0, 0, 24);
            path.closeSubpath();
        } else if (m_isFimBlock) {
            // Fim Block: top notch cutout, bottom tab, rounded bottom-left corner
            path.moveTo(0, H - 20);
            path.quadTo(0, H - 6, 16, H - 6);
            path.lineTo(20, H - 6);
            path.lineTo(20, H);
            path.lineTo(36, H);
            path.lineTo(36, H - 6);
            path.lineTo(W - 6, H - 6);
            path.quadTo(W, H - 6, W, H - 12);
            path.lineTo(W, 12);
            path.quadTo(W, 6, W - 6, 6);
            path.lineTo(36, 6);
            path.lineTo(36, 12);
            path.lineTo(20, 12);
            path.lineTo(20, 6);
            path.lineTo(6, 6);
            path.quadTo(0, 6, 0, 12);
            path.closeSubpath();
        } else {
            // Standard Action / Control block: top notch cutout, bottom projection tab
            path.moveTo(0, H - 12);
            path.quadTo(0, H - 6, 6, H - 6);
            path.lineTo(20, H - 6);
            path.lineTo(20, H);
            path.lineTo(36, H);
            path.lineTo(36, H - 6);
            path.lineTo(W - 6, H - 6);
            path.quadTo(W, H - 6, W, H - 12);
            path.lineTo(W, 12);
            path.quadTo(W, 6, W - 6, 6);
            path.lineTo(36, 6);
            path.lineTo(36, 12);
            path.lineTo(20, 12);
            path.lineTo(20, 6);
            path.lineTo(6, 6);
            path.quadTo(0, 6, 0, 12);
            path.closeSubpath();
        }

        // Fill background with Frutiger Aero high-gloss linear gradient
        QColor baseColor = m_hovered ? m_color.lighter(112) : m_color;
        QLinearGradient grad(0, 0, 0, H);
        grad.setColorAt(0.0, baseColor.lighter(130));
        grad.setColorAt(0.45, baseColor.lighter(108));
        grad.setColorAt(0.46, baseColor.darker(105));
        grad.setColorAt(1.0, baseColor.darker(118));
        painter.fillPath(path, grad);

        // Draw a clean, premium border
        QColor borderColor = m_color.darker(125);
        double penWidth = 1.5;
        if (isSelected) {
            borderColor = QColor("#FFFFFF"); // Premium white selection highlight border
            penWidth = 2.5;
        }
        QPen pen(borderColor, penWidth);
        painter.setPen(pen);
        painter.drawPath(path);

        // Draw a subtle translucent white overlay at the top (top 3D highlight)
        painter.setPen(QPen(QColor(255, 255, 255, 110), 1.5));
        if (m_isHatBlock) {
            // Curve highlight
            QPainterPath highlightPath;
            highlightPath.moveTo(1, 24);
            highlightPath.quadTo(W / 2, 1, W - 1, 24);
            painter.drawPath(highlightPath);
        } else {
            // Top highlight along notch shape
            painter.drawLine(6, 7, 20, 7);
            painter.drawLine(20, 7, 20, 13);
            painter.drawLine(20, 13, 36, 13);
            painter.drawLine(36, 13, 36, 7);
            painter.drawLine(36, 7, W - 6, 7);
        }
    }

private:
    QColor m_color;
    bool m_isHatBlock;
    bool m_isFimBlock;
    bool m_hovered;
};

class CBracketSpineWidget : public QWidget {
public:
    explicit CBracketSpineWidget(const QColor& color, QWidget* parent = nullptr) 
        : QWidget(parent), m_color(color) {
        setFixedWidth(24);
    }
protected:
    void paintEvent(QPaintEvent* event) override {
        Q_UNUSED(event);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        // Draw a solid 12px vertical spine from x=6 to x=18
        QColor fillColor = m_color;
        QColor borderColor = m_color.darker(115);

        painter.fillRect(6, 0, 12, height(), fillColor);

        painter.setPen(QPen(borderColor, 1.5));
        painter.drawLine(6, 0, 6, height());
        painter.drawLine(18, 0, 18, height());
    }
private:
    QColor m_color;
};

BlockEditor::BlockEditor(QWidget* parent) : QWidget(parent) {
    setObjectName("blockEditor");
    setFocusPolicy(Qt::StrongFocus);
    setStyleSheet(
        "QWidget#blockEditor { background: #F7F8FA; }"
        "QLabel { background: transparent; color: #0F172A; font-family: 'Segoe UI', Arial, sans-serif; }"
        "QListWidget#blockListWidget { background: #FBFBFB; border: 1px solid #D9E2EC; border-radius: 12px; padding: 10px; outline: none; }"
        "QListWidget#blockListWidget::item { background: transparent; border: none; padding: 0px; margin: 0px; }"
        "QListWidget#blockListWidget::item:hover { background: transparent; border: none; }"
        "QListWidget#blockListWidget::item:selected { background: transparent; border: none; }"
        "QPushButton { "
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #EEF2FF, stop:0.45 #E0E7FF, stop:0.46 #C7D2FE, stop:1 #A5B4FC); "
        "  border: 1.5px solid #6366F1; "
        "  border-radius: 8px; "
        "  padding: 6px 12px; "
        "  font-weight: bold; "
        "  color: #4338CA; "
        "  font-size: 11px; "
        "} "
        "QPushButton:hover { "
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #E0E7FF, stop:0.45 #C7D2FE, stop:0.46 #A5B4FC, stop:1 #818CF8); "
        "  border-color: #4F46E5; "
        "  color: #312E81; "
        "} "
        "QPushButton:pressed { "
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #A5B4FC, stop:1 #4F46E5); "
        "  color: white; "
        "}"
    );

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(10, 10, 10, 10);
    mainLayout->setSpacing(8);

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    
    // Left side: Variables Palette
    auto* paletteContainer = new QWidget(splitter);
    paletteContainer->setObjectName("paletteContainer");
    paletteContainer->setStyleSheet(
        "QWidget#paletteContainer { "
        "  background: #FBFBFB; "
        "  border: 1px solid #D9E2EC; "
        "  border-radius: 12px; "
    );

    auto* paletteLay = new QVBoxLayout(paletteContainer);
    paletteLay->setContentsMargins(12, 12, 12, 12);
    paletteLay->setSpacing(8);
    
    auto* palTitle = new QLabel("PALETA DE ACESSO RÁPIDO", paletteContainer);
    palTitle->setStyleSheet("font-size: 10px; font-weight: bold; color: #64748B; padding-left: 2px; background: transparent;");
    paletteLay->addWidget(palTitle);

    auto* scrollArea = new QScrollArea(paletteContainer);
    scrollArea->setWidgetResizable(true);
    scrollArea->setStyleSheet("QScrollArea { border: none; background: transparent; }");
    
    auto* scrollWidget = new QWidget();
    scrollWidget->setStyleSheet("background: transparent;");
    m_paletteLayout = new QVBoxLayout(scrollWidget);
    m_paletteLayout->setAlignment(Qt::AlignTop);
    m_paletteLayout->setSpacing(8); // Beautiful variable list item vertical separation
    m_paletteLayout->setContentsMargins(0, 4, 0, 4);
    scrollArea->setWidget(scrollWidget);
    paletteLay->addWidget(scrollArea);
    
    // Right side: Event Slot Logic
    auto* slotContainer = new QWidget(splitter);
    auto* slotLay = new QVBoxLayout(slotContainer);
    slotLay->setContentsMargins(0, 0, 0, 0);



    m_blockListWidget = new QListWidget(this);
    m_blockListWidget->setObjectName("blockListWidget");
    m_blockListWidget->setFocusPolicy(Qt::StrongFocus);
    m_blockListWidget->setSpacing(-6);
    m_blockListWidget->setDragEnabled(true);
    m_blockListWidget->setAcceptDrops(true);
    m_blockListWidget->setDropIndicatorShown(true);
    m_blockListWidget->setDragDropMode(QAbstractItemView::InternalMove);
    
    m_blockListWidget->setToolTip("Dica: Clique duplo em um bloco para excluí-lo.");
    
    // Intercept clicks, double-clicks, and right-clicks on viewport/widget
    m_blockListWidget->installEventFilter(this);
    m_blockListWidget->viewport()->installEventFilter(this);
    
    slotLay->addWidget(m_blockListWidget);

    splitter->addWidget(paletteContainer);
    splitter->addWidget(slotContainer);
    QList<int> sizes; sizes << 160 << 400; // slightly wider left-side panel
    splitter->setSizes(sizes);

    mainLayout->addWidget(splitter);
    setEnabled(false);
}

void BlockEditor::refreshPalette() {
    QLayoutItem* child;
    while ((child = m_paletteLayout->takeAt(0)) != nullptr) {
        if (child->widget()) child->widget()->deleteLater();
        delete child;
    }

    if (m_currentEventName == "monitor") {
        auto* helpHeader = new QLabel("PALETA DE VARIÁVEIS", this);
        helpHeader->setStyleSheet("font-size: 10px; font-weight: bold; color: #64748B; padding-left: 2px; padding-top: 4px; background: transparent;");
        m_paletteLayout->addWidget(helpHeader);

        auto* helpDesc = new QLabel("Use <b>duplo clique</b> na área à direita para adicionar blocos do tipo <b>eventCreate</b> para cada evento desejado.", this);
        helpDesc->setWordWrap(true);
        helpDesc->setStyleSheet("font-size: 11px; color: #475569; padding-left: 2px; line-height: 1.4;");
        m_paletteLayout->addWidget(helpDesc);
        return;
    }

    // Section 1: Variables
    auto* varHeader = new QLabel("VARIÁVEIS DO ESCOPO", this);
    varHeader->setStyleSheet("font-size: 10px; font-weight: bold; color: #64748B; padding-left: 2px; padding-top: 4px; background: transparent;");
    m_paletteLayout->addWidget(varHeader);

    for (const auto& varDef : m_currentScopeVariables) {
        m_paletteLayout->addWidget(new VisualVariableItem(varDef, this));
    }

    // Section 2: EEPROM Registry (Discovery of globally saved variables/keys)
    QSet<QString> eepromKeys;
    // Scan all event blocks in the entire project
    for (auto it = m_eventBlockStorage.begin(); it != m_eventBlockStorage.end(); ++it) {
        for (const auto& block : it.value()) {
            if (block.type == LogicBlockType::EEPROM_OP && block.actionCommand == "SAVE") {
                QString key = block.actionTarget.trimmed().remove(" ");
                if (!key.isEmpty()) {
                    eepromKeys.insert(key);
                }
            }
        }
    }
    // Scan active blocks too
    for (const auto& block : m_activeBlocks) {
        if (block.type == LogicBlockType::EEPROM_OP && block.actionCommand == "SAVE") {
            QString key = block.actionTarget.trimmed().remove(" ");
            if (!key.isEmpty()) {
                eepromKeys.insert(key);
            }
        }
    }
    
    if (!eepromKeys.isEmpty()) {
        auto* eepromHeader = new QLabel("REGISTRO EEPROM (Salvos)", this);
        eepromHeader->setStyleSheet("font-size: 10px; font-weight: bold; color: #EF4444; padding-left: 2px; padding-top: 10px; background: transparent;");
        m_paletteLayout->addWidget(eepromHeader);
        
        // Sort keys alphabetically
        QList<QString> sortedKeys = eepromKeys.values();
        std::sort(sortedKeys.begin(), sortedKeys.end());
        
        for (const auto& ekey : sortedKeys) {
            VariableDef def;
            def.name = ekey;
            def.type = VarType::INT; 
            def.scope = VarScope::COMP_GLOBAL; // Make it global scope so it drop-matches as a global
            def.description = QString("Registro persistente salvo na EEPROM na chave '%1'.").arg(ekey);
            
            auto* item = new VisualVariableItem(def, this);
            item->setToolTip(def.description);
            item->setStyleSheet("VisualVariableItem { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #EF4444, stop:1 #B91C1C); border: 1.5px solid #991B1B; } "
                                "QLabel { color: #FFFFFF !important; }");
            m_paletteLayout->addWidget(item);
        }
    }
}

void BlockEditor::rebuildScopeVariables() {
    m_currentScopeVariables = m_hardwareScopeVariables;

    for (const auto& block : m_activeBlocks) {
        if (block.type == LogicBlockType::CREATE_VAR) {
            QString name = block.createVarName.trimmed().remove(" ");
            if (!name.isEmpty()) {
                VariableDef def;
                def.name = name;
                def.type = block.createVarType;
                def.scope = VarScope::LOCAL_EVENT;
                switch (def.type) {
                    case VarType::INT: def.initialValue = "0"; break;
                    case VarType::FLOAT: def.initialValue = "0.0"; break;
                    case VarType::BOOL: def.initialValue = "false"; break;
                    case VarType::STRING: def.initialValue = "\"\""; break;
                    default: def.initialValue = "0"; break;
                }
                def.description = QString("Variável Local (%1)").arg(VariableDef::typeToString(def.type));
                m_currentScopeVariables.append(def);
            }
        }
    }

    refreshPalette();
}

static QString getNumericSuffix(const QString& name) {
    int sep = name.lastIndexOf('-');
    if (sep == -1) sep = name.lastIndexOf('_');
    if (sep != -1) {
        bool ok = false;
        int num = name.mid(sep + 1).toInt(&ok);
        if (ok) return QString("_%1").arg(num);
    }
    return "";
}

void BlockEditor::loadEventLogic(const QString& compId, const QString& eventName, 
                                 const QStringList& avLeds, const QStringList& avPots, const QStringList& avBuzzers, const QStringList& avMotors,
                                 const QStringList& avDhts, const QStringList& avHcsrs) {
    if (!m_currentCompId.isEmpty() && !m_currentEventName.isEmpty()) {
        QString key = QString("%1:%2").arg(m_currentCompId).arg(m_currentEventName);
        m_eventBlockStorage[key] = m_activeBlocks;
    }

    setEnabled(true);
    m_currentCompId = compId;
    m_currentEventName = eventName;

    // Convert contextual components into Visual Variables!
    m_hardwareScopeVariables.clear();

    // Add global DHT22 variables (umidade and temperatura) per sensor
    for (const auto& dhtName : avDhts) {
        QString suffix = getNumericSuffix(dhtName);
        {
            VariableDef def;
            def.name = QString("umidade%1").arg(suffix);
            def.type = VarType::FLOAT;
            def.scope = VarScope::COMP_GLOBAL;
            def.initialValue = "0.0";
            def.description = QString("Valor global da umidade do ar (%) para %1").arg(dhtName);
            m_hardwareScopeVariables.append(def);
        }
        {
            VariableDef def;
            def.name = QString("temperatura%1").arg(suffix);
            def.type = VarType::FLOAT;
            def.scope = VarScope::COMP_GLOBAL;
            def.initialValue = "0.0";
            def.description = QString("Valor global da temperatura ambiente (ºC) para %1").arg(dhtName);
            m_hardwareScopeVariables.append(def);
        }
    }
    
    // Add global HC-SR04 variable (distancia) per sensor
    for (const auto& hcName : avHcsrs) {
        QString suffix = getNumericSuffix(hcName);
        {
            VariableDef def;
            def.name = QString("distancia%1").arg(suffix);
            def.type = VarType::FLOAT;
            def.scope = VarScope::COMP_GLOBAL;
            def.initialValue = "0.0";
            def.description = QString("Valor global da distância calculada (cm) para %1").arg(hcName);
            m_hardwareScopeVariables.append(def);
        }
    }

    // Add LEDs/Actuators
    for (const auto& led : avLeds) {
        VariableDef def;
        def.name = "PIN_" + sanitizeIdentifier(led);
        def.type = VarType::BOOL;
        def.scope = VarScope::RUNTIME_OUTPUT;
        def.initialValue = "false";
        def.description = QString("Atuador Digital / LED (%1)").arg(led);
        m_hardwareScopeVariables.append(def);
    }

    // Add Potentiometers/Sensors
    for (const auto& pot : avPots) {
        VariableDef def;
        def.name = "PIN_" + sanitizeIdentifier(pot);
        def.type = VarType::FLOAT;
        def.scope = VarScope::RUNTIME_OUTPUT;
        def.initialValue = "0.0";
        def.description = QString("Sensor Analógico (%1)").arg(pot);
        m_hardwareScopeVariables.append(def);
    }

    // Add Buzzers
    for (const auto& buz : avBuzzers) {
        VariableDef def;
        def.name = "PIN_" + sanitizeIdentifier(buz);
        def.type = VarType::BOOL;
        def.scope = VarScope::RUNTIME_OUTPUT;
        def.initialValue = "false";
        def.description = QString("Buzzer (%1)").arg(buz);
        m_hardwareScopeVariables.append(def);
    }

    // Add Motors
    for (const auto& mot : avMotors) {
        VariableDef def;
        def.name = "PIN_" + sanitizeIdentifier(mot);
        def.type = VarType::INT;
        def.scope = VarScope::RUNTIME_OUTPUT;
        def.initialValue = "0";
        def.description = QString("Motor / Atuador de Rotação (%1)").arg(mot);
        m_hardwareScopeVariables.append(def);
    }

    QString key = QString("%1:%2").arg(compId).arg(eventName);
    if (m_eventBlockStorage.contains(key)) {
        m_activeBlocks = m_eventBlockStorage[key];
    } else {
        m_activeBlocks.clear();
    }

    rebuildScopeVariables();
    refreshListDisplay();
}

void BlockEditor::addAssignmentBlock() {
    static int c = 0;
    EventLogicBlock b;
    b.id = QString("assign_%1").arg(c++);
    b.type = LogicBlockType::ASSIGNMENT;
    m_activeBlocks.append(b);
    refreshListDisplay();
    emit blocksChanged();
}

void BlockEditor::addConditionBlock() {
    static int c = 0;
    EventLogicBlock b;
    b.id = QString("cond_%1").arg(c++);
    b.type = LogicBlockType::CONDITION;
    b.conditionExpression = "true"; // safe default
    m_activeBlocks.append(b);

    EventLogicBlock fim;
    fim.id = QString("fim_%1").arg(c++);
    fim.type = LogicBlockType::FIM;
    m_activeBlocks.append(fim);

    refreshListDisplay();
    emit blocksChanged();
}

void BlockEditor::addActionBlock() {
    static int c = 0;
    EventLogicBlock b;
    b.id = QString("act_%1").arg(c++);
    b.type = LogicBlockType::ACTION;
    m_activeBlocks.append(b);
    refreshListDisplay();
    emit blocksChanged();
}

void BlockEditor::addMathBlock() {
    static int c = 0;
    EventLogicBlock b;
    b.id = QString("math_%1").arg(c++);
    b.type = LogicBlockType::MATH;
    b.mathOperator = "+"; // default to sum block
    m_activeBlocks.append(b);
    refreshListDisplay();
    emit blocksChanged();
}

void BlockEditor::addBlock(const QString& type) {
    static int c = 0;
    EventLogicBlock b;
    b.id = QString("%1_%2").arg(type).arg(c++);
    
    if (type == "serialPrint") {
        b.type = LogicBlockType::SERIAL_PRINT;
        b.assignTarget = "LN";
        b.assignExpression = "";
        m_activeBlocks.append(b);
    } else if (type == "eepromSave") {
        b.type = LogicBlockType::EEPROM_OP;
        b.actionCommand = "SAVE";
        b.actionTarget = "";
        m_activeBlocks.append(b);
    } else if (type == "eepromRestore") {
        b.type = LogicBlockType::EEPROM_OP;
        b.actionCommand = "RESTORE";
        b.actionTarget = "";
        m_activeBlocks.append(b);
    } else if (type == "if" || type == "elseif" || type == "else" || type == "while" || type == "for") {

        b.type = LogicBlockType::CONDITION;
        if (type == "else") {
            b.conditionExpression = "senao";
        } else if (type == "for") {
            b.conditionExpression = "int i = 0; i < 10; i++";
        } else {
            b.conditionExpression = "true";
        }
        m_activeBlocks.append(b);

        EventLogicBlock fim;
        fim.id = QString("fim_%1").arg(c++);
        fim.type = LogicBlockType::FIM;
        m_activeBlocks.append(fim);
    } else if (type == "fim") {
        b.type = LogicBlockType::FIM;
        m_activeBlocks.append(b);
    } else {
        b.type = LogicBlockType::ACTION;
        if (type == "callFunction") {
            b.actionCommand = "CALL_FUNCTION";
            b.actionTarget = "minhaFuncao";
        } else if (type == "return") {
            b.actionCommand = "RETURN";
            b.actionTarget = "0";
        } else {
            b.actionTarget = "pino";
            if (type == "digitalWrite") {
                b.actionCommand = "HIGH";
            } else if (type == "toggle") {
                b.actionCommand = "TOGGLE";
            } else if (type == "delay") {
                b.actionCommand = "DELAY";
                b.actionTarget = "1000";
            } else {
                b.actionCommand = "HIGH";
            }
        }
        m_activeBlocks.append(b);
    }
    refreshListDisplay();
    emit blocksChanged();
}

void BlockEditor::addCreateVarBlock() {
    static int c = 0;
    EventLogicBlock b;
    b.id = QString("create_var_%1").arg(c++);
    b.type = LogicBlockType::CREATE_VAR;
    b.createVarName = "";
    b.createVarType = VarType::INT;
    m_activeBlocks.append(b);
    rebuildScopeVariables();
    refreshListDisplay();
    emit blocksChanged();
}

void BlockEditor::removeSelectedBlock() {
    int row = m_blockListWidget->currentRow();
    int blockIndex = row - 1; // Align list item indices to active action array
    if (blockIndex >= 0 && blockIndex < m_activeBlocks.size()) {
        m_activeBlocks.removeAt(blockIndex);
        rebuildScopeVariables();
        refreshListDisplay();
        emit blocksChanged();
    }
}

void BlockEditor::refreshListDisplay() {
    m_blockListWidget->clear();
    if (m_currentCompId.isEmpty()) return;

    // Pick visual color themes matched to event types
    QString eventColor = "#F59E0B"; // Neon Amber
    if (m_currentEventName == "aoIniciar") {
        eventColor = "#10B981"; // Emerald Green
    } else if (m_currentEventName == "aoLigar") {
        eventColor = "#8B5CF6"; // Royal Violet
    } else if (m_currentEventName == "aoGirar") {
        eventColor = "#06B6D4"; // Cyan
    } else if (m_currentEventName == "aoTocar") {
        eventColor = "#EF4444"; // Red
    }

    // 1. Create Virtual Parent Event Header Block (High-Contrast Dome Top)
    auto* headerItem = new QListWidgetItem(m_blockListWidget);
    auto* headerWidget = new SketchwareBlockWidget(QColor(eventColor), true, false, this);
    headerWidget->setObjectName("headerBlockWidget");
    headerWidget->setFixedHeight(76);

    auto* headerLay = new QHBoxLayout(headerWidget);
    headerLay->setContentsMargins(18, 30, 18, 12); // larger top margin for curved dome
    headerLay->setSpacing(10);
    
    auto* triggerBadge = new QLabel("TRIGGER", headerWidget);
    triggerBadge->setStyleSheet(
        "background: rgba(255, 255, 255, 0.7); "
        "border: 1px solid rgba(255, 255, 255, 0.75); "
        "border-radius: 12px; "
        "padding: 3px 8px; "
        "font-size: 9px; "
        "font-weight: 900; "
        "color: #0F172A;"
    );
    headerLay->addWidget(triggerBadge);

    auto* titleLbl = new QLabel(m_currentEventName, headerWidget);
    titleLbl->setStyleSheet("font-size: 14px; font-weight: bold; color: #FFFFFF; font-family: 'Consolas', monospace;");
    headerLay->addWidget(titleLbl);

    auto* arrowLbl = new QLabel("➔", headerWidget);
    arrowLbl->setStyleSheet("font-size: 13px; color: rgba(255, 255, 255, 0.85); font-weight: bold;");
    headerLay->addWidget(arrowLbl);

    auto* compBadge = new QLabel(m_currentCompId, headerWidget);
    compBadge->setStyleSheet(
        "background: rgba(255, 255, 255, 0.28); "
        "border: 1px solid rgba(255, 255, 255, 0.35); "
        "border-radius: 12px; "
        "padding: 4px 10px; "
        "font-size: 11px; "
        "font-weight: bold; "
        "color: #FFFFFF; "
        "font-family: 'Consolas', monospace;"
    );
    headerLay->addWidget(compBadge);
    
    headerLay->addStretch();

    // Wrap header in alignment container
    auto* headerContainer = new QWidget();
    auto* headerContainerLay = new QHBoxLayout(headerContainer);
    headerContainerLay->setContentsMargins(0, 0, 0, 0);
    headerContainerLay->setSpacing(0);
    headerContainerLay->addSpacing(16); // Left space to align with blocks
    headerContainerLay->addWidget(headerWidget);
    headerContainerLay->addSpacing(10); // Right space

    headerItem->setSizeHint(QSize(250, 70)); // visually overlaps next block by 6px
    m_blockListWidget->setItemWidget(headerItem, headerContainer);

    // 2. Render all nested blocks with nesting C-bracket spines
    QVector<QColor> nestStack;
        for (int i = 0; i < m_activeBlocks.size(); ++i) {
            const auto& block = m_activeBlocks[i];
            
            QColor matchedParentColor;
            // Decrease indent level BEFORE rendering closing/transition blocks
            if (block.type == LogicBlockType::FIM) {
                if (!nestStack.isEmpty()) {
                    matchedParentColor = nestStack.last();
                    nestStack.removeLast();
                }
            }
            
            QVector<QColor> currentSpines = nestStack;
            
            // Increase indent level AFTER rendering opening blocks
            if (block.type == LogicBlockType::CONDITION) {
                QColor condColor = QColor("#FF5722"); // default orange for 'if', 'else'
                if (block.id.startsWith("while") || block.id.startsWith("for")) {
                    condColor = QColor("#D97706"); // loop dark amber
                } else if (block.id.startsWith("elseif")) {
                    condColor = QColor("#F97316"); // elseif orange
                }
                nestStack.append(condColor);
            }
            
            auto* listItem = new QListWidgetItem(m_blockListWidget);
            
            auto* container = new QWidget();
            container->setObjectName("blockContainer");
            container->setStyleSheet("QWidget#blockContainer { background: transparent; }");
            auto* containerLay = new QHBoxLayout(container);
            containerLay->setContentsMargins(0, 0, 0, 0);
            containerLay->setSpacing(0);

            // 1. Add left margin alignment spacer
            containerLay->addSpacing(16);

            // 2. Add visual C-bracket connector spines in indentation spaces
            for (const auto& color : currentSpines) {
                containerLay->addWidget(new CBracketSpineWidget(color, container));
            }
            
            // 3. Add custom block widget itself
            QWidget* blockWidget = createBlockWidget(i, block, matchedParentColor);
            containerLay->addWidget(blockWidget);

            // 4. Add right spacer
            containerLay->addSpacing(10);

            listItem->setSizeHint(QSize(250, 66)); // visually overlaps next block by 6px
            m_blockListWidget->setItemWidget(listItem, container);
        }

    // 3. Create Virtual Footer Spacer (for 100% bug-free drag reordering index tracking)
    auto* footerItem = new QListWidgetItem(m_blockListWidget);
    auto* footerSpacer = new QWidget();
    footerSpacer->setFixedHeight(12);
    footerItem->setSizeHint(QSize(250, 12));
    m_blockListWidget->setItemWidget(footerItem, footerSpacer);
}

QWidget* BlockEditor::createBlockWidget(int index, const EventLogicBlock& block, const QColor& customFimColor) {
    QColor blockColor; 
    QString actionName;

    if (block.type == LogicBlockType::ASSIGNMENT) { 
        blockColor = QColor("#10B981"); // Green
        actionName = "ATRIBUIR";
    }
    else if (block.type == LogicBlockType::CONDITION) { 
        blockColor = QColor("#FF5722"); // Orange
        actionName = "SE";
        if (block.id.startsWith("while")) {
            actionName = "ENQUANTO";
            blockColor = QColor("#D97706"); // Loop dark amber
        } else if (block.id.startsWith("for")) {
            actionName = "PARA (FOR)";
            blockColor = QColor("#D97706"); // Loop dark amber
        } else if (block.id.startsWith("elseif")) {
            actionName = "SENÃO SE";
            blockColor = QColor("#F97316"); // Elseif different orange shade
        } else if (block.conditionExpression == "senao" || block.conditionExpression == "else") {
            actionName = "SENÃO";
        }
    }
    else if (block.type == LogicBlockType::ACTION) { 
        blockColor = QColor("#3B82F6"); // Blue
        actionName = "AÇÃO";
        if (block.actionCommand == "RETURN") {
            actionName = "RETORNAR";
            blockColor = QColor("#EF4444"); // Red for return
        } else if (block.actionCommand == "CALL_FUNCTION") {
            actionName = "FUNÇÃO";
        }
    }
    else if (block.type == LogicBlockType::MATH) {
        blockColor = QColor("#EC4899"); // Pink/Magenta for math
        actionName = "MATEMÁTICA";
    }
    else if (block.type == LogicBlockType::CREATE_VAR) {
        blockColor = QColor("#6366F1"); // Indigo for Create Var
        actionName = "CRIAR VARIÁVEL";
    } else if (block.type == LogicBlockType::SERIAL_PRINT) {
        blockColor = QColor("#8B5CF6"); // Violet
        actionName = "SERIAL PRINT";
    } else if (block.type == LogicBlockType::EEPROM_OP) {
        if (block.actionCommand == "SAVE") {
            blockColor = QColor("#D97706"); // Amber/Orange
            actionName = "SALVAR NA EEPROM";
        } else {
            blockColor = QColor("#059669"); // Emerald/Teal
            actionName = "RESTAURAR DA EEPROM";
        }
    } else if (block.type == LogicBlockType::EVENT_CREATE) {
        blockColor = QColor("#D946EF"); // Fuchsia
        actionName = "eventCreate";
    } else {
        blockColor = customFimColor.isValid() ? customFimColor : QColor("#64748B");
        actionName = "FIM";
    }

    auto* w = new SketchwareBlockWidget(blockColor, false, block.type == LogicBlockType::FIM, this);
    w->setObjectName("blockWidget");
    w->setFixedHeight(72);

    auto* lay = new QHBoxLayout(w);
    lay->setContentsMargins(18, 8, 18, 8);
    lay->setSpacing(10);

    // 1. Add Badge (Clean Translucent White Capsule)
    auto* actionBadge = new QLabel(actionName, w);
    actionBadge->setStyleSheet(
        "background: rgba(255, 255, 255, 0.15); "
        "border: 1px solid rgba(255, 255, 255, 0.25); "
        "border-radius: 12px; "
        "padding: 4px 10px; "
        "font-size: 11px; "
        "font-weight: bold; "
        "color: #FFFFFF;"
    );
    lay->addWidget(actionBadge);

    // 3. Add dynamic controls
    if (block.type == LogicBlockType::ASSIGNMENT) {
        auto* targetEdit = new VariableSlotEdit(w);
        targetEdit->installEventFilter(this);
        targetEdit->setPlaceholderText("Alvo (Arraste Var)");
        targetEdit->setText(block.assignTarget);

        auto* equalsLbl = new QLabel("=", w);
        equalsLbl->setStyleSheet("color: white; font-weight: bold;");

        auto* expEdit = new VariableSlotEdit(w);
        expEdit->installEventFilter(this);
        expEdit->setPlaceholderText("Valor ou Expressão (Arraste Var)");
        expEdit->setText(block.assignExpression);

        lay->addWidget(targetEdit);
        lay->addWidget(equalsLbl);
        lay->addWidget(expEdit);

        auto saveParams = [this, index, targetEdit, expEdit]() {
            m_activeBlocks[index].assignTarget = targetEdit->text();
            m_activeBlocks[index].assignExpression = expEdit->text();
            emit blocksChanged();
        };

        connect(targetEdit, &QLineEdit::textChanged, this, saveParams);
        connect(expEdit, &QLineEdit::textChanged, this, saveParams);

    } else if (block.type == LogicBlockType::CONDITION) {
        auto* expEdit = new VariableSlotEdit(w);
        expEdit->installEventFilter(this);
        expEdit->setPlaceholderText("Expressão Condicional (ex: var > 10)");
        expEdit->setText(block.conditionExpression);

        lay->addWidget(expEdit);

        connect(expEdit, &QLineEdit::textChanged, this, [this, index, expEdit]() {
            m_activeBlocks[index].conditionExpression = expEdit->text();
            emit blocksChanged();
        });

    } else if (block.type == LogicBlockType::ACTION) {
        auto* targetEdit = new VariableSlotEdit(w);
        targetEdit->installEventFilter(this);
        targetEdit->setText(block.actionTarget);

        auto* paramEdit = new VariableSlotEdit(w);
        paramEdit->installEventFilter(this);
        paramEdit->setText(block.actionParam);

        auto* paramEdit2 = new VariableSlotEdit(w);
        paramEdit2->installEventFilter(this);
        paramEdit2->setText(block.actionParam2);

        auto* paramCombo3 = new QComboBox(w);
        paramCombo3->addItem("ms");
        paramCombo3->addItem("s");
        paramCombo3->setCurrentText(block.actionParam3.isEmpty() ? "ms" : block.actionParam3);

        auto* cmdCombo = new QComboBox(w);
        cmdCombo->addItem("LIGAR (HIGH)", "HIGH");
        cmdCombo->addItem("DESLIGAR (LOW)", "LOW");
        cmdCombo->addItem("INVERTER (TOGGLE)", "TOGGLE");
        cmdCombo->addItem("AGUARDAR (DELAY)", "DELAY");
        cmdCombo->addItem("GIRAR MOTOR (Ângulo)", "ROTATE_MOTOR");
        cmdCombo->addItem("GIRAR MOTOR (Tempo)", "MOTOR_SPIN_TIME");
        cmdCombo->addItem("GIRAR MOTOR (Infinito)", "MOTOR_SPIN_INFINITE");
        cmdCombo->addItem("CALCULAR BATERIA (%)", "CALC_BATTERY");
        cmdCombo->addItem("CHAMAR FUNÇÃO", "CALL_FUNCTION");
        cmdCombo->addItem("RETORNAR VALOR", "RETURN");

        int cmdIndex = cmdCombo->findData(block.actionCommand);
        if (cmdIndex == -1) {
            cmdIndex = cmdCombo->findText(block.actionCommand);
        }
        if (cmdIndex != -1) {
            cmdCombo->setCurrentIndex(cmdIndex);
        }

        lay->addWidget(targetEdit);
        lay->addWidget(cmdCombo);
        lay->addWidget(paramEdit);
        lay->addWidget(paramEdit2);
        lay->addWidget(paramCombo3);

        auto updateVis = [cmdCombo, targetEdit, paramEdit, paramEdit2, paramCombo3]() {
            QString cmd = cmdCombo->currentData().toString();
            paramEdit2->hide();
            paramCombo3->hide();
            if (cmd == "ROTATE_MOTOR") {
                targetEdit->setPlaceholderText("Motor Alvo");
                paramEdit->setPlaceholderText("Graus / Velocidade");
                targetEdit->show();
                paramEdit->show();
            } else if (cmd == "MOTOR_SPIN_INFINITE") {
                targetEdit->setPlaceholderText("Motor Alvo");
                paramEdit->setPlaceholderText("Velocidade (0-100)");
                targetEdit->show();
                paramEdit->show();
            } else if (cmd == "MOTOR_SPIN_TIME") {
                targetEdit->setPlaceholderText("Motor Alvo");
                paramEdit->setPlaceholderText("Velocidade (0-100)");
                paramEdit2->setPlaceholderText("Tempo");
                targetEdit->show();
                paramEdit->show();
                paramEdit2->show();
                paramCombo3->show();
            } else if (cmd == "CALL_FUNCTION") {
                targetEdit->setPlaceholderText("Nome da função (ex: minhaFuncao)");
                paramEdit->setPlaceholderText("Parâmetros");
                targetEdit->show();
                paramEdit->show();
            } else if (cmd == "CALC_BATTERY") {
                targetEdit->setPlaceholderText("Pino ADC (ex: BESS)");
                paramEdit->setPlaceholderText("Salvar resultado na Var...");
                targetEdit->show();
                paramEdit->show();
            } else if (cmd == "DELAY") {
                targetEdit->hide();
                paramEdit->setPlaceholderText("Milissegundos (ms)");
                paramEdit->show();
            } else if (cmd == "RETURN") {
                targetEdit->hide();
                paramEdit->setPlaceholderText("Valor ou expressão");
                paramEdit->show();
            } else {
                targetEdit->setPlaceholderText("Alvo (Pino / Var)");
                targetEdit->show();
                paramEdit->hide();
            }
        };
        updateVis();

        auto saveParams = [this, index, targetEdit, cmdCombo, paramEdit, paramEdit2, paramCombo3]() {
            m_activeBlocks[index].actionTarget = targetEdit->text();
            m_activeBlocks[index].actionCommand = cmdCombo->currentData().toString();
            m_activeBlocks[index].actionParam = paramEdit->text();
            m_activeBlocks[index].actionParam2 = paramEdit2->text();
            m_activeBlocks[index].actionParam3 = paramCombo3->currentText();
            emit blocksChanged();
        };

        connect(targetEdit, &QLineEdit::textChanged, this, saveParams);
        connect(paramEdit, &QLineEdit::textChanged, this, saveParams);
        connect(paramEdit2, &QLineEdit::textChanged, this, saveParams);
        connect(paramCombo3, &QComboBox::currentTextChanged, this, saveParams);
        connect(cmdCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [saveParams, updateVis](int) {
            updateVis();
            saveParams();
        });
    } else if (block.type == LogicBlockType::MATH) {
        auto* targetEdit = new VariableSlotEdit(w);
        targetEdit->installEventFilter(this);
        targetEdit->setPlaceholderText("Resultado (Arraste Var)");
        targetEdit->setText(block.mathTarget);

        auto* equalsLbl = new QLabel("=", w);
        equalsLbl->setStyleSheet("color: white; font-weight: bold;");

        auto* op1Edit = new VariableSlotEdit(w);
        op1Edit->installEventFilter(this);
        op1Edit->setPlaceholderText("Valor A ou Fórmula...");
        op1Edit->setText(block.mathOperand1);

        auto* opCombo = new QComboBox(w);
        opCombo->addItem("+ (Soma)", "+");
        opCombo->addItem("- (Subtração)", "-");
        opCombo->addItem("* (Multiplicação)", "*");
        opCombo->addItem("/ (Divisão)", "/");
        opCombo->addItem("Fórmula Avançada", " "); // Space for custom formula
        
        bool isFormulaMode = block.mathOperator.trimmed().isEmpty();
        if (isFormulaMode) {
            opCombo->setCurrentText("Fórmula Avançada");
        } else {
            opCombo->setCurrentText(block.mathOperator);
        }
        opCombo->setFixedWidth(120);

        auto* op2Edit = new VariableSlotEdit(w);
        op2Edit->installEventFilter(this);
        op2Edit->setPlaceholderText("Valor B");
        op2Edit->setText(block.mathOperand2);

        // Advanced math formula button (f(x))
        auto* formulaBtn = new QPushButton("f(x)", w);
        formulaBtn->setToolTip("Abrir Criador de Fórmulas Avançado");
        formulaBtn->setStyleSheet(
            "QPushButton { "
            "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #FDF2F8, stop:0.45 #FCE7F3, stop:0.46 #FBCFE8, stop:1 #F472B6); "
            "  border: 1.5px solid #EC4899; "
            "  border-radius: 8px; "
            "  color: #BE185D; "
            "  font-weight: bold; "
            "  font-size: 11px; "
            "  padding: 4px 10px; "
            "  min-width: 32px; "
            "  min-height: 20px; "
            "} "
            "QPushButton:hover { "
            "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #FCE7F3, stop:0.45 #FBCFE8, stop:0.46 #F472B6, stop:1 #EC4899); "
            "  border-color: #BE185D; "
            "  color: #9D174D; "
            "} "
            "QPushButton:pressed { "
            "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #F472B6, stop:1 #EC4899); "
            "  color: white; "
            "}"
        );

        lay->addWidget(targetEdit);
        lay->addWidget(equalsLbl);
        lay->addWidget(op1Edit);
        lay->addWidget(opCombo);
        lay->addWidget(op2Edit);
        lay->addWidget(formulaBtn);

        auto updateLayoutVisibility = [opCombo, op2Edit, op1Edit]() {
            bool formulaMode = opCombo->currentText() == "Fórmula Avançada";
            if (formulaMode) {
                op2Edit->hide();
                op1Edit->setPlaceholderText("Fórmula avançada (clique f(x) para editar)...");
            } else {
                op2Edit->show();
                op1Edit->setPlaceholderText("Valor A");
            }
        };
        updateLayoutVisibility();

        auto saveParams = [this, index, targetEdit, op1Edit, opCombo, op2Edit]() {
            m_activeBlocks[index].mathTarget = targetEdit->text();
            m_activeBlocks[index].mathOperand1 = op1Edit->text();
            QString op = opCombo->currentText() == "Fórmula Avançada" ? " " : opCombo->currentText();
            m_activeBlocks[index].mathOperator = op;
            m_activeBlocks[index].mathOperand2 = op2Edit->text();
            emit blocksChanged();
        };

        connect(targetEdit, &QLineEdit::textChanged, this, saveParams);
        connect(op1Edit, &QLineEdit::textChanged, this, saveParams);
        connect(op2Edit, &QLineEdit::textChanged, this, saveParams);
        connect(opCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [saveParams, updateLayoutVisibility](int) {
            updateLayoutVisibility();
            saveParams();
        });

        connect(formulaBtn, &QPushButton::clicked, this, [this, index, op1Edit, opCombo]() {
            MathFormulaDialog dlg(op1Edit->text(), m_currentScopeVariables, this);
            if (dlg.exec() == QDialog::Accepted) {
                op1Edit->setText(dlg.getFormula());
                opCombo->setCurrentText("Fórmula Avançada");
                m_activeBlocks[index].mathOperator = " ";
                m_activeBlocks[index].mathOperand2 = "";
                emit blocksChanged();
            }
        });
    } else if (block.type == LogicBlockType::CREATE_VAR) {
        auto* typeCombo = new QComboBox(w);
        typeCombo->addItem("INTEIRO (int)", static_cast<int>(VarType::INT));
        typeCombo->addItem("DECIMAL (float)", static_cast<int>(VarType::FLOAT));
        typeCombo->addItem("BOOLEANO (bool)", static_cast<int>(VarType::BOOL));
        typeCombo->addItem("TEXTO (String)", static_cast<int>(VarType::STRING));

        int typeIdx = typeCombo->findData(static_cast<int>(block.createVarType));
        if (typeIdx != -1) {
            typeCombo->setCurrentIndex(typeIdx);
        }

        typeCombo->setStyleSheet(
            "QComboBox { "
            "  background: #FFFFFF; "
            "  border: 1.5px solid #BFDBFE; "
            "  border-radius: 8px; "
            "  color: #0F172A; "
            "  font-size: 11px; "
            "  font-weight: bold; "
            "  padding: 4px 8px; "
            "  min-width: 130px; "
            "}"
            "QComboBox::drop-down { border: none; }"
            "QComboBox QAbstractItemView { "
            "  background-color: #FFFFFF; "
            "  border: 1.5px solid #BFDBFE; "
            "  color: #0F172A; "
            "  selection-background-color: #DBEAFE; "
            "}"
        );

        auto* nameEdit = new QLineEdit(w);
        nameEdit->setPlaceholderText("Nome da variável (ex: contador)");
        nameEdit->setText(block.createVarName);
        nameEdit->setStyleSheet(
            "QLineEdit { "
            "  background: #FFFFFF; "
            "  border: 1.5px solid #BFDBFE; "
            "  border-radius: 8px; "
            "  color: #1D4ED8; "
            "  font-size: 11px; "
            "  font-weight: bold; "
            "  padding: 4px 8px; "
            "  min-width: 180px; "
            "}"
        );

        lay->addWidget(typeCombo);
        lay->addWidget(nameEdit);

        auto saveParams = [this, index, typeCombo, nameEdit]() {
            m_activeBlocks[index].createVarType = static_cast<VarType>(typeCombo->currentData().toInt());
            m_activeBlocks[index].createVarName = nameEdit->text().trimmed().remove(" ");
            rebuildScopeVariables();
            emit blocksChanged();
        };

        connect(nameEdit, &QLineEdit::textChanged, this, saveParams);
        connect(typeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [saveParams](int) { saveParams(); });
    } else if (block.type == LogicBlockType::SERIAL_PRINT) {
        auto* expEdit = new VariableSlotEdit(w);
        expEdit->installEventFilter(this);
        expEdit->setPlaceholderText("Mensagem ou Expressão (use << para concatenar)");
        expEdit->setText(block.assignExpression);
        expEdit->setProperty("isSerial", true);

        auto* lnCombo = new QComboBox(w);
        lnCombo->addItem("Nova Linha (println)", "LN");
        lnCombo->addItem("Mesma Linha (print)", "SAME");

        int lnIdx = lnCombo->findData(block.assignTarget);
        if (lnIdx != -1) {
            lnCombo->setCurrentIndex(lnIdx);
        } else {
            lnCombo->setCurrentIndex(0); // Default to LN (println)
        }

        lnCombo->setStyleSheet(
            "QComboBox { "
            "  background: #FFFFFF; "
            "  border: 1.5px solid #BFDBFE; "
            "  border-radius: 8px; "
            "  color: #0F172A; "
            "  font-size: 11px; "
            "  font-weight: bold; "
            "  padding: 4px 8px; "
            "  min-width: 150px; "
            "}"
            "QComboBox::drop-down { border: none; }"
            "QComboBox QAbstractItemView { "
            "  background-color: #FFFFFF; "
            "  border: 1.5px solid #BFDBFE; "
            "  color: #0F172A; "
            "  selection-background-color: #DBEAFE; "
            "}"
        );

        lay->addWidget(expEdit);
        lay->addWidget(lnCombo);

        auto saveParams = [this, index, expEdit, lnCombo]() {
            m_activeBlocks[index].assignExpression = expEdit->text();
            m_activeBlocks[index].assignTarget = lnCombo->currentData().toString();
            emit blocksChanged();
        };

        connect(expEdit, &QLineEdit::textChanged, this, saveParams);
        connect(lnCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [saveParams](int) { saveParams(); });
    } else if (block.type == LogicBlockType::EEPROM_OP) {
        auto* slotEdit = new VariableSlotEdit(w);
        slotEdit->setPlaceholderText("Arraste o item...");
        slotEdit->setText(block.actionTarget);
        slotEdit->installEventFilter(this);
        
        if (block.actionCommand == "SAVE") {
            slotEdit->setStyleSheet("QLineEdit { background: #FFFFFF; border: 1.5px solid #FCD34D; border-radius: 8px; color: #B45309; font-size: 11px; font-weight: bold; padding: 4px 8px; min-width: 180px; }");
        } else {
            slotEdit->setStyleSheet("QLineEdit { background: #FFFFFF; border: 1.5px solid #6EE7B7; border-radius: 8px; color: #047857; font-size: 11px; font-weight: bold; padding: 4px 8px; min-width: 180px; }");
        }

        QLabel* label;
        if (block.actionCommand == "SAVE") {
            label = new QLabel("Salvar (Componente ou Variável):", w);
        } else {
            label = new QLabel("Restaurar (Componente ou Variável):", w);
        }
        label->setStyleSheet("color: white; font-weight: bold;");

        auto saveParams = [this, index, slotEdit]() {
            m_activeBlocks[index].actionTarget = slotEdit->text();
            emit blocksChanged();
        };

        connect(slotEdit, &QLineEdit::textChanged, this, saveParams);

    } else if (block.type == LogicBlockType::EVENT_CREATE) {
        auto* nameEdit = new QLineEdit(w);
        nameEdit->setPlaceholderText("aoClicar");
        nameEdit->setText(block.actionTarget);
        nameEdit->setStyleSheet(
            "QLineEdit { background: #FFFFFF; border: 1.5px solid #F0ABFC; border-radius: 8px; color: #701A75; font-size: 11px; font-weight: bold; padding: 4px 8px; min-width: 150px; }"
        );

        auto* labelVoid = new QLabel("void", w);
        labelVoid->setStyleSheet("color: #FFFFFF; font-weight: bold; font-family: 'Consolas', monospace; font-size: 12px;");

        auto* labelParens = new QLabel("()", w);
        labelParens->setStyleSheet("color: #FFFFFF; font-weight: bold; font-family: 'Consolas', monospace; font-size: 12px;");

        auto saveParams = [this, index, nameEdit]() {
            m_activeBlocks[index].actionTarget = nameEdit->text();
            emit blocksChanged();
        };

        connect(nameEdit, &QLineEdit::textChanged, this, saveParams);

        lay->addWidget(labelVoid);
        lay->addWidget(nameEdit);
        lay->addWidget(labelParens);
    }

    lay->addStretch();
    return w;
}

void BlockEditor::updateBlockParams() {
    emit blocksChanged();
}

bool BlockEditor::eventFilter(QObject* watched, QEvent* event) {
    VariableSlotEdit* slotEdit = qobject_cast<VariableSlotEdit*>(watched);
    if (slotEdit) {
        if (event->type() == QEvent::MouseButtonDblClick) {
            auto* mouseEvent = static_cast<QMouseEvent*>(event);
            QPoint globalPos = slotEdit->mapToGlobal(mouseEvent->pos());
            QPoint viewportPos = m_blockListWidget->viewport()->mapFromGlobal(globalPos);
            spawnSearchBox(viewportPos, QString(), slotEdit);
            return true;
        }
        else if (event->type() == QEvent::MouseButtonPress) {
            auto* mouseEvent = static_cast<QMouseEvent*>(event);
            if (mouseEvent->button() == Qt::RightButton) {
                QPoint globalPos = slotEdit->mapToGlobal(mouseEvent->pos());
                QPoint viewportPos = m_blockListWidget->viewport()->mapFromGlobal(globalPos);
                spawnSearchBox(viewportPos, QString(), slotEdit);
                return true;
            }
        }
    }

    if (watched == m_blockListWidget || watched == m_blockListWidget->viewport()) {
        if (event->type() == QEvent::MouseButtonDblClick) {
            auto* mouseEvent = static_cast<QMouseEvent*>(event);
            QListWidgetItem* item = m_blockListWidget->itemAt(mouseEvent->pos());
            if (item) {
                int row = m_blockListWidget->row(item);
                if (row > 0 && row <= m_activeBlocks.size()) { // Ignore header (row 0) and footer
                    m_blockListWidget->setCurrentRow(row);
                    removeSelectedBlock();
                    return true;
                }
            }
            spawnSearchBox(mouseEvent->pos());
            return true;
        }
        else if (event->type() == QEvent::MouseButtonPress) {
            auto* mouseEvent = static_cast<QMouseEvent*>(event);
            if (mouseEvent->button() == Qt::RightButton) {
                spawnSearchBox(mouseEvent->pos());
                return true;
            }
        }
        else if (event->type() == QEvent::KeyPress) {
            auto* keyEvent = static_cast<QKeyEvent*>(event);
            if (keyEvent->key() == Qt::Key_Delete || keyEvent->key() == Qt::Key_Backspace) {
                // Ignore the delete key entirely to prevent accidental component deletion
                // User must now use double click to delete blocks
                event->accept(); 
                return true;
            }
            QString text = keyEvent->text();
            if (!text.isEmpty() && text.at(0).isPrint() && keyEvent->modifiers() == Qt::NoModifier) {
                spawnSearchBox(m_blockListWidget->viewport()->rect().center(), text);
                return true;
            }
        }
        else if (event->type() == QEvent::Drop) {
            auto* dropEvent = static_cast<QDropEvent*>(event);
            
            QList<QListWidgetItem*> selected = m_blockListWidget->selectedItems();
            if (selected.isEmpty()) {
                dropEvent->acceptProposedAction();
                return true;
            }
            
            int srcRow = m_blockListWidget->row(selected.first());
            int srcIdx = srcRow - 1; // Header occupies row 0
            
            int totalBlocks = m_activeBlocks.size();
            if (srcIdx >= 0 && srcIdx < totalBlocks) {
                QPoint dropPos = dropEvent->position().toPoint();
                if (watched == m_blockListWidget) {
                    dropPos = m_blockListWidget->viewport()->mapFrom(m_blockListWidget, dropPos);
                }
                QListWidgetItem* targetItem = m_blockListWidget->itemAt(dropPos);
                int targetRow = targetItem ? m_blockListWidget->row(targetItem) : (totalBlocks + 1);
                
                if (targetRow != srcRow) {
                    EventLogicBlock draggedBlock = m_activeBlocks[srcIdx];
                    int tgtIdx = targetRow - 1;
                    
                    if (targetRow <= 0) {
                        m_activeBlocks.removeAt(srcIdx);
                        m_activeBlocks.insert(0, draggedBlock);
                    } else if (targetRow > totalBlocks) {
                        m_activeBlocks.removeAt(srcIdx);
                        m_activeBlocks.insert(m_activeBlocks.size(), draggedBlock);
                    } else {
                        // We dropped on a valid block at tgtIdx
                        EventLogicBlock targetBlock = m_activeBlocks[tgtIdx];
                        m_activeBlocks.removeAt(srcIdx);
                        
                        // Find the target block's new index after removal
                        int newTgtIdx = -1;
                        for (int k = 0; k < m_activeBlocks.size(); ++k) {
                            if (m_activeBlocks[k].id == targetBlock.id) {
                                newTgtIdx = k;
                                break;
                            }
                        }
                        
                        int insertIdx = srcIdx;
                        if (newTgtIdx != -1) {
                            LogicBlockType tgtType = targetBlock.type;
                            if (tgtType == LogicBlockType::CONDITION) {
                                // Dropped on an opening conditional block -> go INSIDE (immediately after it)
                                insertIdx = newTgtIdx + 1;
                            } else {
                                // Standard reordering: place it before the target block
                                insertIdx = newTgtIdx;
                            }
                        }
                        
                        m_activeBlocks.insert(qMax(0, qMin(insertIdx, m_activeBlocks.size())), draggedBlock);
                    }
                    
                    refreshListDisplay();
                    emit blocksChanged();
                }
            }
            dropEvent->acceptProposedAction();
            return true;
        }
    }
    else if (watched->objectName() == "floatingBlockSearchBox") {
        if (event->type() == QEvent::KeyPress) {
            auto* keyEvent = static_cast<QKeyEvent*>(event);
            if (keyEvent->key() == Qt::Key_Escape) {
                watched->deleteLater();
                return true;
            }
        } else if (event->type() == QEvent::FocusOut) {
            QLineEdit* edit = qobject_cast<QLineEdit*>(watched);
            if (edit) {
                QPointer<QLineEdit> guardedEdit(edit);
                QTimer::singleShot(150, this, [guardedEdit]() {
                    if (guardedEdit) {
                        guardedEdit->deleteLater();
                    }
                });
            }
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void BlockEditor::spawnSearchBox(const QPoint& pos, const QString& initialText, VariableSlotEdit* targetSlotEdit) {
    if (m_blockListWidget->viewport()->findChild<QLineEdit*>("floatingBlockSearchBox")) {
        return;
    }

    QLineEdit* searchEdit = new QLineEdit(m_blockListWidget->viewport());
    searchEdit->setObjectName("floatingBlockSearchBox");
    if (targetSlotEdit) {
        searchEdit->setPlaceholderText("Busca rápida...");
    } else {
        searchEdit->setPlaceholderText("Adicionar bloco...");
    }
    searchEdit->setStyleSheet(
        "QLineEdit#floatingBlockSearchBox { "
        "  background: rgba(255, 255, 255, 0.98); "
        "  border: 2px solid #BFDBFE; "
        "  border-radius: 8px; "
        "  color: #0F172A; "
        "  font-family: 'Segoe UI', Arial, sans-serif; "
        "  font-size: 12px; "
        "  font-weight: 500; "
        "  padding: 6px 12px; "
        "  min-width: 180px; "
        "}"
    );

    if (!initialText.isEmpty()) {
        searchEdit->setText(initialText);
    }

    searchEdit->adjustSize();
    int w = searchEdit->width();
    int h = searchEdit->height();

    // Bound coordinates to blockListWidget viewport
    int x = qMax(10, qMin(pos.x() - w/2, m_blockListWidget->viewport()->width() - w - 10));
    int y = qMax(10, qMin(pos.y() - h/2, m_blockListWidget->viewport()->height() - h - 10));

    searchEdit->setGeometry(x, y, w, h);
    searchEdit->show();
    searchEdit->raise();
    searchEdit->setFocus(Qt::OtherFocusReason);
    searchEdit->installEventFilter(this);

    // Dynamic autocomplete list of logical blocks or slot-specific scope variables and operators
    QStringList logicOptions;
    if (targetSlotEdit) {
        for (const auto& varDef : m_currentScopeVariables) {
            logicOptions << QString("🔹 %1 (%2)").arg(varDef.name).arg(VariableDef::typeToString(varDef.type));
        }
        logicOptions << "🟢 Soma (+)"
                     << "🟢 Subtração (-)"
                     << "🟢 Multiplicação (*)"
                     << "🟢 Divisão (/)"
                     << "🟢 Maior que (>)"
                     << "🟢 Menor que (<)"
                     << "🟢 Igual a (==)"
                     << "🟢 Diferente de (!=)"
                     << "🟢 E (&&)"
                     << "🟢 Ou (||)"
                     << "🟢 Não (!)";
    } else {
        if (m_currentEventName == "monitor") {
            logicOptions << "eventCreate (Criar Evento Customizado)";
        } else {
            logicOptions << "Criar Variável (Declara nova variável)"
                         << "Atribuir (Define valor de variável)"
                         << "Se (if - Bloco Condicional SE)"
                         << "Senão Se (elseif - Condicional Alternativa)"
                         << "Senão (else - Bloco Padrão)"
                         << "Enquanto (while - Repete enquanto verdadeiro)"
                         << "Repetir Para (for - Loop com contador)"
                         << "Fim (Fecha bloco condicional ou loop)"
                         << "Ação (Controla hardware / pino)"
                         << "Calcular Bateria (Lê o ADC e calcula carga)"
                         << "Chamar Função (executa função C++)"
                         << "Girar Motor (Ajusta ângulo do servo ou DC)"
                         << "Retornar (return - Devolve valor de função)"
                         << "Matemática (Contas e operadores)"
                         << "Escrever (digitalWrite)" 
                         << "Aguardar (delay)" 
                         << "Alternar (toggle)"
                         << "Salvar na EEPROM (Persistir estado/variável)"
                         << "Restaurar da EEPROM (Recuperar estado/variável)"
                         << "Serial Print (Escrever na Serial)"
                         << "eventCreate (Criar Evento Customizado)";
        }
    }

    logicOptions.sort(Qt::CaseInsensitive);

    QCompleter* completer = new QCompleter(logicOptions, searchEdit);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    completer->setFilterMode(Qt::MatchContains);
    completer->setCompletionMode(QCompleter::PopupCompletion);

    completer->popup()->setStyleSheet(
        "QAbstractItemView { "
        "  background-color: #FFFFFF; "
        "  border: 2px solid #BFDBFE; "
        "  border-radius: 8px; "
        "  color: #0F172A; "
        "  padding: 4px; "
        "  font-family: 'Segoe UI', Arial, sans-serif; "
        "  font-size: 11px; "
        "  font-weight: 500; "
        "}"
        "QAbstractItemView::item { "
        "  padding: 6px 12px; "
        "  border-radius: 4px; "
        "  color: #0F172A; "
        "}"
        "QAbstractItemView::item:hover { "
        "  background-color: #EEF2FF; "
        "  color: #1D4ED8; "
        "}"
        "QAbstractItemView::item:selected { "
        "  background-color: #DBEAFE; "
        "  color: white; "
        "}"
    );
    searchEdit->setCompleter(completer);

    // Show popup immediately
    QTimer::singleShot(50, searchEdit, [completer]() {
        completer->complete();
    });

    auto handleSelection = [this, searchEdit, targetSlotEdit](const QString& textVal) {
        if (!searchEdit || searchEdit->property("processed").toBool()) return;
        searchEdit->setProperty("processed", true);

        if (targetSlotEdit) {
            QString text = textVal.trimmed();
            QString insertText = text;

            // Extract content from parentheses if present (handles "Soma (+)", "E (&&)", etc.)
            int startParen = text.lastIndexOf("(");
            int endParen = text.lastIndexOf(")");
            if (startParen != -1 && endParen != -1 && endParen > startParen) {
                QString inside = text.mid(startParen + 1, endParen - startParen - 1).trimmed();
                if (inside == "+" || inside == "-" || inside == "*" || inside == "/" || 
                    inside == ">" || inside == "<" || inside == "==" || inside == "!=" ||
                    inside == "&&" || inside == "||" || inside == "!") {
                    insertText = inside;
                } else {
                    // For variables like "🔹 varName (type)", strip the prefix "🔹 " and suffix " (type)"
                    QString leftPart = text.left(startParen).trimmed();
                    if (leftPart.startsWith("🔹")) {
                        leftPart = leftPart.mid(1).trimmed(); // skip the emoji
                    }
                    insertText = leftPart;
                }
            } else {
                // Fallback, clean any emoji prefix
                if (insertText.startsWith("🔹") || insertText.startsWith("🟢")) {
                    insertText = insertText.mid(1).trimmed();
                }
            }

            QString current = targetSlotEdit->text().trimmed();
            if (current.isEmpty()) {
                targetSlotEdit->setText(insertText);
            } else {
                bool isOp = (insertText == "+" || insertText == "-" || insertText == "*" || insertText == "/" || 
                             insertText == ">" || insertText == "<" || insertText == "==" || insertText == "!=" ||
                             insertText == "&&" || insertText == "||" || insertText == "!");
                bool endsWithOp = (current.endsWith("+") || current.endsWith("-") || current.endsWith("*") || current.endsWith("/") ||
                                   current.endsWith(">") || current.endsWith("<") || current.endsWith("=") || current.endsWith("!") ||
                                   current.endsWith("&") || current.endsWith("|"));

                if (isOp) {
                    targetSlotEdit->setText(current + " " + insertText + " ");
                } else {
                    if (endsWithOp) {
                        targetSlotEdit->setText(targetSlotEdit->text() + " " + insertText);
                    } else {
                        targetSlotEdit->setText(targetSlotEdit->text() + " " + insertText);
                    }
                }
            }
            emit targetSlotEdit->textChanged(targetSlotEdit->text());
            searchEdit->deleteLater();
            return;
        }

        QString text = textVal.trimmed().toLower();

        if (text.contains("eventcreate") || text.contains("criar evento") || text.contains("aodetectar")) {
            EventLogicBlock b;
            b.id = QUuid::createUuid().toString();
            b.type = LogicBlockType::EVENT_CREATE;
            b.actionTarget = "aoDetectar";
            m_activeBlocks.append(b);
            refreshListDisplay();
            emit blocksChanged();
        } else if (text.contains("criar") || text.contains("declar") || text.contains("create") || text.contains("new")) {
            addCreateVarBlock();
        } else if (text.contains("atrib") || text.contains("defin") || text.contains("assign") || text == "=") {
            addAssignmentBlock();
        } else if (text.contains("fim") || text.contains("end") || text.contains("fech") || text == "}") {
            addBlock("fim");
        } else if (text.contains("senão se") || text.contains("senao se") || text.contains("elseif") || text.contains("else if")) {
            addBlock("elseif");
        } else if (text.contains("sen") || text.contains("else")) {
            addBlock("else");
        } else if (text.contains("condi") || text.contains("se ") || text == "se" || text.contains("if")) {
            addConditionBlock();
        } else if (text.contains("repetir para") || text.contains("para ") || text == "para" || text.contains("for")) {
            addBlock("for");
        } else if (text.contains("enquant") || text.contains("while") || text.contains("loop")) {
            addBlock("while");
        } else if (text.contains("retornar") || text.contains("return") || text.contains("devolver")) {
            addBlock("return");
        } else if (text.contains("salvar na eeprom") || text.contains("persistir")) {
            addBlock("eepromSave");
        } else if (text.contains("restaurar da eeprom") || text.contains("recuperar")) {
            addBlock("eepromRestore");
        } else if (text.contains("eeprom") || text.contains("memória") || text.contains("disco")) {
            // Default fallback
            addBlock("eepromSave");
        } else if (text.contains("serial") || text.contains("print") || text.contains("escrever na serial")) {
            // Serial BEFORE digitalWrite because "Escrever na Serial" also contains "escrev"
            addBlock("serialPrint");
        } else if ((text.contains("escrev") || text.contains("digit") || text.contains("write")) && !text.contains("serial")) {
            addBlock("digitalWrite");
        } else if (text.contains("aguard") || text.contains("delay") || text.contains("temp")) {
            addBlock("delay");
        } else if (text.contains("altern") || text.contains("tog") || text.contains("inver")) {
            addBlock("toggle");
        } else if (text.contains("chamar") || text.contains("call function") || text.contains("invocar")) {
            addBlock("callFunction");
        } else if (text.contains("calcular bateria") || text.contains("bateria") || text.contains("calc bat")) {
            EventLogicBlock b;
            b.id = QUuid::createUuid().toString();
            b.type = LogicBlockType::ACTION;
            b.actionCommand = "CALC_BATTERY";
            m_activeBlocks.append(b);
            refreshListDisplay();
            emit blocksChanged();
        } else if (text.contains("ação") || text.contains("acao") || text.contains("act") || text.contains("action")) {
            addActionBlock();
        } else if (text.contains("girar") || text.contains("motor") || text.contains("rotacionar")) {
            EventLogicBlock b;
            b.id = QUuid::createUuid().toString();
            b.type = LogicBlockType::ACTION;
            b.actionCommand = "ROTATE_MOTOR";
            m_activeBlocks.append(b);
            refreshListDisplay();
            emit blocksChanged();
        } else if (text.contains("matem") || text.contains("math") || text.contains("calc") || text.contains("soma") || text.contains("oper") || text.contains("cont") || text.contains("+")) {
            addMathBlock();
        }

        searchEdit->deleteLater();
    };

    connect(searchEdit, &QLineEdit::returnPressed, this, [searchEdit, handleSelection]() {
        handleSelection(searchEdit->text());
    });

    connect(completer, QOverload<const QString&>::of(&QCompleter::activated), this, handleSelection);
}

void BlockEditor::clearAllBlocks() {
    m_currentCompId.clear();
    m_currentEventName.clear();
    m_activeBlocks.clear();
    m_eventBlockStorage.clear();
    m_hardwareScopeVariables.clear();
    m_currentScopeVariables.clear();
    refreshListDisplay();
    emit blocksChanged();
}

QVector<EventLogicBlock> BlockEditor::getEventBlocks(const QString& compId, const QString& eventName) const {
    QString key = QString("%1:%2").arg(compId).arg(eventName);
    return m_eventBlockStorage.value(key);
}

void BlockEditor::setEventBlocks(const QString& compId, const QString& eventName, const QVector<EventLogicBlock>& blocks) {
    QString key = QString("%1:%2").arg(compId).arg(eventName);
    m_eventBlockStorage[key] = blocks;
    if (m_currentCompId == compId && m_currentEventName == eventName) {
        m_activeBlocks = blocks;
        rebuildScopeVariables();
        refreshListDisplay();
    }
}

void BlockEditor::setAvailableHooks(const QStringList& hooks) {
    m_availableHooks = hooks;
}

QJsonArray EventLogicBlock::serializeVector(const QVector<EventLogicBlock>& blocks) {
    QJsonArray arr;
    for (const auto& b : blocks) {
        QJsonObject obj;
        obj["id"] = b.id;
        obj["type"] = static_cast<int>(b.type);
        obj["assignTarget"] = b.assignTarget;
        obj["assignExpression"] = b.assignExpression;
        obj["conditionExpression"] = b.conditionExpression;
        obj["actionTarget"] = b.actionTarget;
        obj["actionCommand"] = b.actionCommand;
        obj["actionParam"] = b.actionParam;
        obj["actionParam2"] = b.actionParam2;
        obj["actionParam3"] = b.actionParam3;
        obj["mathTarget"] = b.mathTarget;
        obj["mathOperand1"] = b.mathOperand1;
        obj["mathOperator"] = b.mathOperator;
        obj["mathOperand2"] = b.mathOperand2;
        obj["createVarName"] = b.createVarName;
        obj["createVarType"] = static_cast<int>(b.createVarType);
        arr.append(obj);
    }
    return arr;
}

QVector<EventLogicBlock> EventLogicBlock::deserializeArray(const QJsonArray& array) {
    QVector<EventLogicBlock> blocks;
    for (int i = 0; i < array.size(); ++i) {
        QJsonObject obj = array[i].toObject();
        EventLogicBlock b;
        b.id = obj["id"].toString();
        b.type = static_cast<LogicBlockType>(obj["type"].toInt());
        b.assignTarget = obj["assignTarget"].toString();
        b.assignExpression = obj["assignExpression"].toString();
        b.conditionExpression = obj["conditionExpression"].toString();
        b.actionTarget = obj["actionTarget"].toString();
        b.actionCommand = obj["actionCommand"].toString();
        b.actionParam = obj["actionParam"].toString();
        b.actionParam2 = obj["actionParam2"].toString();
        b.actionParam3 = obj["actionParam3"].toString();
        b.mathTarget = obj["mathTarget"].toString();
        b.mathOperand1 = obj["mathOperand1"].toString();
        b.mathOperator = obj["mathOperator"].toString();
        b.mathOperand2 = obj["mathOperand2"].toString();
        b.createVarName = obj["createVarName"].toString();
        b.createVarType = static_cast<VarType>(obj["createVarType"].toInt());
        blocks.append(b);
    }
    return blocks;
}

MathFormulaDialog::MathFormulaDialog(const QString& initialFormula, const QVector<VariableDef>& vars, QWidget* parent)
    : QDialog(parent), m_formula(initialFormula), m_vars(vars) {
    setWindowTitle("Criador de Fórmulas Avançadas - LCD");
    resize(850, 580);
    setStyleSheet("QDialog { background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #E0F2FE, stop:0.6 #BAE6FD, stop:1 #F0FDFA); }");
    buildUI();
    updatePreview();
}

void MathFormulaDialog::buildUI() {
    auto* mainLay = new QHBoxLayout(this);
    mainLay->setContentsMargins(15, 15, 15, 15);
    mainLay->setSpacing(15);

    // Left Panel: Variables
    auto* leftContainer = new QWidget(this);
    leftContainer->setFixedWidth(200);
    auto* leftLay = new QVBoxLayout(leftContainer);
    leftLay->setContentsMargins(0, 0, 0, 0);
    leftLay->setSpacing(8);

    auto* varLbl = new QLabel("<b>VARIÁVEIS EXPOSTAS</b>", this);
    varLbl->setStyleSheet("color: #0369A1; font-size: 11px; font-weight: bold; font-family: 'Segoe UI', Arial;");
    leftLay->addWidget(varLbl);

    m_varListWidget = new QListWidget(this);
    m_varListWidget->setStyleSheet(
        "QListWidget { "
        "  background: rgba(255, 255, 255, 0.7); "
        "  border: 2px solid rgba(255, 255, 255, 0.8); "
        "  border-radius: 12px; "
        "  padding: 8px; "
        "  color: #0F172A; "
        "  font-family: 'Segoe UI', sans-serif; "
        "  font-size: 12px; "
        "  font-weight: bold; "
        "} "
        "QListWidget::item { "
        "  padding: 8px 12px; "
        "  border-radius: 8px; "
        "  margin-bottom: 2px; "
        "  color: #1E293B; "
        "} "
        "QListWidget::item:hover { "
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 rgba(56, 189, 248, 0.35), stop:0.45 rgba(56, 189, 248, 0.2), stop:0.46 rgba(14, 165, 233, 0.3), stop:1 rgba(14, 165, 233, 0.5)); "
        "  border: 1px solid rgba(14, 165, 233, 0.6); "
        "  color: #0369A1; "
        "} "
        "QListWidget::item:selected { "
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 rgba(14, 165, 233, 0.7), stop:0.45 rgba(14, 165, 233, 0.5), stop:0.46 rgba(2, 132, 199, 0.6), stop:1 rgba(2, 132, 199, 0.85)); "
        "  border: 1.5px solid rgba(2, 132, 199, 0.9); "
        "  color: white; "
        "}"
    );

    for (const auto& v : m_vars) {
        auto* item = new QListWidgetItem(QString("🔹 %1").arg(v.name), m_varListWidget);
        item->setData(Qt::UserRole, v.name);
        item->setToolTip(QString("Tipo: %1").arg(VariableDef::typeToString(v.type)));
    }
    // Also add X (integration variable)
    auto* itemX = new QListWidgetItem("🟢 X (Var. Integração)", m_varListWidget);
    itemX->setData(Qt::UserRole, "X");
    itemX->setToolTip("Variável usada para integração na função integral()");

    leftLay->addWidget(m_varListWidget);
    mainLay->addWidget(leftContainer);

    connect(m_varListWidget, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
        insertText(item->data(Qt::UserRole).toString());
    });

    // Right Panel: Calculator
    auto* rightContainer = new QWidget(this);
    auto* rightLay = new QVBoxLayout(rightContainer);
    rightLay->setContentsMargins(0, 0, 0, 0);
    rightLay->setSpacing(12);

    // 1. LCD Screen (Retro LCD theme)
    m_lcdScreen = new QTextBrowser(this);
    m_lcdScreen->setFixedHeight(140);
    m_lcdScreen->setStyleSheet(
        "QTextBrowser { "
        "  background-color: #FFFFFF; "
        "  border: 2px solid #CBD5E1; "
        "  border-radius: 10px; "
        "  padding: 14px; "
        "  color: #0F172A; "
        "  font-family: 'Consolas', 'Segoe UI', monospace; "
        "  font-size: 13px; "
        "  font-weight: bold; "
        "}"
    );
    rightLay->addWidget(m_lcdScreen);

    // 2. Raw edit
    m_formulaEdit = new QLineEdit(this);
    m_formulaEdit->setPlaceholderText("Escreva ou use os botões abaixo para montar sua fórmula...");
    m_formulaEdit->setText(m_formula);
    m_formulaEdit->setStyleSheet(
        "QLineEdit { "
        "  background: rgba(255, 255, 255, 0.85); "
        "  border: 2px solid rgba(255, 255, 255, 0.9); "
        "  border-radius: 10px; "
        "  padding: 10px 14px; "
        "  font-family: 'Consolas', monospace; "
        "  font-size: 14px; "
        "  color: #0F172A; "
        "} "
        "QLineEdit:focus { "
        "  border: 2.5px solid #0EA5E9; "
        "  background: #FFFFFF; "
        "}"
    );
    rightLay->addWidget(m_formulaEdit);

    connect(m_formulaEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
        m_formula = text;
        updatePreview();
    });

    // 3. Buttons Grid
    auto* gridContainer = new QWidget(this);
    auto* grid = new QGridLayout(gridContainer);
    grid->setSpacing(6);
    grid->setContentsMargins(0, 0, 0, 0);

    struct CalcBtn {
        QString label;
        QString textToInsert;
        QString style; // basic, func, action, clear
    };

    QVector<CalcBtn> buttons = {
        {"fraction(A,B)", "fraction(,)", "func"},
        {"sqrt(x)", "sqrt()", "func"},
        {"integral(A,B,f)", "integral(0,pi,,)", "func"},
        {"base^exp", "^", "func"},
        
        {"sin(x)", "sin()", "func"},
        {"cos(x)", "cos()", "func"},
        {"tan(x)", "tan()", "func"},
        {"log(x)", "log()", "func"},
        
        {"ln(x)", "ln()", "func"},
        {"abs(x)", "abs()", "func"},
        {"pi (π)", "pi", "func"},
        {"X", "X", "func"},
        
        {"(", "(", "basic"},
        {")", ")", "basic"},
        {",", ",", "basic"},
        {"/", "/", "basic"},
        
        {"7", "7", "basic"},
        {"8", "8", "basic"},
        {"9", "9", "basic"},
        {"*", "*", "basic"},
        
        {"4", "4", "basic"},
        {"5", "5", "basic"},
        {"6", "6", "basic"},
        {"-", "-", "basic"},
        
        {"1", "1", "basic"},
        {"2", "2", "basic"},
        {"3", "3", "basic"},
        {"+", "+", "basic"},
        
        {"0", "0", "basic"},
        {".", ".", "basic"},
        {"Backspace", "BACKSPACE", "clear"},
        {"Limpar", "CLEAR", "clear"}
    };

    int rRow = 0;
    int col = 0;
    for (const auto& btn : buttons) {
        auto* button = new QPushButton(btn.label, this);
        button->setFixedHeight(36);
        
        if (btn.style == "func") {
            button->setStyleSheet(
                "QPushButton { "
                "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #F0F9FF, stop:0.45 #E0F2FE, stop:0.46 #BAE6FD, stop:1 #7DD3FC); "
                "  border: 1.5px solid #38BDF8; "
                "  border-radius: 8px; "
                "  color: #0369A1; "
                "  font-weight: bold; "
                "  font-size: 11px; "
                "} "
                "QPushButton:hover { "
                "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #E0F2FE, stop:0.45 #BAE6FD, stop:0.46 #7DD3FC, stop:1 #38BDF8); "
                "  border-color: #0284C7; "
                "  color: #075985; "
                "} "
                "QPushButton:pressed { "
                "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #7DD3FC, stop:1 #0284C7); "
                "  color: white; "
                "}"
            );
        } else if (btn.style == "clear") {
            button->setStyleSheet(
                "QPushButton { "
                "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #FFF5F5, stop:0.45 #FED7D7, stop:0.46 #FEB2B2, stop:1 #FCA5A5); "
                "  border: 1.5px solid #F87171; "
                "  border-radius: 8px; "
                "  color: #991B1B; "
                "  font-weight: bold; "
                "  font-size: 11px; "
                "} "
                "QPushButton:hover { "
                "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #FED7D7, stop:0.45 #FEB2B2, stop:0.46 #FCA5A5, stop:1 #F87171); "
                "  border-color: #DC2626; "
                "  color: #7F1D1D; "
                "} "
                "QPushButton:pressed { "
                "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #FCA5A5, stop:1 #DC2626); "
                "  color: white; "
                "}"
            );
        } else {
            button->setStyleSheet(
                "QPushButton { "
                "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #FFFFFF, stop:0.45 #F8FAFC, stop:0.46 #F1F5F9, stop:1 #E2E8F0); "
                "  border: 1.5px solid #CBD5E1; "
                "  border-radius: 8px; "
                "  color: #1E293B; "
                "  font-weight: bold; "
                "  font-size: 12px; "
                "} "
                "QPushButton:hover { "
                "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #F8FAFC, stop:0.45 #F1F5F9, stop:0.46 #E2E8F0, stop:1 #CBD5E1); "
                "  border-color: #94A3B8; "
                "  color: #0F172A; "
                "} "
                "QPushButton:pressed { "
                "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #E2E8F0, stop:1 #94A3B8); "
                "}"
            );
        }

        grid->addWidget(button, rRow, col);

        connect(button, &QPushButton::clicked, this, [this, btn]() {
            if (btn.textToInsert == "CLEAR") {
                m_formulaEdit->clear();
            } else if (btn.textToInsert == "BACKSPACE") {
                m_formulaEdit->backspace();
            } else {
                insertText(btn.textToInsert);
            }
        });

        col++;
        if (col >= 4) {
            col = 0;
            rRow++;
        }
    }

    rightLay->addWidget(gridContainer);

    // 4. Action bottom row
    auto* actionLay = new QHBoxLayout();
    actionLay->addStretch();
    
    auto* cancelBtn = new QPushButton("Cancelar", this);
    cancelBtn->setStyleSheet(
        "QPushButton { "
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #FFFFFF, stop:0.45 #F8FAFC, stop:0.46 #F1F5F9, stop:1 #E2E8F0); "
        "  border: 1.5px solid #CBD5E1; "
        "  border-radius: 8px; "
        "  color: #475569; "
        "  font-weight: bold; "
        "  padding: 8px 24px; "
        "} "
        "QPushButton:hover { "
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #F8FAFC, stop:0.45 #F1F5F9, stop:0.46 #E2E8F0, stop:1 #CBD5E1); "
        "  border-color: #94A3B8; "
        "  color: #334155; "
        "} "
        "QPushButton:pressed { "
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #E2E8F0, stop:1 #94A3B8); "
        "}"
    );
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    actionLay->addWidget(cancelBtn);

    auto* confirmBtn = new QPushButton("Confirmar Fórmula", this);
    confirmBtn->setStyleSheet(
        "QPushButton { "
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #A7F3D0, stop:0.45 #34D399, stop:0.46 #059669, stop:1 #047857); "
        "  border: 1.5px solid #059669; "
        "  border-radius: 8px; "
        "  color: white; "
        "  font-weight: bold; "
        "  padding: 8px 24px; "
        "} "
        "QPushButton:hover { "
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #34D399, stop:0.45 #059669, stop:0.46 #047857, stop:1 #065F46); "
        "  border-color: #047857; "
        "} "
        "QPushButton:pressed { "
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #047857, stop:1 #064E3B); "
        "}"
    );
    connect(confirmBtn, &QPushButton::clicked, this, &MathFormulaDialog::onConfirm);
    actionLay->addWidget(confirmBtn);

    rightLay->addLayout(actionLay);
    mainLay->addWidget(rightContainer);
}

void MathFormulaDialog::insertText(const QString& text) {
    int pos = m_formulaEdit->cursorPosition();
    m_formulaEdit->insert(text);
    
    if (text.endsWith("()")) {
        m_formulaEdit->setCursorPosition(pos + text.length() - 1);
    } else if (text.endsWith("(,)")) {
        m_formulaEdit->setCursorPosition(pos + text.length() - 2);
    } else if (text.endsWith("(0,pi,,)")) {
        m_formulaEdit->setCursorPosition(pos + text.length() - 2);
    }
    m_formulaEdit->setFocus();
}

void MathFormulaDialog::updatePreview() {
    m_lcdScreen->setHtml(parseFormulaToHtml(m_formula));
}

void MathFormulaDialog::onConfirm() {
    accept();
}

QString MathFormulaDialog::parseFormulaToHtml(const QString& formula) {
    QString html = formula;
    
    html.replace(" ", ""); 
    
    html.replace("pi", "&pi;");
    html.replace("PI", "&pi;");
    html.replace("π", "&pi;");
    
    QRegularExpression fracRegex("fraction\\(([^,]+),([^\\)]+)\\)");
    while (true) {
        QRegularExpressionMatch m = fracRegex.match(html);
        if (!m.hasMatch()) break;
        QString top = m.captured(1);
        QString bottom = m.captured(2);
        QString replacement = QString(
            "<table style='display:inline-table; border-collapse:collapse; margin: 0 4px; vertical-align:middle;'>"
            "<tr><td style='border-bottom:1.5px solid #1C2D27; text-align:center; padding: 0 4px; font-size:11px;'>%1</td></tr>"
            "<tr><td style='text-align:center; padding: 0 4px; font-size:11px;'>%2</td></tr>"
            "</table>"
        ).arg(top).arg(bottom);
        html.replace(m.capturedStart(0), m.capturedLength(0), replacement);
    }
    
    QRegularExpression intRegex("integral\\(([^,]+),([^,]+),([^\\)]+)\\)");
    while (true) {
        QRegularExpressionMatch m = intRegex.match(html);
        if (!m.hasMatch()) break;
        QString lower = m.captured(1);
        QString upper = m.captured(2);
        QString expr = m.captured(3);
        QString replacement = QString(
            "<table style='display:inline-table; border-collapse:collapse; margin: 0 6px; vertical-align:middle;'>"
            "<tr>"
            "  <td style='font-size: 8px; text-align:center; padding-bottom:1px;'>%2</td>"
            "  <td rowspan='2' style='font-size: 28px; font-weight: normal; vertical-align: middle; font-family: serif;'>&int;</td>"
            "  <td rowspan='2' style='vertical-align: middle; padding-left: 3px; font-size:12px;'>%3 dx</td>"
            "</tr>"
            "<tr>"
            "  <td style='font-size: 8px; text-align:center; padding-top:1px;'>%1</td>"
            "</tr>"
            "</table>"
        ).arg(lower).arg(upper).arg(expr);
        html.replace(m.capturedStart(0), m.capturedLength(0), replacement);
    }
    
    QRegularExpression sqrtRegex("sqrt\\(([^\\)]+)\\)");
    while (true) {
        QRegularExpressionMatch m = sqrtRegex.match(html);
        if (!m.hasMatch()) break;
        QString expr = m.captured(1);
        QString replacement = QString(
            "<table style='display:inline-table; border-collapse:collapse; vertical-align:middle; margin: 0 2px;'>"
            "<tr>"
            "  <td style='font-size: 16px; vertical-align:middle; padding-right:1px;'>&radic;</td>"
            "  <td style='border-top: 1.5px solid #1C2D27; padding-top:1px; vertical-align:middle; font-size:12px;'>%1</td>"
            "</tr>"
            "</table>"
        ).arg(expr);
        html.replace(m.capturedStart(0), m.capturedLength(0), replacement);
    }
    
    QRegularExpression powRegex("pow\\(([^,]+),([^\\)]+)\\)");
    while (true) {
        QRegularExpressionMatch m = powRegex.match(html);
        if (!m.hasMatch()) break;
        QString base = m.captured(1);
        QString exp = m.captured(2);
        QString replacement = QString("%1<sup>%2</sup>").arg(base).arg(exp);
        html.replace(m.capturedStart(0), m.capturedLength(0), replacement);
    }

    QRegularExpression powShorthand("([A-Za-z0-9_]+|\\([^\\)]+\\))\\^([A-Za-z0-9_]+|\\([^\\)]+\\))");
    while (true) {
        QRegularExpressionMatch m = powShorthand.match(html);
        if (!m.hasMatch()) break;
        QString base = m.captured(1);
        QString exp = m.captured(2);
        QString replacement = QString("%1<sup>%2</sup>").arg(base).arg(exp);
        html.replace(m.capturedStart(0), m.capturedLength(0), replacement);
    }
    
    QStringList functions = {"sin", "cos", "tan", "log", "ln", "abs"};
    for (const auto& func : functions) {
        html.replace(func + "(", QString("<i>%1</i>(").arg(func));
    }
    
    return QString(
        "<html><head><style>"
        "body { font-family: 'Lucida Console', 'Courier New', monospace; color: #1C2D27; margin: 0; padding: 0; }"
        "</style></head><body>"
        "<div style='font-size: 14px; font-weight: bold; line-height: 1.4;'>"
        "%1"
        "</div>"
        "</body></html>"
    ).arg(html);
}
