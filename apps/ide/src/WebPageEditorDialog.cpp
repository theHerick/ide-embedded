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
#include <QTimer>

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
            if (auto* webItem = dynamic_cast<WebElementItem*>(item)) {
                QMenu menu;
                menu.setStyleSheet(
                    "QMenu { background: #FBFBFB; border: 1px solid #E2E8F0; color: #0F172A; padding: 4px; }"
                    "QMenu::item { padding: 6px 20px; }"
                    "QMenu::item:selected { background: #EEF2FF; color: #1D4ED8; }"
                );
                QString type = webItem->elementType();
                
                if (type == "Chart") {
                    menu.addAction("Vincular Múltiplas Variáveis", dialog, [this, webItem](){
                        bool ok;
                        QString var = QInputDialog::getText(nullptr, "Múltiplas Variáveis", "Nomes separados por vírgula:", QLineEdit::Normal, webItem->boundVar(), &ok);
                        if (ok) webItem->setBoundVar(var);
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
    m_enableSwitch = new QCheckBox("Ativar Web Page no ESP (Servidor Web)");
    m_enableSwitch->setChecked(m_data.contains("enabled") ? m_data["enabled"].toBool() : false);
    topLayout->addWidget(m_enableSwitch);
    
    topLayout->addStretch();
    
    m_orientationCombo = new QComboBox();
    m_orientationCombo->addItem("Desktop (16:9)", QSizeF(1280, 720));
    m_orientationCombo->addItem("Mobile (9:16)", QSizeF(405, 720));
    topLayout->addWidget(m_orientationCombo);
    
    QPushButton* btnHelp = new QPushButton("Ajuda");
    connect(btnHelp, &QPushButton::clicked, this, [](){
        QMessageBox::information(nullptr, "Ajuda", "Clique duplo no canvas para adicionar elementos.\nBotão direito no elemento para vincular a variáveis globais do projeto.");
    });
    topLayout->addWidget(btnHelp);
    
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
    m_view->setBackgroundBrush(QColor(230, 240, 250)); // Frutiger aero light blue background
    
    mainLayout->addWidget(m_view);
    
    QHBoxLayout* bottomLayout = new QHBoxLayout();
    QPushButton* btnSave = new QPushButton("Salvar e Fechar");
    connect(btnSave, &QPushButton::clicked, this, &WebPageEditorDialog::saveAndClose);
    bottomLayout->addStretch();
    bottomLayout->addWidget(btnSave);
    
    mainLayout->addLayout(bottomLayout);
    
    if (m_data.contains("elements")) {
        m_elements = m_data["elements"].toArray();
        rebuildScene();
    }
}

WebPageEditorDialog::~WebPageEditorDialog() {}

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

void WebPageEditorDialog::saveAndClose() {
    m_data["enabled"] = m_enableSwitch->isChecked();
    m_data["orientation"] = m_orientationCombo->currentIndex();
    
    QJsonArray newElements;
    for (QGraphicsItem* item : m_scene->items()) {
        if (auto* webItem = dynamic_cast<WebElementItem*>(item)) {
            newElements.append(webItem->toJson());
        }
    }
    m_data["elements"] = newElements;
    
    accept();
}

void WebPageEditorDialog::requestEditEvent(const QString& compId, const QString& eventName) {
    m_editEventCompId = compId;
    m_editEventName = eventName;
    saveAndClose();
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
        "  background: #FBFBFB; "
        "  border: 1px solid #E6EEF3; "
        "  border-radius: 8px; "
        "  color: #0F172A; "
        "  font-family: 'Segoe UI', Arial, sans-serif; "
        "  font-size: 12px; "
        "  font-weight: 500; "
        "  padding: 6px 12px; "
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

    QStringList componentsList;
    componentsList << "Texto" << "Botão" << "Gráfico";

    QCompleter* completer = new QCompleter(componentsList, searchEdit);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    completer->setFilterMode(Qt::MatchContains);
    completer->setCompletionMode(QCompleter::PopupCompletion);

    completer->popup()->setStyleSheet(
        "QAbstractItemView { "
        "  background-color: #FBFBFB; "
        "  border: 1px solid #E6EEF3; "
        "  border-radius: 8px; "
        "  color: #0F172A; "
        "  padding: 4px; "
        "  font-family: 'Segoe UI', Arial, sans-serif; "
        "  font-size: 12px; "
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
        "  background-color: #93C5FD; "
        "  color: white; "
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
