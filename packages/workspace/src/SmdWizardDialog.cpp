#include "SmdWizardDialog.h"

SmdWizardDialog::SmdWizardDialog(ComponentType type, QWidget* parent) 
    : QDialog(parent), m_type(type) {
    setupUI(type);
    setStyleSheet("QDialog { background: #FFFFFF; border: 1px solid #E6EEF3; }"
                  "QLabel { color: #0F172A; font-weight: bold; font-family: 'Segoe UI', Arial; font-size: 13px; }"
                  "QComboBox { padding: 5px; font-size: 13px; border: 1px solid #CBD5E1; border-radius: 4px; }"
                  "QPushButton { background: #2563EB; color: #FFFFFF; font-weight: bold; padding: 8px 16px; border-radius: 6px; }");
}

void SmdWizardDialog::setupUI(ComponentType type) {
    auto* layout = new QVBoxLayout(this);
    
    // Size
    auto* sizeLayout = new QHBoxLayout();
    sizeLayout->addWidget(new QLabel("Tamanho SMD:", this));
    m_sizeCombo = new QComboBox(this);
    if (type == LED) {
        m_sizeCombo->addItems({"0603", "0805", "1206", "5050"});
    } else {
        m_sizeCombo->addItems({"0402", "0603", "0805", "1206"});
    }
    sizeLayout->addWidget(m_sizeCombo);
    layout->addLayout(sizeLayout);

    // Param 1
    auto* p1Layout = new QHBoxLayout();
    m_param1Combo = new QComboBox(this);
    if (type == RESISTOR) {
        p1Layout->addWidget(new QLabel("Resistência:", this));
        m_param1Combo->addItems({"10Ω", "22Ω", "100Ω", "220Ω", "330Ω", "1KΩ", "4.7KΩ", "10KΩ", "100KΩ", "1MΩ"});
    } else if (type == CAPACITOR) {
        p1Layout->addWidget(new QLabel("Capacitância:", this));
        m_param1Combo->addItems({"100pF", "1nF", "10nF", "100nF", "1uF", "10uF", "100uF"});
    } else if (type == LED) {
        p1Layout->addWidget(new QLabel("Cor:", this));
        m_param1Combo->addItems({"Vermelho", "Verde", "Azul", "Branco", "Amarelo", "RGB"});
    }
    p1Layout->addWidget(m_param1Combo);
    layout->addLayout(p1Layout);

    // Param 2
    auto* p2Layout = new QHBoxLayout();
    m_param2Combo = new QComboBox(this);
    if (type == RESISTOR) {
        p2Layout->addWidget(new QLabel("Potência:", this));
        m_param2Combo->addItems({"1/16W", "1/10W", "1/8W", "1/4W"});
    } else if (type == CAPACITOR) {
        p2Layout->addWidget(new QLabel("Tensão:", this));
        m_param2Combo->addItems({"6.3V", "10V", "16V", "25V", "50V"});
    } else if (type == LED) {
        p2Layout->addWidget(new QLabel("Brilho:", this));
        m_param2Combo->addItems({"Normal", "Alto brilho", "Difuso"});
    }
    p2Layout->addWidget(m_param2Combo);
    layout->addLayout(p2Layout);

    // Param 3 (Tolerance for Resistor)
    m_param3Combo = nullptr;
    if (type == RESISTOR) {
        auto* p3Layout = new QHBoxLayout();
        p3Layout->addWidget(new QLabel("Tolerância:", this));
        m_param3Combo = new QComboBox(this);
        m_param3Combo->addItems({"±1%", "±5%"});
        p3Layout->addWidget(m_param3Combo);
        layout->addLayout(p3Layout);
    }

    auto* btnLayout = new QHBoxLayout();
    auto* btnOk = new QPushButton("Adicionar Componente", this);
    connect(btnOk, &QPushButton::clicked, this, &QDialog::accept);
    btnLayout->addStretch();
    btnLayout->addWidget(btnOk);
    layout->addLayout(btnLayout);

    setWindowTitle(type == RESISTOR ? "Configurar Mini-Resistor" :
                   type == CAPACITOR ? "Configurar Mini-Capacitor" : "Configurar Mini-LED");
}

QJsonObject SmdWizardDialog::getProperties() const {
    QJsonObject props;
    props["smdSize"] = m_sizeCombo->currentText();
    if (m_type == RESISTOR) {
        props["resistance"] = m_param1Combo->currentText();
        props["power"] = m_param2Combo->currentText();
        props["tolerance"] = m_param3Combo->currentText();
    } else if (m_type == CAPACITOR) {
        props["capacitance"] = m_param1Combo->currentText();
        props["voltage"] = m_param2Combo->currentText();
    } else if (m_type == LED) {
        props["color"] = m_param1Combo->currentText();
        props["brightness"] = m_param2Combo->currentText();
    }
    return props;
}
