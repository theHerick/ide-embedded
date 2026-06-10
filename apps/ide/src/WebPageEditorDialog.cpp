#include "WebPageEditorDialog.h"
#include "WebElementItem.h"
#include "VariableSystem.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QCheckBox>
#include <QPushButton>
#include <QMenu>
#include <QAction>
#include <QInputDialog>
#include <QGraphicsSceneMouseEvent>
#include <QMessageBox>
#include <QComboBox>

#include <QListWidget>
#include <QCompleter>
#include <QAbstractItemView>

#include <QFile>
#include <QTemporaryFile>
#include <QStandardPaths>
#include <QDesktopServices>
#include <QUrl>
#include <QDir>
#include <QJsonArray>
#include <QVarLengthArray>
#include <QKeyEvent>
#include <QPointer>
#include <QTimer>
#include <QFormLayout>
#include <QSpinBox>
#include <QLineEdit>
#include <QDialogButtonBox>
#include <QPointer>
#include <QTimer>
#include <QTimer>

#include <QDialogButtonBox>
#include <QLabel>
#include "MainWindow.h"

// Dialog for selecting multiple variables
class MultiVarSelectionDialog : public QDialog {
public:
    MultiVarSelectionDialog(const QStringList& allVars, const QString& currentBound, QWidget* parent = nullptr) 
        : QDialog(parent) 
    {
        setWindowTitle("Vincular Múltiplas Variáveis");
        setMinimumWidth(300);
        QVBoxLayout* layout = new QVBoxLayout(this);
        
        QLabel* lbl = new QLabel("Selecione as variáveis para exibir no gráfico:");
        lbl->setStyleSheet("color: #0F172A;");
        layout->addWidget(lbl);
        
        QListWidget* listWidget = new QListWidget(this);
        listWidget->setStyleSheet(
            "QListWidget { border: 1px solid #CBD5E1; border-radius: 4px; padding: 4px; outline: none; background: #FFFFFF; color: #0F172A; }"
            "QListWidget::item { padding: 4px; border-bottom: 1px solid #F1F5F9; color: #0F172A; }"
        );
        
        QStringList currentList = currentBound.split(",", Qt::SkipEmptyParts);
        for (QString& s : currentList) s = s.trimmed();
        
        for (const QString& var : allVars) {
            QListWidgetItem* item = new QListWidgetItem(var, listWidget);
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(currentList.contains(var) ? Qt::Checked : Qt::Unchecked);
        }
        layout->addWidget(listWidget);
        
        QDialogButtonBox* btnBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        connect(btnBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(btnBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
        layout->addWidget(btnBox);
        
        m_list = listWidget;
    }
    
    QString getSelectedVars() const {
        QStringList selected;
        for (int i = 0; i < m_list->count(); ++i) {
            if (m_list->item(i)->checkState() == Qt::Checked) {
                selected << m_list->item(i)->text();
            }
        }
        return selected.join(", ");
    }
    
private:
    QListWidget* m_list;
};

// Custom Scene to handle double click for quick search
class WebScene : public QGraphicsScene {
public:
    WebScene(WebPageEditorDialog* dlg, QObject* parent = nullptr) : QGraphicsScene(parent), dialog(dlg) {}
protected:
    void drawBackground(QPainter *painter, const QRectF &rect) override {
        QGraphicsScene::drawBackground(painter, rect);
        
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);

        // 1. Fill external background with a beautiful cyan linear gradient (simulating the web dashboard body)
        QLinearGradient bgGrad(rect.topLeft(), rect.bottomRight());
        bgGrad.setColorAt(0.0, QColor("#e0f7fa"));
        bgGrad.setColorAt(1.0, QColor("#b2ebf2"));
        painter->fillRect(rect, bgGrad);

        // 2. Draw active container boundary (sceneRect) with rounded glass effect and dropshadow
        QRectF sRect = sceneRect();

        // Subtle drop shadow around the canvas
        for (int i = 1; i <= 8; ++i) {
            painter->setPen(QPen(QColor(0, 0, 0, 15 - i * 1.5), 1));
            painter->setBrush(Qt::NoBrush);
            painter->drawRoundedRect(sRect.adjusted(-i, -i, i, i), 20 + i, 20 + i);
        }

        // Fill the canvas area (glassmorphism: rgba(255, 255, 255, 0.4) as in the simulated dashboard container)
        QColor containerBg(255, 255, 255, 102); // 102 / 255 = 40%
        painter->setPen(Qt::NoPen);
        painter->setBrush(containerBg);
        painter->drawRoundedRect(sRect, 20, 20);

        // Clip grid lines exclusively inside the container area
        painter->save();
        QPainterPath clipPath;
        clipPath.addRoundedRect(sRect, 20, 20);
        painter->setClipPath(clipPath);

        // Soft white grid lines
        int gridSize = 20;
        qreal left = int(sRect.left()) - (int(sRect.left()) % gridSize);
        qreal top = int(sRect.top()) - (int(sRect.top()) % gridSize);
        
        QVarLengthArray<QLineF, 100> lines;
        for (qreal x = left; x < sRect.right(); x += gridSize)
            lines.append(QLineF(x, sRect.top(), x, sRect.bottom()));
        for (qreal y = top; y < sRect.bottom(); y += gridSize)
            lines.append(QLineF(sRect.left(), y, sRect.right(), y));

        painter->setPen(QPen(QColor(255, 255, 255, 80), 1, Qt::SolidLine));
        painter->drawLines(lines.data(), lines.size());
        
        painter->restore();

        // 3. Draw container border (1px translucent white: rgba(255, 255, 255, 0.8))
        painter->setPen(QPen(QColor(255, 255, 255, 204), 1.5));
        painter->setBrush(Qt::NoBrush);
        painter->drawRoundedRect(sRect, 20, 20);

        // 4. Draw limits label above the container
        painter->setPen(QColor("#01579b")); // Dark blue text
        QFont font = painter->font();
        font.setBold(true);
        font.setPixelSize(12);
        painter->setFont(font);
        painter->drawText(sRect.topLeft() + QPointF(10, -8), QString("ÁREA VISÍVEL (%1x%2)").arg(sRect.width()).arg(sRect.height()));

        painter->restore();
    }

    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event) override {
        if (!itemAt(event->scenePos(), QTransform())) {
            // Map scene pos to view pos
            QPoint viewPos = dialog->getView()->mapFromScene(event->scenePos());
            dialog->showQuickSearch(event->scenePos(), viewPos);
        } else {
            QGraphicsScene::mouseDoubleClickEvent(event);
        }
    }
    
    void contextMenuEvent(QGraphicsSceneContextMenuEvent *event) override {
        if (auto* item = itemAt(event->scenePos(), QTransform())) {
            WebElementItem* webItem = dynamic_cast<WebElementItem*>(item);
            if (!webItem && item->parentItem()) {
                webItem = dynamic_cast<WebElementItem*>(item->parentItem());
            }
            if (webItem) {
                QMenu menu;
                menu.setStyleSheet(
                    "QMenu { background: #FBFBFB; border: 1px solid #E2E8F0; color: #0F172A; padding: 4px; }"
                    "QMenu::item { padding: 6px 20px; }"
                    "QMenu::item:selected { background: #EEF2FF; color: #1D4ED8; }"
                );
                QString type = webItem->elementType();
                
                if (type == "Chart") {
                    menu.addAction("Vincular Múltiplas Variáveis", dialog, [this, webItem](){
                        MultiVarSelectionDialog varDlg(dialog->getAvailableVars(), webItem->boundVar(), dialog);
                        if (varDlg.exec() == QDialog::Accepted) {
                            webItem->setBoundVar(varDlg.getSelectedVars());
                        }
                    });
                    menu.addAction("Editar Tipo de Gráfico", dialog, [this, webItem](){
                        QStringList items = {"Linha (line)", "Barra (bar)", "Pizza (pie)", "Doughnut (doughnut)"};
                        QString current = webItem->chartType();
                        int currentIndex = 0;
                        if (current == "bar") currentIndex = 1;
                        else if (current == "pie") currentIndex = 2;
                        else if (current == "doughnut") currentIndex = 3;
                        
                        bool ok;
                        QString res = QInputDialog::getItem(dialog, "Tipo de Gráfico", "Selecione o tipo:", items, currentIndex, false, &ok);
                        if (ok && !res.isEmpty()) {
                            if (res.contains("line")) webItem->setChartType("line");
                            else if (res.contains("bar")) webItem->setChartType("bar");
                            else if (res.contains("pie")) webItem->setChartType("pie");
                            else if (res.contains("doughnut")) webItem->setChartType("doughnut");
                        }
                    });
                } else if (type == "Button") {
                    menu.addAction("Editar Evento: Ao Clicar", dialog, [this, webItem](){
                        dialog->requestEditEvent(webItem->id(), "aoClicar");
                    });
                } else if (type == "Text") {
                    menu.addAction("Editar Evento: Atualizar Texto", dialog, [this, webItem](){
                        dialog->requestEditEvent(webItem->id(), "aoAtualizar");
                    });
                    menu.addAction("Editar Formatação", dialog, [this, webItem](){
                        QDialog fmtDlg(dialog);
                        fmtDlg.setWindowTitle("Formatação de Texto");
                        fmtDlg.setStyleSheet(
                            "QDialog { background: #F8F9FA; }"
                            "QLabel, QCheckBox { color: #1E293B; font-weight: bold; font-size: 13px; }"
                            "QSpinBox, QLineEdit { background: #FFFFFF; color: #0F172A; border: 1px solid #CBD5E1; padding: 5px; border-radius: 6px; }"
                            "QSpinBox::up-button, QSpinBox::down-button { width: 20px; }"
                        );
                        auto* layout = new QFormLayout(&fmtDlg);
                        auto* sizeSpin = new QSpinBox();
                        sizeSpin->setRange(8, 100);
                        sizeSpin->setValue(webItem->formatSize());
                        auto* colorEdit = new QLineEdit(webItem->formatColor());
                        auto* boldCheck = new QCheckBox("Negrito");
                        boldCheck->setChecked(webItem->formatBold());
                        layout->addRow("Tamanho (px):", sizeSpin);
                        layout->addRow("Cor (Hex):", colorEdit);
                        layout->addRow("", boldCheck);
                        auto* btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
                        layout->addRow(btns);
                        connect(btns, &QDialogButtonBox::accepted, &fmtDlg, &QDialog::accept);
                        connect(btns, &QDialogButtonBox::rejected, &fmtDlg, &QDialog::reject);
                        if (fmtDlg.exec() == QDialog::Accepted) {
                            webItem->setFormatSize(sizeSpin->value());
                            webItem->setFormatColor(colorEdit->text());
                            webItem->setFormatBold(boldCheck->isChecked());
                        }
                    });
                } else if (type == "Input") {
                    menu.addAction("Editar Evento: Ao Alterar Valor", dialog, [this, webItem](){
                        dialog->requestEditEvent(webItem->id(), "aoAlterar");
                    });
                } else if (type == "Slider") {
                    menu.addAction("Editar Evento: Ao Alterar Valor", dialog, [this, webItem](){
                        if (MainWindow* mainWin = qobject_cast<MainWindow*>(dialog->parent())) {
                            if (mainWin->getActiveTutorial() == 3 && mainWin->getTutorialOverlay() && mainWin->getTutorialOverlay()->currentStep() == 7) {
                                mainWin->getTutorialOverlay()->advance();
                            }
                        }
                        dialog->requestEditEvent(webItem->id(), "aoAlterar");
                    });
                    menu.addAction("Editar Evento: Ao Desligar (aoDesligar)", dialog, [this, webItem](){
                        dialog->requestEditEvent(webItem->id(), "aoDesligar");
                    });
                }
                
                menu.addSeparator();
                menu.addAction("Deletar", dialog, [this, webItem](){
                    removeItem(webItem);
                    delete webItem;
                });
                menu.exec(event->screenPos());
                return;
            }
        }
        QGraphicsScene::contextMenuEvent(event);
    }
private:
    WebPageEditorDialog* dialog;
};

WebPageEditorDialog::WebPageEditorDialog(QJsonObject& data, const QStringList& availableVars, QWidget* parent)
    : QDialog(parent), m_data(data), m_availableVars(availableVars)
{
    setWindowTitle("Construtor Web Page / Dashboard ESP");
    resize(1340, 880);
    
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    
    QHBoxLayout* topLayout = new QHBoxLayout();
    
    m_enableSwitch = new QCheckBox("Habilitar WebPage");
    m_enableSwitch->setObjectName("webEnableSwitch");
    m_enableSwitch->setChecked(m_data.contains("enabled") ? m_data["enabled"].toBool() : false);
    m_enableSwitch->setStyleSheet(
        "QCheckBox { color: #0F172A; font-weight: bold; font-size: 12px; }"
    );
    connect(m_enableSwitch, &QCheckBox::stateChanged, this, [this](int state) {
        if (state == Qt::Checked) {
            if (MainWindow* mainWin = qobject_cast<MainWindow*>(parentWidget())) {
                if (mainWin->getActiveTutorial() == 3 && mainWin->getTutorialOverlay() && mainWin->getTutorialOverlay()->currentStep() == 13) {
                    mainWin->getTutorialOverlay()->advance();
                }
            }
        }
    });
    topLayout->addWidget(m_enableSwitch);
    
    // Auth Panel
    m_authEnable = new QCheckBox("Exigir Senha");
    m_authEnable->setChecked(m_data.contains("auth_enabled") ? m_data["auth_enabled"].toBool() : false);
    m_authEnable->setStyleSheet("QCheckBox { color: #0F172A; font-size: 11px; margin-left: 10px; }");
    topLayout->addWidget(m_authEnable);
    
    m_authUser = new QLineEdit();
    m_authUser->setPlaceholderText("Usuário");
    m_authUser->setText(m_data.contains("auth_user") ? m_data["auth_user"].toString() : "admin");
    m_authUser->setFixedWidth(80);
    m_authUser->setStyleSheet("QLineEdit { border: 1px solid #CBD5E1; border-radius: 4px; padding: 2px 4px; font-size: 11px; }");
    topLayout->addWidget(m_authUser);
    
    m_authPass = new QLineEdit();
    m_authPass->setPlaceholderText("Senha");
    m_authPass->setText(m_data.contains("auth_pass") ? m_data["auth_pass"].toString() : "1234");
    m_authPass->setEchoMode(QLineEdit::Password);
    m_authPass->setFixedWidth(80);
    m_authPass->setStyleSheet("QLineEdit { border: 1px solid #CBD5E1; border-radius: 4px; padding: 2px 4px; font-size: 11px; }");
    topLayout->addWidget(m_authPass);

    topLayout->addStretch();
    
    m_orientationCombo = new QComboBox();
    m_orientationCombo->addItem("Desktop (16:9)", QSizeF(1280, 720));
    m_orientationCombo->addItem("Mobile (9:16)", QSizeF(405, 720));
    m_orientationCombo->setStyleSheet(
        "QComboBox { "
        "  background: #FFFFFF; "
        "  border: 1px solid #CBD5E1; "
        "  border-radius: 4px; "
        "  padding: 4px 10px; "
        "  color: #0F172A; "
        "  font-weight: 500; "
        "}"
        "QComboBox::drop-down { border: none; }"
        "QComboBox QAbstractItemView { "
        "  background: #FFFFFF; "
        "  color: #0F172A; "
        "  selection-background-color: #EEF2FF; "
        "  selection-color: #1D4ED8; "
        "}"
    );
    topLayout->addWidget(m_orientationCombo);
    
    mainLayout->addLayout(topLayout);
    
    m_scene = new WebScene(this, this);
    m_scene->setSceneRect(0, 0, 1280, 720);
    
    // Connect combo box
    connect(m_orientationCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        QSizeF size = m_orientationCombo->itemData(index).toSizeF();
        m_scene->setSceneRect(0, 0, size.width(), size.height());
        m_view->update();
    });
    
    // Restore saved orientation if any
    if (m_data.contains("orientation")) {
        int idx = m_data["orientation"].toInt();
        if (idx >= 0 && idx < m_orientationCombo->count()) {
            m_orientationCombo->setCurrentIndex(idx);
        }
    }
    
    m_view = new QGraphicsView(m_scene);
    m_view->setObjectName("webScene");
    m_view->setRenderHint(QPainter::Antialiasing);
    m_view->setBackgroundBrush(Qt::white);
    
    mainLayout->addWidget(m_view);
    
    if (m_data.contains("elements")) {
        m_elements = m_data["elements"].toArray();
        rebuildScene();
    }
}

WebPageEditorDialog::~WebPageEditorDialog() {}

bool WebPageEditorDialog::eventFilter(QObject* watched, QEvent* event) {
    if (watched->objectName() == "floatingSearchBoxWeb") {
        if (event->type() == QEvent::KeyPress) {
            auto* keyEvent = static_cast<QKeyEvent*>(event);
            if (keyEvent->key() == Qt::Key_Escape) {
                watched->deleteLater();
                return true;
            }
        } else if (event->type() == QEvent::FocusOut) {
            QLineEdit* edit = qobject_cast<QLineEdit*>(watched);
            if (edit) {
                QCompleter* completer = edit->completer();
                if (completer && completer->popup() && completer->popup()->isVisible()) {
                    if (completer->popup()->geometry().contains(QCursor::pos())) {
                        return true; // Intercept focus out to prevent line edit from hiding popup on click
                    }
                }
                QPointer<QLineEdit> guardedEdit(edit);
                QTimer::singleShot(150, this, [guardedEdit]() {
                    if (guardedEdit) guardedEdit->deleteLater();
                });
            }
            return true;
        }
    }
    return QDialog::eventFilter(watched, event);
}

void WebPageEditorDialog::rebuildScene() {
    for (const QJsonValue& val : m_elements) {
        QJsonObject obj = val.toObject();
        WebElementItem* item = new WebElementItem(obj);
        m_scene->addItem(item);
    }
}

void WebPageEditorDialog::addElement(const QString& type, const QPointF& pos) {
    QJsonObject obj;
    obj["type"] = type;
    
    // Generate a default ID based on type
    static int counter = 1;
    obj["id"] = QString("web%1_%2").arg(type.toLower()).arg(counter++);
    
    obj["x"] = pos.x();
    obj["y"] = pos.y();
    
    if (type == "Chart") {
        obj["width"] = 300;
        obj["height"] = 200;
        obj["text"] = "Gráfico";
    }
    
    WebElementItem* item = new WebElementItem(obj);
    m_scene->addItem(item);
    
    if (MainWindow* mainWin = qobject_cast<MainWindow*>(parent())) {
        if (mainWin->getActiveTutorial() == 3 && type == "Slider" && mainWin->getTutorialOverlay() && mainWin->getTutorialOverlay()->currentStep() == 6) {
            mainWin->getTutorialOverlay()->advance();
        }
    }
}

void WebPageEditorDialog::done(int r) {
    if (MainWindow* mainWin = qobject_cast<MainWindow*>(parentWidget())) {
        if (mainWin->getActiveTutorial() == 3 && mainWin->getTutorialOverlay() && mainWin->getTutorialOverlay()->currentStep() == 14) {
            mainWin->getTutorialOverlay()->advance();
        }
    }

    m_data.insert("enabled", m_enableSwitch->isChecked());
    m_data.insert("auth_enabled", m_authEnable->isChecked());
    m_data.insert("auth_user", m_authUser->text());
    m_data.insert("auth_pass", m_authPass->text());
    m_data.insert("orientation", m_orientationCombo->currentIndex());
    
    QJsonArray newElements;
    for (QGraphicsItem* item : m_scene->items()) {
        if (auto* webItem = dynamic_cast<WebElementItem*>(item)) {
            newElements.append(webItem->toJson());
        }
    }
    m_data.insert("elements", newElements);
    
    QDialog::done(r);
}

void WebPageEditorDialog::requestEditEvent(const QString& compId, const QString& eventName) {
    m_editEventCompId = compId;
    m_editEventName = eventName;
    accept();
}

// Stubs for header slots
void WebPageEditorDialog::handleDoubleClick(const QPoint&) {}
void WebPageEditorDialog::showContextMenu(const QPoint&) {}

void WebPageEditorDialog::showQuickSearch(const QPointF& scenePos, const QPoint& viewPos) {
    if (m_view->viewport()->findChild<QLineEdit*>("floatingSearchBoxWeb")) {
        return;
    }

    QLineEdit* searchEdit = new QLineEdit(m_view->viewport());
    searchEdit->setObjectName("floatingSearchBoxWeb");
    searchEdit->setPlaceholderText("Adicionar componente...");
    searchEdit->setStyleSheet(
        "QLineEdit#floatingSearchBoxWeb { "
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 rgba(255, 255, 255, 0.95), stop:0.48 rgba(235, 248, 255, 0.9), stop:0.5 rgba(200, 235, 255, 0.95), stop:1 rgba(185, 230, 250, 0.95)); "
        "  border: 2px solid rgba(130, 190, 230, 0.8); "
        "  border-radius: 12px; "
        "  color: #003050; "
        "  font-family: 'Segoe UI', Arial, sans-serif; "
        "  font-size: 13px; "
        "  font-weight: 600; "
        "  padding: 8px 14px; "
        "  min-width: 180px; "
        "}"
    );

    searchEdit->adjustSize();
    int w = searchEdit->width();
    int h = searchEdit->height();

    // Bound coordinates to viewport boundaries
    int x = qMax(10, qMin(viewPos.x() - w/2, m_view->viewport()->width() - w - 10));
    int y = qMax(10, qMin(viewPos.y() - h/2, m_view->viewport()->height() - h - 10));

    searchEdit->setGeometry(x, y, w, h);
    searchEdit->show();
    searchEdit->raise();
    searchEdit->setFocus(Qt::OtherFocusReason);
    searchEdit->installEventFilter(this);

    QStringList componentsList;
    componentsList << "Texto" << "Botão" << "Slider" << "LED Virtual" << "Input Texto";

    QCompleter* completer = new QCompleter(componentsList, searchEdit);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    completer->setFilterMode(Qt::MatchContains);
    completer->setCompletionMode(QCompleter::PopupCompletion);

    completer->popup()->setStyleSheet(
        "QAbstractItemView { "
        "  background-color: rgba(255, 255, 255, 0.95); "
        "  border: 2px solid rgba(130, 190, 230, 0.8); "
        "  border-radius: 10px; "
        "  color: #003050; "
        "  padding: 4px; "
        "  font-family: 'Segoe UI', Arial, sans-serif; "
        "  font-size: 12px; "
        "  font-weight: 600; "
        "}"
        "QAbstractItemView::item { "
        "  padding: 8px 12px; "
        "  border-radius: 6px; "
        "  color: #003050; "
        "}"
        "QAbstractItemView::item:hover { "
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #EAF5FF, stop:1 #D4EAFF); "
        "  color: #0050A0; "
        "}"
        "QAbstractItemView::item:selected { "
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #BAE0FF, stop:1 #80C8FF); "
        "  color: #003050; "
        "}"
    );
    searchEdit->setCompleter(completer);

    auto adjustPopupSize = [completer]() {
        if (!completer) return;
        QAbstractItemView* popup = completer->popup();
        if (!popup) return;
        int count = completer->completionCount();
        if (count == 0) return;
        int maxVisible = completer->maxVisibleItems();
        int visibleCount = qMin(count, maxVisible);
        int itemHeight = 31;
        int totalHeight = visibleCount * itemHeight;
        int frameHeight = popup->frameWidth() * 2;
        int padding = 10;
        popup->setFixedHeight(totalHeight + frameHeight + padding);
    };

    connect(searchEdit, &QLineEdit::textChanged, searchEdit, [adjustPopupSize]() {
        QTimer::singleShot(10, adjustPopupSize);
    });

    QTimer::singleShot(50, searchEdit, [completer, adjustPopupSize]() {
        completer->complete();
        adjustPopupSize();
    });

    auto handleAddition = [this, searchEdit, scenePos](const QString& textVal) {
        if (!searchEdit || searchEdit->property("processed").toBool()) return;
        searchEdit->setProperty("processed", true);

        QString text = textVal.trimmed();
        if (text == "Texto") addElement("Text", scenePos);
        else if (text == "Botão") addElement("Button", scenePos);
        else if (text == "Gráfico") addElement("Chart", scenePos);
        else if (text == "Slider") addElement("Slider", scenePos);
        else if (text == "LED Virtual") addElement("LED", scenePos);
        else if (text == "Input Texto") addElement("Input", scenePos);

        searchEdit->deleteLater();
    };

    connect(completer, static_cast<void(QCompleter::*)(const QString&)>(&QCompleter::activated), searchEdit, handleAddition);

    connect(completer->popup(), &QAbstractItemView::clicked, searchEdit, [completer, handleAddition](const QModelIndex& index) {
        QString textVal = completer->popup()->model()->data(index, Qt::DisplayRole).toString();
        handleAddition(textVal);
    });

    connect(searchEdit, &QLineEdit::returnPressed, searchEdit, [handleAddition, searchEdit]() {
        handleAddition(searchEdit->text());
    });

    // Close when losing focus
    connect(searchEdit, &QLineEdit::editingFinished, searchEdit, [searchEdit]() {
        if (!searchEdit->property("processed").toBool()) {
            QTimer::singleShot(100, searchEdit, &QObject::deleteLater);
        }
    });
}
