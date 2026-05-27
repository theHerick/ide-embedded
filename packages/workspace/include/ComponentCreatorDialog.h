#pragma once

#include <QDialog>

class QLineEdit;
class QComboBox;
class QSpinBox;
class QPlainTextEdit;
class QTableWidget;

#include "CustomComponent.h"

class ComponentCreatorDialog : public QDialog {
    Q_OBJECT
public:
    explicit ComponentCreatorDialog(QWidget* parent = nullptr);
    ~ComponentCreatorDialog() override = default;

    CustomComponentDef getCreatedComponent() const { return m_createdDef; }

private slots:
    void addPinRow();
    void addHookRow();
    void addOutputRow();
    void addVariableRow();
    void removeSelectedPinRow();
    void openAiJsonDialog();
    void openBlockEditor(const QString& section);
    void validateAndSave();

private:
    QLineEdit* m_nameEdit = nullptr;
    QLineEdit* m_idEdit = nullptr;
    QLineEdit* m_labelEdit = nullptr;
    QComboBox* m_shapeCombo = nullptr;
    QComboBox* m_categoryCombo = nullptr;
    QComboBox* m_colorCombo = nullptr;
    QSpinBox* m_widthSpin = nullptr;
    QSpinBox* m_heightSpin = nullptr;
    QTableWidget* m_pinsTable = nullptr;
    QTableWidget* m_hooksTable = nullptr;
    QTableWidget* m_outputsTable = nullptr;
    QTableWidget* m_variablesTable = nullptr;

    QVector<EventLogicBlock> m_setupBlocks;
    QVector<EventLogicBlock> m_loopBlocks;
    QMap<QString, QVector<EventLogicBlock>> m_functionBlocks;
    QMap<QString, QVector<EventLogicBlock>> m_eventLogicBlocks;

    QPlainTextEdit* m_monitorCodeEdit = nullptr;
    QPlainTextEdit* m_functionsEdit = nullptr;
    QPlainTextEdit* m_includesEdit = nullptr;
    QPlainTextEdit* m_globalsEdit = nullptr;
    QPlainTextEdit* m_setupEdit = nullptr;
    QPlainTextEdit* m_loopEdit = nullptr;

    CustomComponentDef m_createdDef;

    void setupUI();
    void applyJsonToForm(const QJsonObject& obj);
    void appendPinRow(const QString& pinName, bool generateCode);
};
