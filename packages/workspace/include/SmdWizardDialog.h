#pragma once
#include <QDialog>
#include <QJsonObject>
#include <QString>
#include <QComboBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>

class SmdWizardDialog : public QDialog {
    Q_OBJECT
public:
    enum ComponentType { RESISTOR, CAPACITOR, LED };
    explicit SmdWizardDialog(ComponentType type, QWidget* parent = nullptr);
    QJsonObject getProperties() const;

private:
    void setupUI(ComponentType type);

    ComponentType m_type;
    QComboBox* m_sizeCombo;
    QComboBox* m_param1Combo;
    QComboBox* m_param2Combo;
    QComboBox* m_param3Combo;
};
