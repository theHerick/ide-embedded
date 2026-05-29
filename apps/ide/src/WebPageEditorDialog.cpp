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

#include <QListWidget>

// Custom Popup for Quick Search
class WebQuickSearchPopup : public QWidget {
public:
    WebQuickSearchPopup(WebPageEditorDialog* dlg, const QPointF& sPos, const QPoint& gPos) : QWidget(nullptr, Qt::Popup | Qt::FramelessWindowHint), dialog(dlg), scenePos(sPos) {
        setAttribute(Qt::WA_DeleteOnClose);
        
        QVBoxLayout* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);
        
        QLineEdit* searchEdit = new QLineEdit(this);
        searchEdit->setPlaceholderText("Busca rápida...");
        searchEdit->setStyleSheet(
            "QLineEdit { "
            "  background: rgba(255, 255, 255, 0.98); "
            "  border: 2px solid #BFDBFE; "
            "  border-radius: 8px 8px 0 0; "
            "  color: #0F172A; "
            "  font-family: 'Segoe UI', Arial, sans-serif; "
            "  font-size: 12px; "
            "  font-weight: 500; "
            "  padding: 6px 12px; "
            "}"
        );
        layout->addWidget(searchEdit);
        
        QListWidget* listWidget = new QListWidget(this);
        listWidget->setStyleSheet(
            "QListWidget { "
            "  background: #FFFFFF; "
            "  border: 1px solid #BFDBFE; "
            "  border-top: none; "
            "  border-radius: 0 0 8px 8px; "
            "  outline: none; "
            "  font-family: 'Segoe UI', Arial, sans-serif; "
            "  font-size: 11px; "
            "  color: #334155; "
            "}"
            "QListWidget::item { padding: 8px 12px; border-bottom: 1px solid #F1F5F9; }"
            "QListWidget::item:selected { background: #EEF2FF; color: #4F46E5; font-weight: bold; border-left: 3px solid #4F46E5; }"
        );
        listWidget->addItems({"Adicionar Texto", "Adicionar Botão", "Adicionar Input", "Adicionar Gráfico"});
        layout->addWidget(listWidget);
        
        setGeometry(gPos.x(), gPos.y(), 200, 180);
        
        connect(searchEdit, &QLineEdit::textChanged, this, [listWidget](const QString& text) {
            for (int i = 0; i < listWidget->count(); ++i) {
                QListWidgetItem* item = listWidget->item(i);
                item->setHidden(!item->text().contains(text, Qt::CaseInsensitive));
            }
        });
        
        connect(listWidget, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
            if (item->text() == "Adicionar Texto") dialog->addElement("Text", scenePos);
            else if (item->text() == "Adicionar Botão") dialog->addElement("Button", scenePos);
            else if (item->text() == "Adicionar Input") dialog->addElement("Input", scenePos);
            else if (item->text() == "Adicionar Gráfico") dialog->addElement("Chart", scenePos);
            close();
        });
        
        searchEdit->setFocus();
    }
private:
    WebPageEditorDialog* dialog;
    QPointF scenePos;
};

// Custom Scene to handle double click for quick search
class WebScene : public QGraphicsScene {
public:
    WebScene(WebPageEditorDialog* dlg, QObject* parent = nullptr) : QGraphicsScene(parent), dialog(dlg) {}
protected:
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event) override {
        if (!itemAt(event->scenePos(), QTransform())) {
            dialog->showQuickSearch(event->scenePos(), event->screenPos());
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

void WebPageEditorDialog::showQuickSearch(const QPointF& scenePos, const QPoint& globalPos) {
    WebQuickSearchPopup* popup = new WebQuickSearchPopup(this, scenePos, globalPos);
    popup->show();
}
