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

// Custom Scene to handle double click for quick search
class WebScene : public QGraphicsScene {
public:
    WebScene(WebPageEditorDialog* dlg, QObject* parent = nullptr) : QGraphicsScene(parent), dialog(dlg) {}
protected:
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event) override {
        if (!itemAt(event->scenePos(), QTransform())) {
            QMenu menu;
            menu.addAction("Adicionar Texto", dialog, [this, event](){ dialog->addElement("Text"); });
            menu.addAction("Adicionar Botão", dialog, [this, event](){ dialog->addElement("Button"); });
            menu.addAction("Adicionar Input", dialog, [this, event](){ dialog->addElement("Input"); });
            menu.addAction("Adicionar Gráfico", dialog, [this, event](){ dialog->addElement("Chart"); });
            menu.exec(event->screenPos());
        } else {
            QGraphicsScene::mouseDoubleClickEvent(event);
        }
    }
    
    void contextMenuEvent(QGraphicsSceneContextMenuEvent *event) override {
        if (auto* item = itemAt(event->scenePos(), QTransform())) {
            if (auto* webItem = dynamic_cast<WebElementItem*>(item)) {
                QMenu menu;
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
    
    QPushButton* btnHelp = new QPushButton("Ajuda");
    connect(btnHelp, &QPushButton::clicked, this, [](){
        QMessageBox::information(nullptr, "Ajuda", "Clique duplo no canvas para adicionar elementos.\nBotão direito no elemento para vincular a variáveis globais do projeto.");
    });
    topLayout->addStretch();
    topLayout->addWidget(btnHelp);
    
    mainLayout->addLayout(topLayout);
    
    m_scene = new WebScene(this, this);
    m_scene->setSceneRect(0, 0, 1200, 800);
    
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

void WebPageEditorDialog::addElement(const QString& type) {
    QJsonObject obj;
    obj["type"] = type;
    
    // Generate a default ID based on type
    static int counter = 1;
    obj["id"] = QString("web%1_%2").arg(type.toLower()).arg(counter++);
    
    // Default position at center of view roughly
    obj["x"] = 100;
    obj["y"] = 100;
    
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
void WebPageEditorDialog::showQuickSearch(const QPoint&) {}
