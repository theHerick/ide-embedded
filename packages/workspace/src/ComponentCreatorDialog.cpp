#include "ComponentCreatorDialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QSpinBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QMessageBox>
#include <QRegularExpression>
#include <QDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QClipboard>
#include <QGuiApplication>
#include <QTableWidget>
#include <QHeaderView>
#include <QCheckBox>
#include <QTabWidget>

namespace {

struct ParsedFunction {
    QString name;
    QString fullCode;
};

QVector<ParsedFunction> extractFunctionsFromCode(const QString& source) {
    QVector<ParsedFunction> functions;
    QRegularExpression signatureRegex(
        "(?:^|\\n)\\s*(?:void|int|float|double|bool|String|long|short|char|unsigned|signed)\\s+([A-Za-z_][A-Za-z0-9_]*)\\s*\\([^\\)]*\\)\\s*\\{"
    );

    QRegularExpressionMatchIterator it = signatureRegex.globalMatch(source);
    while (it.hasNext()) {
        QRegularExpressionMatch m = it.next();
        const QString fnName = m.captured(1).trimmed();
        if (fnName.isEmpty()) continue;

        int blockStart = source.indexOf('{', m.capturedStart(0));
        if (blockStart < 0) continue;

        int depth = 0;
        int i = blockStart;
        for (; i < source.size(); ++i) {
            if (source[i] == '{') depth++;
            else if (source[i] == '}') {
                depth--;
                if (depth == 0) break;
            }
        }
        if (i >= source.size() || depth != 0) continue;

        ParsedFunction fn;
        fn.name = fnName;
        fn.fullCode = source.mid(m.capturedStart(0), i - m.capturedStart(0) + 1).trimmed();
        functions.append(fn);
    }
    return functions;
}

} // namespace

ComponentCreatorDialog::ComponentCreatorDialog(QWidget* parent)
    : QDialog(parent) {
    setWindowTitle("Criador de Componentes Customizados");
    resize(950, 850);
    setStyleSheet(
        "QDialog { background-color: #F8FAFC; }"
        "QLabel { color: #334155; font-size: 13px; }"
        "QLineEdit, QPlainTextEdit, QSpinBox, QComboBox, QTableWidget { "
        "  background-color: white; border: 1px solid #CBD5E1; border-radius: 6px; padding: 5px; color: #1E293B; "
        "}"
        "QComboBox QAbstractItemView { background-color: white; color: #1E293B; selection-background-color: #3B82F6; selection-color: white; border: 1px solid #CBD5E1; }"
        "QLineEdit:focus, QPlainTextEdit:focus { border: 2px solid #3B82F6; }"
        "QPushButton { background-color: #FFFFFF; border: 1px solid #CBD5E1; border-radius: 6px; padding: 8px 16px; color: #334155; font-weight: 500; }"
        "QPushButton:hover { background-color: #F1F5F9; border-color: #94A3B8; }"
        "QTabWidget::pane { border: 1px solid #CBD5E1; border-radius: 4px; background: white; }"
        "QTabBar::tab { background: #E2E8F0; padding: 10px 20px; margin-right: 2px; border-top-left-radius: 4px; border-top-right-radius: 4px; color: #64748B; }"
        "QTabBar::tab:selected { background: white; color: #3B82F6; border-bottom: none; }"
    );
    setupUI();
}

void ComponentCreatorDialog::setupUI() {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(20, 20, 20, 20);
    root->setSpacing(15);

    auto* headerLay = new QHBoxLayout();
    auto* header = new QLabel("<b>Novo Componente Customizado</b>", this);
    header->setStyleSheet("font-size: 20px; color: #0F172A;");
    headerLay->addWidget(header);
    headerLay->addStretch();
    
    auto* importBtnTop = new QPushButton("Importar JSON", this);
    importBtnTop->setStyleSheet("background-color: #EFF6FF; color: #2563EB; border-color: #BFDBFE; font-weight: bold;");
    connect(importBtnTop, &QPushButton::clicked, this, &ComponentCreatorDialog::openAiJsonDialog);
    headerLay->addWidget(importBtnTop);
    root->addLayout(headerLay);

    auto* tabs = new QTabWidget(this);
    
    // --- ABA 1: Aparência ---
    auto* tabApp = new QWidget();
    auto* appLay = new QVBoxLayout(tabApp);
    auto* form = new QFormLayout();
    m_nameEdit = new QLineEdit(this); m_nameEdit->setPlaceholderText("Ex: Sensor de Chama"); form->addRow("Nome:", m_nameEdit);
    m_idEdit = new QLineEdit(this); m_idEdit->setPlaceholderText("Ex: sensor_chama"); form->addRow("ID Único:", m_idEdit);
    m_labelEdit = new QLineEdit(this); m_labelEdit->setPlaceholderText("CHAMA"); form->addRow("Label Central:", m_labelEdit);
    
    m_shapeCombo = new QComboBox(this);
    m_shapeCombo->addItem("Retângulo", "rectangle");
    m_shapeCombo->addItem("Círculo", "circle");
    m_shapeCombo->addItem("Cápsula", "capsule");
    form->addRow("Formato Visual:", m_shapeCombo);

    m_categoryCombo = new QComboBox(this);
    m_categoryCombo->addItem("Sensor", "sensor");
    m_categoryCombo->addItem("Atuador", "actuator");
    m_categoryCombo->addItem("Módulo", "module");
    form->addRow("Categoria:", m_categoryCombo);

    m_colorCombo = new QComboBox(this);
    m_colorCombo->addItem("Azul Sky", "#3B82F6");
    m_colorCombo->addItem("Verde Esmeralda", "#10B981");
    m_colorCombo->addItem("Vermelho Coral", "#EF4444");
    m_colorCombo->addItem("Amarelo Âmbar", "#F59E0B");
    m_colorCombo->addItem("Cinza Slate", "#475569");
    form->addRow("Cor:", m_colorCombo);
    appLay->addLayout(form);

    appLay->addWidget(new QLabel("<b>Pinos do Hardware:</b>"));
    m_pinsTable = new QTableWidget(0, 2, this);
    m_pinsTable->setHorizontalHeaderLabels({"Nome do Pino", "Gerar Macro?"});
    m_pinsTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    appLay->addWidget(m_pinsTable);
    
    auto* pinBtns = new QHBoxLayout();
    auto* addPinBtn = new QPushButton("Adicionar Pino", this);
    connect(addPinBtn, &QPushButton::clicked, this, &ComponentCreatorDialog::addPinRow);
    pinBtns->addWidget(addPinBtn);
    appLay->addLayout(pinBtns);
    
    tabs->addTab(tabApp, "1. Visual e Pinos");

    // --- ABA 2: Eventos ---
    auto* tabEv = new QWidget();
    auto* evLay = new QVBoxLayout(tabEv);
    evLay->addWidget(new QLabel("<b>Definição de Eventos (Hooks):</b>"));
    m_hooksTable = new QTableWidget(0, 2, this);
    m_hooksTable->setHorizontalHeaderLabels({"Nome do Evento (ex: aoDetectar)", "Lógica"});
    m_hooksTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    evLay->addWidget(m_hooksTable);
    auto* addHookBtn = new QPushButton("Adicionar Hook de Evento", this);
    connect(addHookBtn, &QPushButton::clicked, this, &ComponentCreatorDialog::addHookRow);
    evLay->addWidget(addHookBtn);
    tabs->addTab(tabEv, "2. Eventos");

    // --- ABA 3: Lógica ---
    auto* tabLog = new QWidget();
    auto* logLay = new QVBoxLayout(tabLog);
    
    auto* loopBtn = new QPushButton("🧩 Editar Mecânica do Monitor (Lógica Interna)", this);
    loopBtn->setStyleSheet("background-color: #F0FDF4; color: #15803D; border-color: #BBF7D0; padding: 20px; font-weight: bold; font-size: 14px;");
    connect(loopBtn, &QPushButton::clicked, this, [this](){ openBlockEditor("monitor"); });
    logLay->addWidget(loopBtn);

    logLay->addStretch();
    
    auto* sep = new QFrame(this);
    sep->setFrameShape(QFrame::HLine);
    sep->setFrameShadow(QFrame::Sunken);
    logLay->addWidget(sep);

    logLay->addWidget(new QLabel("<b>Lógica C++ Legada (Apenas se necessário):</b>"));
    m_monitorCodeEdit = new QPlainTextEdit(this);
    m_monitorCodeEdit->setPlaceholderText("Lógica em C++ direto no monitor...");
    m_monitorCodeEdit->setMaximumHeight(100);
    logLay->addWidget(m_monitorCodeEdit);
    
    logLay->addWidget(new QLabel("<b>Funções Auxiliares:</b>"));
    m_functionsEdit = new QPlainTextEdit(this);
    logLay->addWidget(m_functionsEdit);
    tabs->addTab(tabLog, "3. Lógica do Monitor");

    // --- ABA 4: Simulação ---
    auto* tabSim = new QWidget();
    auto* simLay = new QVBoxLayout(tabSim);
    simLay->addWidget(new QLabel("<b>Vínculos de Simulação:</b>"));
    simLay->addWidget(new QLabel("<small>Configure como o componente envia dados para os pinos na simulação.</small>"));
    m_outputsTable = new QTableWidget(0, 4, this);
    m_outputsTable->setHorizontalHeaderLabels({"Parâmetro", "Pino Destino", "Tipo", "Vlr Inicial"});
    m_outputsTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    simLay->addWidget(m_outputsTable);
    auto* addSimBtn = new QPushButton("Adicionar Vínculo de Simulação", this);
    connect(addSimBtn, &QPushButton::clicked, this, &ComponentCreatorDialog::addOutputRow);
    simLay->addWidget(addSimBtn);
    tabs->addTab(tabSim, "4. Simulação");

    // --- ABA 5: Variáveis Globais ---
    auto* tabVars = new QWidget();
    auto* varLay = new QVBoxLayout(tabVars);
    varLay->addWidget(new QLabel("<b>Variáveis do Componente (Expostas):</b>"));
    m_variablesTable = new QTableWidget(0, 3, this);
    m_variablesTable->setHorizontalHeaderLabels({"Nome da Variável", "Tipo", "Vlr Inicial"});
    m_variablesTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    varLay->addWidget(m_variablesTable);
    auto* addVarBtn = new QPushButton("Adicionar Variável Global", this);
    connect(addVarBtn, &QPushButton::clicked, this, &ComponentCreatorDialog::addVariableRow);
    varLay->addWidget(addVarBtn);
    tabs->addTab(tabVars, "5. Variáveis");

    // --- ABA 6: Avançado ---
    auto* tabAdv = new QWidget();
    auto* advLay = new QFormLayout(tabAdv);
    m_includesEdit = new QPlainTextEdit(); m_includesEdit->setMaximumHeight(80); advLay->addRow("Includes:", m_includesEdit);
    m_globalsEdit  = new QPlainTextEdit(); m_globalsEdit->setMaximumHeight(80);  advLay->addRow("Globais:", m_globalsEdit);
    m_setupEdit    = new QPlainTextEdit(); m_setupEdit->setMaximumHeight(80);    advLay->addRow("Setup:", m_setupEdit);
    tabs->addTab(tabAdv, "6. Avançado");

    root->addWidget(tabs);

    auto* bottomBtns = new QHBoxLayout();
    bottomBtns->addStretch();
    auto* cancel = new QPushButton("Cancelar", this);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    bottomBtns->addWidget(cancel);
    auto* save = new QPushButton("Salvar Componente", this);
    save->setStyleSheet("background-color: #2563EB; color: white; font-weight: bold; border: none; padding: 10px 30px;");
    connect(save, &QPushButton::clicked, this, &ComponentCreatorDialog::validateAndSave);
    bottomBtns->addWidget(save);
    root->addLayout(bottomBtns);

    appendPinRow("SIG", true);
}

void ComponentCreatorDialog::addOutputRow() {
    int row = m_outputsTable->rowCount();
    m_outputsTable->insertRow(row);
    
    auto* nameEdit = new QLineEdit(this); nameEdit->setPlaceholderText("ex: Sinal");
    m_outputsTable->setCellWidget(row, 0, nameEdit);
    
    auto* pinCombo = new QComboBox(this);
    pinCombo->addItem("SIG", "SIG");
    for (int i = 0; i < m_pinsTable->rowCount(); ++i) {
        auto* edit = qobject_cast<QLineEdit*>(m_pinsTable->cellWidget(i, 0));
        if (edit && !edit->text().isEmpty() && edit->text() != "SIG") pinCombo->addItem(edit->text(), edit->text());
    }
    m_outputsTable->setCellWidget(row, 1, pinCombo);

    auto* typeCombo = new QComboBox(this);
    typeCombo->addItem("Digital (HIGH/LOW)", "digital");
    typeCombo->addItem("Analógico (0-1023)", "analog");
    typeCombo->addItem("PWM (0-255)", "pwm");
    m_outputsTable->setCellWidget(row, 2, typeCombo);
    
    auto* valEdit = new QLineEdit(this); valEdit->setPlaceholderText("0 ou LOW");
    m_outputsTable->setCellWidget(row, 3, valEdit);
}

void ComponentCreatorDialog::addVariableRow() {
    int row = m_variablesTable->rowCount();
    m_variablesTable->insertRow(row);
    auto* nameEdit = new QLineEdit(this); nameEdit->setPlaceholderText("distancia");
    m_variablesTable->setCellWidget(row, 0, nameEdit);
    auto* typeCombo = new QComboBox(this);
    typeCombo->addItem("float", "float"); typeCombo->addItem("int", "int"); typeCombo->addItem("bool", "bool");
    m_variablesTable->setCellWidget(row, 1, typeCombo);
    auto* valEdit = new QLineEdit(this); valEdit->setPlaceholderText("0");
    m_variablesTable->setCellWidget(row, 2, valEdit);
}

void ComponentCreatorDialog::removeSelectedPinRow() {
    int row = m_pinsTable->currentRow();
    if (row >= 0) m_pinsTable->removeRow(row);
}

void ComponentCreatorDialog::addPinRow() { appendPinRow("", true); }
void ComponentCreatorDialog::addHookRow() {
    int row = m_hooksTable->rowCount();
    m_hooksTable->insertRow(row);
    
    auto* nameEdit = new QLineEdit(this);
    nameEdit->setPlaceholderText("aoDetectar");
    m_hooksTable->setCellWidget(row, 0, nameEdit);

    auto* editBtn = new QPushButton("🧩 Editar Lógica", this);
    editBtn->setStyleSheet("background-color: #FDF2F8; color: #BE185D; border-color: #FBCFE8;");
    m_hooksTable->setCellWidget(row, 1, editBtn);

    connect(editBtn, &QPushButton::clicked, this, [this, nameEdit]() {
        QString evtName = nameEdit->text().trimmed();
        if (evtName.isEmpty()) {
            QMessageBox::warning(this, "Erro", "Defina o nome do evento antes de editar a lógica.");
            return;
        }
        openBlockEditor("event:" + evtName);
    });
}

void ComponentCreatorDialog::appendPinRow(const QString& pinName, bool generateCode) {
    int row = m_pinsTable->rowCount();
    m_pinsTable->insertRow(row);
    auto* nameEdit = new QLineEdit(this); nameEdit->setText(pinName);
    m_pinsTable->setCellWidget(row, 0, nameEdit);
    auto* check = new QCheckBox("Sim", this); check->setChecked(generateCode);
    auto* wrap = new QWidget(); auto* l = new QHBoxLayout(wrap);
    l->setContentsMargins(0,0,0,0); l->addWidget(check); l->setAlignment(Qt::AlignCenter);
    m_pinsTable->setCellWidget(row, 1, wrap);
}

void ComponentCreatorDialog::openBlockEditor(const QString& section) {
    QDialog dialog(this);
    dialog.setWindowTitle("Editor de Lógica Visual - " + section.toUpper());
    dialog.resize(900, 700);
    
    auto* lay = new QVBoxLayout(&dialog);
    auto* editor = new BlockEditor(&dialog);
    lay->addWidget(editor);

    QStringList leds, pots, buzzers, motors;
    for (int i = 0; i < m_pinsTable->rowCount(); ++i) {
        auto* edit = qobject_cast<QLineEdit*>(m_pinsTable->cellWidget(i, 0));
        if (edit && !edit->text().isEmpty()) leds << edit->text();
    }
    
    QStringList hooks;
    for (int i = 0; i < m_hooksTable->rowCount(); ++i) {
        auto* edit = qobject_cast<QLineEdit*>(m_hooksTable->cellWidget(i, 0));
        if (edit && !edit->text().isEmpty()) hooks << edit->text();
    }
    editor->setAvailableHooks(hooks);
    
    editor->loadEventLogic("custom", section, leds, pots, buzzers, motors);
    
    if (section == "setup") editor->setEventBlocks("custom", section, m_setupBlocks);
    else if (section == "loop") editor->setEventBlocks("custom", section, m_loopBlocks);
    else if (section.startsWith("event:")) {
        QString evtName = section.mid(6);
        editor->setEventBlocks("custom", evtName, m_eventLogicBlocks.value(evtName));
    }

    auto* btnRow = new QHBoxLayout();
    btnRow->addStretch();
    auto* ok = new QPushButton("Confirmar Lógica", &dialog);
    ok->setStyleSheet("background-color: #2563EB; color: white; font-weight: bold;");
    btnRow->addWidget(ok);
    lay->addLayout(btnRow);

    connect(ok, &QPushButton::clicked, [&]() {
        if (section == "setup") m_setupBlocks = editor->getActiveBlocks();
        else if (section == "loop") m_loopBlocks = editor->getActiveBlocks();
        else if (section.startsWith("event:")) {
            QString evtName = section.mid(6);
            m_eventLogicBlocks[evtName] = editor->getActiveBlocks();
        }
        dialog.accept();
    });

    dialog.exec();
}

void ComponentCreatorDialog::validateAndSave() {
    m_createdDef = CustomComponentDef();
    m_createdDef.name = m_nameEdit->text().trimmed();
    m_createdDef.type = m_idEdit->text().trimmed().toLower();
    m_createdDef.labelText = m_labelEdit->text().trimmed();
    m_createdDef.shape = m_shapeCombo->currentData().toString();
    m_createdDef.category = m_categoryCombo->currentData().toString();
    m_createdDef.color = m_colorCombo->currentData().toString();
    m_createdDef.customLoop = m_monitorCodeEdit->toPlainText().trimmed();
    m_createdDef.loopBlocks = m_loopBlocks;
    m_createdDef.codeIncludes = m_includesEdit->toPlainText().trimmed();
    m_createdDef.codeSetup = m_setupEdit->toPlainText().trimmed();
    m_createdDef.setupBlocks = m_setupBlocks;

    // Globals + Variables
    QString globals = m_globalsEdit->toPlainText().trimmed();
    for (int i = 0; i < m_variablesTable->rowCount(); ++i) {
        auto* n = qobject_cast<QLineEdit*>(m_variablesTable->cellWidget(i, 0));
        auto* t = qobject_cast<QComboBox*>(m_variablesTable->cellWidget(i, 1));
        auto* v = qobject_cast<QLineEdit*>(m_variablesTable->cellWidget(i, 2));
        if (n && !n->text().isEmpty()) {
            globals += QString("\n%1 %2 = %3;").arg(t->currentText()).arg(n->text()).arg(v->text().isEmpty() ? "0" : v->text());
        }
    }
    m_createdDef.codeGlobals = globals.trimmed();

    // Pins
    for (int i = 0; i < m_pinsTable->rowCount(); ++i) {
        auto* n = qobject_cast<QLineEdit*>(m_pinsTable->cellWidget(i, 0));
        if (!n || n->text().isEmpty()) continue;
        CustomComponentPin p; p.name = n->text(); p.side = (i % 2 == 0) ? "Left" : "Right";
        QWidget* w = m_pinsTable->cellWidget(i, 1);
        if (w) { if (auto* cb = w->findChild<QCheckBox*>()) p.generateCode = cb->isChecked(); }
        m_createdDef.pins.append(p);
    }

    // Hooks
    for (int i = 0; i < m_hooksTable->rowCount(); ++i) {
        auto* n = qobject_cast<QLineEdit*>(m_hooksTable->cellWidget(i, 0));
        if (!n || n->text().isEmpty()) continue;
        CustomEventDef ev; 
        ev.name = n->text().trimmed(); 
        ev.callback = "event" + ev.name;
        ev.conditionBlocks = m_eventLogicBlocks.value(ev.name);
        m_createdDef.customEvents.append(ev);
    }

    // Sim Bindings
    m_createdDef.outputs.clear();
    for (int i = 0; i < m_outputsTable->rowCount(); ++i) {
        auto* n = qobject_cast<QLineEdit*>(m_outputsTable->cellWidget(i, 0));
        auto* p = qobject_cast<QComboBox*>(m_outputsTable->cellWidget(i, 1));
        auto* t = qobject_cast<QComboBox*>(m_outputsTable->cellWidget(i, 2));
        if (n && !n->text().isEmpty()) {
            CustomComponentOutput out;
            out.name = n->text();
            out.type = t->currentData().toString(); // digital, analog, pwm
            out.unit = p->currentText(); // Bind to this pin
            m_createdDef.outputs.append(out);
        }
    }

    const QVector<ParsedFunction> parsed = extractFunctionsFromCode(m_functionsEdit->toPlainText());
    for (const auto& fn : parsed) {
        CustomFunction f; f.name = fn.name; f.code = fn.fullCode;
        m_createdDef.customFunctions.append(f);
    }

    if (m_createdDef.name.isEmpty() || m_createdDef.type.isEmpty()) {
        QMessageBox::warning(this, "Erro", "Nome e ID são obrigatórios.");
        return;
    }
    accept();
}

void ComponentCreatorDialog::openAiJsonDialog() {
    QDialog dialog(this);
    dialog.setWindowTitle("Importar Definição via JSON");
    dialog.resize(750, 750);
    dialog.setStyleSheet(this->styleSheet());

    auto* lay = new QVBoxLayout(&dialog);
    
    lay->addWidget(new QLabel("<b>Template para IA:</b> (Copie este exemplo e peça para a IA preencher)"));
    
    auto* templateEdit = new QPlainTextEdit(&dialog);
    templateEdit->setReadOnly(true);
    templateEdit->setPlainText(
        "{\n"
        "  \"type\": \"sensor_exemplo\",\n"
        "  \"name\": \"Nome do Componente\",\n"
        "  \"labelText\": \"LABEL\",\n"
        "  \"shape\": \"rectangle\",\n"
        "  \"category\": \"sensor\",\n"
        "  \"color\": \"#3B82F6\",\n"
        "  \"pins\": [\n"
        "    { \"name\": \"SIG\", \"generateCode\": true }\n"
        "  ],\n"
        "  \"setupBlocks\": [], // Blocos visuais serializados\n"
        "  \"loopBlocks\": [],  // Blocos visuais serializados\n"
        "  \"customEvents\": [\n"
        "    { \"name\": \"aoDetectar\", \"blocks\": [] }\n"
        "  ],\n"
        "  \"variables\": [\n"
        "    { \"name\": \"valor\", \"type\": \"float\", \"value\": \"0\" }\n"
        "  ]\n"
        "}"
    );
    lay->addWidget(templateEdit);

    auto* copyBtn = new QPushButton("Copiar Template para Área de Transferência", &dialog);
    connect(copyBtn, &QPushButton::clicked, [templateEdit]() {
        QGuiApplication::clipboard()->setText(templateEdit->toPlainText());
    });
    lay->addWidget(copyBtn);

    lay->addWidget(new QLabel("<br><b>Cole o JSON retornado pela IA aqui:</b>"));
    auto* jsonEdit = new QPlainTextEdit(&dialog);
    lay->addWidget(jsonEdit);

    auto* btnRow = new QHBoxLayout();
    btnRow->addStretch();
    auto* imp = new QPushButton("Importar Agora", &dialog);
    imp->setStyleSheet("background-color: #10B981; color: white; border: none; font-weight: bold; padding: 10px 20px;");
    btnRow->addWidget(imp);
    lay->addLayout(btnRow);

    connect(imp, &QPushButton::clicked, [&]() {
        QJsonDocument doc = QJsonDocument::fromJson(jsonEdit->toPlainText().toUtf8());
        if (doc.isObject()) {
            applyJsonToForm(doc.object());
            dialog.accept();
        } else {
            QMessageBox::warning(&dialog, "Erro", "O JSON fornecido é inválido.");
        }
    });

    dialog.exec();
}

void ComponentCreatorDialog::applyJsonToForm(const QJsonObject& obj) {
    m_nameEdit->setText(obj.value("name").toString());
    m_idEdit->setText(obj.value("type").toString());
    m_labelEdit->setText(obj.value("labelText").toString());
    m_monitorCodeEdit->setPlainText(obj.value("monitorLoop").toString());
    m_setupBlocks = EventLogicBlock::deserializeArray(obj.value("setupBlocks").toArray());
    m_loopBlocks = EventLogicBlock::deserializeArray(obj.value("loopBlocks").toArray());
    
    QJsonArray eventsArr = obj.value("customEvents").toArray();
    m_eventLogicBlocks.clear();
    m_hooksTable->setRowCount(0);
    for (int i = 0; i < eventsArr.size(); ++i) {
        QJsonObject evObj = eventsArr[i].toObject();
        QString name = evObj.value("name").toString();
        m_eventLogicBlocks[name] = EventLogicBlock::deserializeArray(evObj.value("conditionBlocks").toArray());
        addHookRow();
        if (auto* nEdit = qobject_cast<QLineEdit*>(m_hooksTable->cellWidget(i, 0))) nEdit->setText(name);
    }

    m_pinsTable->setRowCount(0);
    for(auto p : obj.value("pins").toArray()) appendPinRow(p.toObject().value("name").toString(), true);
}
