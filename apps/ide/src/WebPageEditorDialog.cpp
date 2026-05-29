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
#include <QKeyEvent>
#include <QPointer>
#include <QTimer>
#include <QTimer>

#include <QDialogButtonBox>
#include <QLabel>

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
        layout->addWidget(lbl);
        
        QListWidget* listWidget = new QListWidget(this);
        listWidget->setStyleSheet(
            "QListWidget { border: 1px solid #CBD5E1; border-radius: 4px; padding: 4px; outline: none; }"
            "QListWidget::item { padding: 4px; border-bottom: 1px solid #F1F5F9; }"
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
        // Draw a dashed border to represent the screen boundary
        QPen pen(Qt::darkGray, 2, Qt::DashLine);
        painter->setPen(pen);
        painter->drawRect(sceneRect());
        
        // Label it
        painter->setPen(Qt::gray);
        painter->drawText(sceneRect().topLeft() + QPointF(5, -5), QString("Área Visível (%1x%2)").arg(sceneRect().width()).arg(sceneRect().height()));
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
                } else if (type == "Button") {
                    menu.addAction("Editar Evento: Ao Clicar", dialog, [this, webItem](){
                        dialog->requestEditEvent(webItem->id(), "aoClicar");
                    });
                } else if (type == "Text") {
                    menu.addAction("Editar Evento: Atualizar Texto", dialog, [this, webItem](){
                        dialog->requestEditEvent(webItem->id(), "aoAtualizar");
                    });
                } else if (type == "Input") {
                    menu.addAction("Editar Evento: Ao Alterar Valor", dialog, [this, webItem](){
                        dialog->requestEditEvent(webItem->id(), "aoAlterar");
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
    resize(800, 600);
    
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    
    QHBoxLayout* topLayout = new QHBoxLayout();
    
    m_enableSwitch = new QCheckBox("Habilitar WebPage");
    m_enableSwitch->setChecked(m_data.contains("enabled") ? m_data["enabled"].toBool() : false);
    m_enableSwitch->setStyleSheet(
        "QCheckBox { color: #0F172A; font-weight: bold; font-size: 12px; }"
    );
    topLayout->addWidget(m_enableSwitch);
    
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
    m_view->setRenderHint(QPainter::Antialiasing);
    
    QLinearGradient aeroGradient(0, 0, 0, 1000);
    aeroGradient.setColorAt(0.0, QColor(195, 235, 255)); // Light sky blue
    aeroGradient.setColorAt(0.4, QColor(140, 205, 245)); // Glossy mid blue
    aeroGradient.setColorAt(0.41, QColor(115, 185, 235)); // Glossy sharp drop
    aeroGradient.setColorAt(1.0, QColor(170, 240, 195)); // Light grass green
    m_view->setBackgroundBrush(QBrush(aeroGradient));
    
    mainLayout->addWidget(m_view);
    
    QHBoxLayout* bottomLayout = new QHBoxLayout();
    QPushButton* btnSave = new QPushButton("Salvar e Fechar");
    btnSave->setStyleSheet(
        "QPushButton { background-color: #10B981; color: white; border: none; border-radius: 4px; padding: 6px 16px; font-weight: bold; }"
        "QPushButton:hover { background-color: #059669; }"
    );
    connect(btnSave, &QPushButton::clicked, this, [this](){ accept(); });
    bottomLayout->addStretch();
    bottomLayout->addWidget(btnSave);
    
    mainLayout->addLayout(bottomLayout);
    
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
}

void WebPageEditorDialog::done(int r) {
    m_data["enabled"] = m_enableSwitch->isChecked();
    m_data["orientation"] = m_orientationCombo->currentIndex();
    
    QJsonArray newElements;
    for (QGraphicsItem* item : m_scene->items()) {
        if (auto* webItem = dynamic_cast<WebElementItem*>(item)) {
            newElements.append(webItem->toJson());
        }
    }
    m_data["elements"] = newElements;
    
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
    componentsList << "Texto" << "Botão" << "Gráfico";

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

    QTimer::singleShot(50, searchEdit, [completer]() {
        completer->complete();
    });

    auto handleAddition = [this, searchEdit, scenePos](const QString& textVal) {
        if (!searchEdit || searchEdit->property("processed").toBool()) return;
        searchEdit->setProperty("processed", true);

        QString text = textVal.trimmed();
        if (text == "Texto") addElement("Text", scenePos);
        else if (text == "Botão") addElement("Button", scenePos);
        else if (text == "Gráfico") addElement("Chart", scenePos);

        searchEdit->deleteLater();
    };

    connect(completer, static_cast<void(QCompleter::*)(const QString&)>(&QCompleter::activated), searchEdit, handleAddition);
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
