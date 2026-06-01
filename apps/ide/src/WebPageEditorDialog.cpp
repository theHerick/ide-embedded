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
        
        // Draw Grid
        int gridSize = 20;
        qreal left = int(rect.left()) - (int(rect.left()) % gridSize);
        qreal top = int(rect.top()) - (int(rect.top()) % gridSize);
        
        QVarLengthArray<QLineF, 100> lines;
        for (qreal x = left; x < rect.right(); x += gridSize)
            lines.append(QLineF(x, rect.top(), x, rect.bottom()));
        for (qreal y = top; y < rect.bottom(); y += gridSize)
            lines.append(QLineF(rect.left(), y, rect.right(), y));
            
        painter->setPen(QPen(QColor(235, 235, 235), 1, Qt::SolidLine));
        painter->drawLines(lines.data(), lines.size());

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
                        dialog->requestEditEvent(webItem->id(), "aoAlterar");
                    });
                    menu.addAction("Editar Evento: Ao Zerar Valor", dialog, [this, webItem](){
                        dialog->requestEditEvent(webItem->id(), "aoZerar");
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
    m_view->setRenderHint(QPainter::Antialiasing);
    m_view->setBackgroundBrush(Qt::white);
    
    mainLayout->addWidget(m_view);
    
    QHBoxLayout* bottomLayout = new QHBoxLayout();
    QPushButton* btnSave = new QPushButton("Salvar e Fechar");
    btnSave->setStyleSheet(
        "QPushButton { "
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #A7F3D0, stop:0.48 #34D399, stop:0.5 #10B981, stop:1 #059669); "
        "  border: 1px solid #047857; "
        "  border-radius: 6px; "
        "  color: white; "
        "  padding: 6px 16px; "
        "  font-weight: bold; "
        "  font-size: 13px; "
        "}"
        "QPushButton:hover { "
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #6EE7B7, stop:0.48 #10B981, stop:0.5 #059669, stop:1 #047857); "
        "}"
        "QPushButton:pressed { "
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #059669, stop:1 #047857); "
        "}"
    );
    connect(btnSave, &QPushButton::clicked, this, [this](){ accept(); });
    bottomLayout->addStretch();
    
    QPushButton* btnPreview = new QPushButton("Preview Web");
    btnPreview->setStyleSheet(
        "QPushButton { "
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #BAE6FD, stop:0.48 #38BDF8, stop:0.5 #0284C7, stop:1 #0369A1); "
        "  border: 1px solid #0284C7; "
        "  border-radius: 6px; "
        "  color: white; "
        "  padding: 6px 16px; "
        "  font-weight: bold; "
        "  font-size: 13px; "
        "}"
        "QPushButton:hover { "
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #E0F2FE, stop:0.48 #7DD3FC, stop:0.5 #0284C7, stop:1 #0369A1); "
        "}"
        "QPushButton:pressed { "
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #0369A1, stop:1 #0284C7); "
        "}"
    );
    connect(btnPreview, &QPushButton::clicked, this, [this](){
        // Generate static HTML for preview
        QString html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
        html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
        html += "<script src='https://cdn.jsdelivr.net/npm/chart.js'></script>";
        html += "<style>";
        html += "body { margin: 0; padding: 20px; font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; ";
        html += "background: linear-gradient(135deg, #e0f7fa 0%, #b2ebf2 100%); min-height: 100vh; }";
        html += ".container { position: relative; width: 100%; height: 80vh; background: rgba(255, 255, 255, 0.4); ";
        html += "border-radius: 20px; box-shadow: 0 8px 32px rgba(0,0,0,0.1); backdrop-filter: blur(10px); border: 1px solid rgba(255,255,255,0.8); overflow: hidden; }";
        html += ".elem { position: absolute; }";
        html += "button.elem { background: linear-gradient(180deg, #4fc3f7 0%, #0288d1 100%); color: white; border: none; border-radius: 30px; ";
        html += "box-shadow: 0 4px 15px rgba(2,136,209,0.4), inset 0 2px 5px rgba(255,255,255,0.5); text-shadow: 0 1px 2px rgba(0,0,0,0.2); ";
        html += "cursor: pointer; font-weight: bold; font-size: 14px; transition: all 0.2s ease; }";
        html += "button.elem:active { transform: translateY(2px); box-shadow: 0 2px 5px rgba(2,136,209,0.4); }";
        html += "input.elem { background: rgba(255,255,255,0.7); border: 1px solid #81d4fa; border-radius: 10px; padding: 8px 15px; ";
        html += "box-shadow: inset 0 2px 4px rgba(0,0,0,0.05); outline: none; font-size: 14px; color: #01579b; }";
        html += "input.elem:focus { border-color: #0288d1; background: rgba(255,255,255,0.9); }";
        html += "input[type='range'].elem { -webkit-appearance: none; background: #e0e0e0; height: 8px; border-radius: 4px; outline: none; }";
        html += "input[type='range'].elem::-webkit-slider-thumb { -webkit-appearance: none; width: 20px; height: 20px; border-radius: 50%; background: #0288d1; cursor: pointer; box-shadow: 0 2px 5px rgba(0,0,0,0.3); }";
        html += ".led { border-radius: 50%; border: 2px solid #b91c1c; box-shadow: inset 0 -2px 6px rgba(0,0,0,0.4), 0 2px 8px rgba(0,0,0,0.2); }";
        html += ".text.elem { font-size: 16px; color: #01579b; font-weight: 600; text-shadow: 0 1px 1px rgba(255,255,255,0.8); }";
        html += "</style></head><body>";
        html += "<div class='container'>";
        
        QJsonArray elements;
        for (QGraphicsItem* item : m_scene->items()) {
            if (auto* webItem = dynamic_cast<WebElementItem*>(item)) {
                elements.append(webItem->toJson());
            }
        }
        
        for (int i = 0; i < elements.size(); ++i) {
            QJsonObject el = elements[i].toObject();
            QString type = el["type"].toString();
            QString id = el["id"].toString();
            int x = el["x"].toInt();
            int y = el["y"].toInt();
            QString text = el["text"].toString().toHtmlEscaped();
            
            if (type == "Text") {
                int fs = el.contains("formatSize") ? el["formatSize"].toInt() : 16;
                QString fc = el.contains("formatColor") ? el["formatColor"].toString() : "#01579b";
                bool fb = el.contains("formatBold") ? el["formatBold"].toBool() : true;
                QString fw = fb ? "bold" : "normal";
                html += QString("<div class='elem text' style='left:%1px; top:%2px; font-size:%3px; color:%4; font-weight:%5;' id='%6'>%7</div>\n")
                    .arg(x).arg(y).arg(fs).arg(fc).arg(fw).arg(id).arg(text);
            } else if (type == "Button") {
                html += QString("<button class='elem' style='left:%1px; top:%2px; width:%3px; height:%4px;'>%5</button>\n")
                    .arg(x).arg(y).arg(el["width"].toInt(100)).arg(el["height"].toInt(40)).arg(text);
            } else if (type == "Input") {
                html += QString("<input type='text' class='elem' style='left:%1px; top:%2px; width:%3px; height:%4px;' value=''>\n")
                    .arg(x).arg(y).arg(el["width"].toInt(150)).arg(el["height"].toInt(30));
            } else if (type == "Chart") {
                html += QString("<div class='elem' style='left:%1px; top:%2px; width:%3px; height:%4px; background:rgba(255,255,255,0.9); border-radius:10px; border:1px solid #4fc3f7; padding:10px; box-shadow: 0 4px 6px rgba(0,0,0,0.1);'><canvas id='%5'></canvas></div>\n")
                    .arg(x).arg(y).arg(el["width"].toInt(300)).arg(el["height"].toInt(200)).arg(id);
            } else if (type == "Slider") {
                html += QString("<input type='range' class='elem' min='0' max='255' value='0' style='left:%1px; top:%2px; width:%3px;'>\n")
                    .arg(x).arg(y).arg(el["width"].toInt(150));
            } else if (type == "LED") {
                int size = el.contains("width") ? qMin(el["width"].toInt(), el["height"].toInt()) : 40;
                html += QString("<div class='elem led' style='left:%1px; top:%2px; width:%3px; height:%3px; background-color:#ef4444;'></div>\n")
                    .arg(x).arg(y).arg(size);
            }
        }
        html += "</div>";
        
        // Initialize charts for preview
        html += "<script>\n";
        html += "const charts = {};\n";
        for (int i = 0; i < elements.size(); ++i) {
            QJsonObject el = elements[i].toObject();
            if (el["type"].toString() == "Chart") {
                QString id = el["id"].toString();
                QString boundVar = el["boundVar"].toString();
                if (boundVar.isEmpty()) boundVar = el["text"].toString();
                
                QString chartType = el.contains("chartType") ? el["chartType"].toString() : "line";
                QStringList vars = boundVar.split(",");
                QStringList colors = {"#ef4444", "#3b82f6", "#10b981", "#f59e0b", "#8b5cf6", "#ec4899", "#6366f1"};
                
                if (chartType == "pie" || chartType == "doughnut" || chartType == "bar") {
                    QString labelsStr = "[";
                    QString colorsStr = "[";
                    for (int j = 0; j < vars.size(); ++j) {
                        QString v = vars[j].trimmed();
                        if (v.isEmpty()) continue;
                        labelsStr += QString("'%1',").arg(v);
                        colorsStr += QString("'%1',").arg(colors[j % colors.size()]);
                    }
                    if (labelsStr.endsWith(",")) labelsStr.chop(1);
                    if (colorsStr.endsWith(",")) colorsStr.chop(1);
                    labelsStr += "]";
                    colorsStr += "]";
                    
                    html += QString("charts['%1'] = new Chart(document.getElementById('%1').getContext('2d'), { type: '%2', data: { labels: %3, datasets: [{data: Array(%4).fill(10), backgroundColor: %5, borderWidth: 1}] }, options: { responsive: true, maintainAspectRatio: false, animation: false, plugins: { legend: { position: 'right' } } } });\n")
                        .arg(id).arg(chartType).arg(labelsStr).arg(vars.size()).arg(colorsStr);
                } else {
                    QString datasetsStr = "[";
                    for (int j = 0; j < vars.size(); ++j) {
                        QString v = vars[j].trimmed();
                        if (v.isEmpty()) continue;
                        QString col = colors[j % colors.size()];
                        datasetsStr += QString("{label: '%1', data: [], borderColor: '%2', backgroundColor: '%255', tension: 0.4, pointRadius: 0, borderWidth: 2},").arg(v).arg(col);
                    }
                    if (datasetsStr.endsWith(",")) datasetsStr.chop(1);
                    datasetsStr += "]";
                    
                    html += QString("charts['%1'] = new Chart(document.getElementById('%1').getContext('2d'), { type: '%2', data: { labels: [], datasets: %3 }, options: { responsive: true, maintainAspectRatio: false, animation: false, scales: { x: { display: false }, y: { beginAtZero: true } } } });\n")
                        .arg(id).arg(chartType).arg(datasetsStr);
                }
            }
        }
        html += "setInterval(() => {\n";
        html += "  for (let id in charts) {\n";
        html += "    let c = charts[id];\n";
        html += "    if (c.config.type === 'pie' || c.config.type === 'doughnut' || c.config.type === 'bar') {\n";
        html += "       for (let i = 0; i < c.data.datasets[0].data.length; i++) {\n";
        html += "           c.data.datasets[0].data[i] = Math.max(1, c.data.datasets[0].data[i] + (Math.random() - 0.5) * 10);\n";
        html += "       }\n";
        html += "    } else {\n";
        html += "       c.data.labels.push('');\n";
        html += "       if(c.data.labels.length > 20) c.data.labels.shift();\n";
        html += "       for(let ds of c.data.datasets) {\n";
        html += "          ds.data.push(Math.random() * 100);\n";
        html += "          if(ds.data.length > 20) ds.data.shift();\n";
        html += "       }\n";
        html += "    }\n";
        html += "    c.update();\n";
        html += "  }\n";
        html += "}, 1000);\n";
        html += "</script></body></html>";
        
        QString path = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/ide_preview.html";
        QFile f(path);
        if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            f.write(html.toUtf8());
            f.close();
            QDesktopServices::openUrl(QUrl::fromLocalFile(path));
        }
    });

    QPushButton* btnSair = new QPushButton("Sair");
    btnSair->setStyleSheet(
        "QPushButton { "
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #FECACA, stop:0.48 #F87171, stop:0.5 #DC2626, stop:1 #B91C1C); "
        "  border: 1px solid #B91C1C; "
        "  border-radius: 6px; "
        "  color: white; "
        "  padding: 6px 16px; "
        "  font-weight: bold; "
        "  font-size: 13px; "
        "}"
        "QPushButton:hover { "
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #FEE2E2, stop:0.48 #FCA5A5, stop:0.5 #DC2626, stop:1 #B91C1C); "
        "}"
        "QPushButton:pressed { "
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #B91C1C, stop:1 #DC2626); "
        "}"
    );
    connect(btnSair, &QPushButton::clicked, this, [this](){ reject(); });

    bottomLayout->addWidget(btnPreview);
    bottomLayout->addWidget(btnSair);
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
    m_data["auth_enabled"] = m_authEnable->isChecked();
    m_data["auth_user"] = m_authUser->text();
    m_data["auth_pass"] = m_authPass->text();
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
        else if (text == "Slider") addElement("Slider", scenePos);
        else if (text == "LED Virtual") addElement("LED", scenePos);
        else if (text == "Input Texto") addElement("Input", scenePos);

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
