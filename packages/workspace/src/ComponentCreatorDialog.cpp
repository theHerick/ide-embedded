#include "ComponentCreatorDialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QTableWidget>
#include <QHeaderView>
#include <QTabWidget>
#include <QPushButton>
#include <QMessageBox>
#include <QTableWidgetItem>

ComponentCreatorDialog::ComponentCreatorDialog(QWidget* parent)
    : QDialog(parent) {
    setWindowTitle("Criador de Componentes Customizados");
    resize(980, 780);
    setStyleSheet(
        "QDialog { background-color: #F8FAFC; }"
        "QLabel { color: #334155; font-size: 13px; font-family: 'Segoe UI', Arial; }"
        "QLineEdit, QSpinBox, QTableWidget { "
        "  background-color: white; border: 1px solid #CBD5E1; border-radius: 6px; padding: 5px; color: #1E293B; "
        "}"
        "QLineEdit:focus { border: 2px solid #3B82F6; }"
        "QPushButton { background-color: #FFFFFF; border: 1px solid #CBD5E1; border-radius: 6px; padding: 8px 16px; color: #334155; font-weight: 500; }"
        "QPushButton:hover { background-color: #F1F5F9; border-color: #94A3B8; }"
        "QTabWidget::pane { border: 1px solid #CBD5E1; border-radius: 4px; background: white; }"
        "QTabBar::tab { background: #E2E8F0; padding: 10px 20px; margin-right: 2px; border-top-left-radius: 4px; border-top-right-radius: 4px; color: #64748B; font-weight: 600; }"
        "QTabBar::tab:selected { background: white; color: #3B82F6; border-bottom: none; }"
    );
    setupUI();
}

void ComponentCreatorDialog::setupUI() {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(20, 20, 20, 20);
    root->setSpacing(15);

    auto* header = new QLabel("<b>Criar Novo Componente por Blocos</b>", this);
    header->setStyleSheet("font-size: 18px; color: #0F172A;");
    root->addWidget(header);

    auto* tabs = new QTabWidget(this);

    // ─────────────────────────────────────────────────────────────────────────
    // ABA 1: CONFIGURAÇÃO DO HARDWARE
    // ─────────────────────────────────────────────────────────────────────────
    auto* tabConfig = new QWidget();
    auto* configLay = new QVBoxLayout(tabConfig);
    configLay->setContentsMargins(16, 16, 16, 16);
    configLay->setSpacing(12);

    auto* form = new QFormLayout();
    form->setSpacing(10);

    m_nameEdit = new QLineEdit(this);
    m_nameEdit->setPlaceholderText("Ex: Sensor de Presença");
    form->addRow("Nome do Componente:", m_nameEdit);

    m_pinsCountSpin = new QSpinBox(this);
    m_pinsCountSpin->setRange(1, 40);
    m_pinsCountSpin->setValue(2);
    form->addRow("Quantidade de Pinos:", m_pinsCountSpin);

    configLay->addLayout(form);

    configLay->addWidget(new QLabel("<b>Nomes dos Pinos:</b>"));
    m_pinsTable = new QTableWidget(2, 1, this);
    m_pinsTable->setHorizontalHeaderLabels({"Nome do Pino"});
    m_pinsTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    
    // Default pin names
    m_pinsTable->setItem(0, 0, new QTableWidgetItem("SIG"));
    m_pinsTable->setItem(1, 0, new QTableWidgetItem("GND"));

    configLay->addWidget(m_pinsTable);

    connect(m_pinsCountSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &ComponentCreatorDialog::updatePinTableRows);

    tabs->addTab(tabConfig, "1. Configuração do Hardware");

    // ─────────────────────────────────────────────────────────────────────────
    // ABA 2: LÓGICA DO MONITOR (BLOCO EDITOR EMBUTIDO)
    // ─────────────────────────────────────────────────────────────────────────
    auto* tabLogic = new QWidget();
    auto* logicLay = new QVBoxLayout(tabLogic);
    logicLay->setContentsMargins(8, 8, 8, 8);
    logicLay->setSpacing(8);

    m_blockEditor = new BlockEditor(this);
    logicLay->addWidget(m_blockEditor, 1);

    tabs->addTab(tabLogic, "2. Lógica do Monitor (Blocos)");

    // Synchronize pin names to the BlockEditor palette when switching tabs
    connect(tabs, &QTabWidget::currentChanged, this, [this](int index) {
        if (index == 1) { // Lógica do Monitor
            QStringList pinNames;
            for (int i = 0; i < m_pinsTable->rowCount(); ++i) {
                auto* item = m_pinsTable->item(i, 0);
                if (item && !item->text().isEmpty()) {
                    pinNames << item->text().trimmed();
                } else {
                    pinNames << QString("PIN%1").arg(i + 1);
                }
            }
            m_blockEditor->loadEventLogic("custom", "monitor", pinNames, {}, {}, {}, {}, {});
        }
    });

    root->addWidget(tabs, 1);

    // Bottom Action Row
    auto* bottomRow = new QHBoxLayout();
    bottomRow->addStretch();
    auto* cancel = new QPushButton("Cancelar", this);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    bottomRow->addWidget(cancel);

    auto* save = new QPushButton("Salvar Componente", this);
    save->setStyleSheet("background-color: #2563EB; color: white; font-weight: bold; border: none; padding: 10px 24px; border-radius: 6px;");
    connect(save, &QPushButton::clicked, this, &ComponentCreatorDialog::validateAndSave);
    bottomRow->addWidget(save);

    root->addLayout(bottomRow);
}

void ComponentCreatorDialog::updatePinTableRows(int count) {
    int prev = m_pinsTable->rowCount();
    m_pinsTable->setRowCount(count);
    if (count > prev) {
        for (int i = prev; i < count; ++i) {
            m_pinsTable->setItem(i, 0, new QTableWidgetItem(QString("PIN%1").arg(i + 1)));
        }
    }
}

void ComponentCreatorDialog::validateAndSave() {
    QString name = m_nameEdit->text().trimmed();
    if (name.isEmpty()) {
        QMessageBox::warning(this, "Erro", "Por favor, defina o nome do componente.");
        return;
    }

    // Auto-generate safe identifiers from name
    QString typeId = name.toLower();
    typeId.replace(QRegularExpression("[^a-z0-9_]"), "_");
    typeId.remove(QRegularExpression("^_+|_+$"));
    if (typeId.isEmpty()) typeId = "custom_component";

    m_createdDef = CustomComponentDef();
    m_createdDef.name = name;
    m_createdDef.type = typeId;
    m_createdDef.labelText = name.left(6).toUpper();
    m_createdDef.shape = "rectangle";
    m_createdDef.category = "digital_trigger"; // Default category
    m_createdDef.color = "#3B82F6"; // Slate Blue default

    // Gather Pins
    for (int i = 0; i < m_pinsTable->rowCount(); ++i) {
        auto* item = m_pinsTable->item(i, 0);
        QString pinName = (item && !item->text().isEmpty()) ? item->text().trimmed() : QString("PIN%1").arg(i + 1);

        CustomComponentPin p;
        p.name = pinName;
        // Distribute pins visually on Left and Right sides
        p.side = (i % 2 == 0) ? "Left" : "Right";
        p.color = "#475569";
        p.generateCode = true;

        m_createdDef.pins.append(p);
    }

    // Gather blocks from the embedded visual editor
    m_createdDef.loopBlocks = m_blockEditor->getActiveBlocks();

    // Automatically parse EVENT_CREATE blocks to declare custom event handlers
    m_createdDef.customEvents.clear();
    for (const auto& block : m_createdDef.loopBlocks) {
        if (block.type == LogicBlockType::EVENT_CREATE) {
            QString rawName = block.actionTarget.trimmed();
            if (rawName.endsWith("()")) {
                rawName.chop(2);
            }
            // Sanitize event name
            rawName.replace(QRegularExpression("[^A-Za-z0-9_]"), "");
            if (!rawName.isEmpty()) {
                CustomEventDef ev;
                ev.name = rawName;
                ev.callback = "event" + rawName;
                m_createdDef.customEvents.append(ev);
            }
        }
    }

    accept();
}
