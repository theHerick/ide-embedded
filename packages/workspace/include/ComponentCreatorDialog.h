#pragma once

#include <QDialog>
#include <QVector>
#include <QMap>
#include "CustomComponent.h"
#include "BlockEditor.h"

class QLineEdit;
class QSpinBox;
class QTableWidget;

class ComponentCreatorDialog : public QDialog {
    Q_OBJECT
public:
    explicit ComponentCreatorDialog(QWidget* parent = nullptr);
    ~ComponentCreatorDialog() override = default;

    CustomComponentDef getCreatedComponent() const { return m_createdDef; }

private slots:
    void updatePinTableRows(int count);
    void validateAndSave();

private:
    QLineEdit* m_nameEdit = nullptr;
    QSpinBox* m_pinsCountSpin = nullptr;
    QTableWidget* m_pinsTable = nullptr;
    BlockEditor* m_blockEditor = nullptr;

    CustomComponentDef m_createdDef;

    void setupUI();
};
