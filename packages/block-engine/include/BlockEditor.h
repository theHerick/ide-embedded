#pragma once
#include <QWidget>
#include <QListWidget>
#include <QPushButton>
#include <QComboBox>
#include <QLineEdit>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMap>
#include <QVector>
#include <QDropEvent>
#include <QDragEnterEvent>
#include <QJsonArray>
#include <QJsonObject>
#include <QDialog>
#include <QTextBrowser>
#include "VariableSystem.h"

// Custom line edit to accept visual variables (drag-and-drop)
class VariableSlotEdit : public QLineEdit {
    Q_OBJECT
public:
    explicit VariableSlotEdit(QWidget* parent = nullptr);
protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
signals:
    void variableDropped(const QString& varName);
};

enum class LogicBlockType {
    ASSIGNMENT,   // x = y
    CONDITION,    // if (x)
    ACTION,       // custom hardware action (e.g. led.on())
    MATH,         // target = operand1 operator operand2
    CREATE_VAR,   // type name;
    FIM,          // }
    SERIAL_PRINT, // Serial.print(...)
    EEPROM_OP,    // Salvar/Ler EEPROM
    EVENT_CREATE  // Novo: eventCreate
};

struct EventLogicBlock {
    QString id;
    LogicBlockType type;
    
    // Assignment
    QString assignTarget; 
    QString assignExpression; 

    // Condition
    QString conditionExpression;

    // Action
    QString actionTarget;
    QString actionCommand;
    QString actionParam;
    QString actionParam2;
    QString actionParam3;

    // Math
    QString mathTarget;
    QString mathOperand1;
    QString mathOperator; // "+", "-", "*", "/"
    QString mathOperand2;

    // Create Variable
    QString createVarName;
    VarType createVarType = VarType::INT;

    static QJsonArray serializeVector(const QVector<EventLogicBlock>& blocks);
    static QVector<EventLogicBlock> deserializeArray(const QJsonArray& array);
};

class BlockEditor : public QWidget {
    Q_OBJECT
public:
    explicit BlockEditor(QWidget* parent = nullptr);
    ~BlockEditor() = default;

    void loadEventLogic(const QString& compId, const QString& eventName, 
                        const QStringList& avLeds, const QStringList& avPots, const QStringList& avBuzzers, const QStringList& avMotors,
                        const QStringList& avDhts = QStringList(), const QStringList& avHcsrs = QStringList(),
                        const QStringList& avSliders = QStringList());
    
    void setAvailableHooks(const QStringList& hooks);

    QVector<EventLogicBlock> getActiveBlocks() const { return m_activeBlocks; }
    QMap<QString, QVector<EventLogicBlock>> getEventBlockStorage() {
        if (!m_currentCompId.isEmpty() && !m_currentEventName.isEmpty()) {
            QString key = QString("%1:%2").arg(m_currentCompId).arg(m_currentEventName);
            m_eventBlockStorage[key] = m_activeBlocks;
        }
        return m_eventBlockStorage;
    }

    QVector<EventLogicBlock> getEventBlocks(const QString& compId, const QString& eventName) const;
    void setEventBlocks(const QString& compId, const QString& eventName, const QVector<EventLogicBlock>& blocks);
    void clearAllBlocks();
    void removeSelectedBlock();

signals:
    void blocksChanged();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    void addAssignmentBlock();
    void addConditionBlock();
    void addActionBlock();
    void addMathBlock();
    void addCreateVarBlock();
    void addBlock(const QString& type);
    void updateBlockParams();

private:
    QString m_currentCompId;
    QString m_currentEventName;
    QVector<VariableDef> m_hardwareScopeVariables; // Saved base hardware component variables
    QVector<VariableDef> m_currentScopeVariables; // Includes Globals, Outputs, Locals
    QStringList m_availableHooks;

    QVector<EventLogicBlock> m_activeBlocks;
    QMap<QString, QVector<EventLogicBlock>> m_eventBlockStorage; 

    // UI Elements
    QListWidget* m_blockListWidget; // The Slot Container
    QVBoxLayout* m_paletteLayout;   // Variable palette on the left

    void buildUI();
    void refreshPalette();
    void rebuildScopeVariables();
    void refreshListDisplay();
    QWidget* createBlockWidget(int index, const EventLogicBlock& block, const QColor& customFimColor = QColor());
    void spawnSearchBox(const QPoint& pos, const QString& initialText = QString(), VariableSlotEdit* targetSlotEdit = nullptr);
};

class MathFormulaDialog : public QDialog {
    Q_OBJECT
public:
    MathFormulaDialog(const QString& initialFormula, const QVector<VariableDef>& vars, QWidget* parent = nullptr);
    QString getFormula() const { return m_formula; }

private slots:
    void insertText(const QString& text);
    void updatePreview();
    void onConfirm();

private:
    QString m_formula;
    QVector<VariableDef> m_vars;
    
    QLineEdit* m_formulaEdit;
    QTextBrowser* m_lcdScreen;
    QListWidget* m_varListWidget;
    
    void buildUI();
    QString parseFormulaToHtml(const QString& formula);
};
