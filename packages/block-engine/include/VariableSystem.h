#pragma once
#include <QString>
#include <QColor>
#include <QJsonObject>
#include <QDrag>
#include <QMimeData>
#include <QWidget>

enum class VarType {
    INT,
    FLOAT,
    BOOL,
    STRING,
    PIN,
    UNKNOWN
};

enum class VarScope {
    LOCAL_EVENT,    // Created by user inside the event slot
    COMP_GLOBAL,    // Belonging to the component (persists state)
    RUNTIME_OUTPUT, // Read-only data coming from physical pins/sensors
    DERIVED         // Automatically derived by IDE logic
};

struct VariableDef {
    QString name;
    VarType type;
    VarScope scope;
    QString initialValue; // Used primarily for LOCAL_EVENT
    QString description;

    static QString typeToString(VarType t) {
        switch (t) {
            case VarType::INT: return "INT";
            case VarType::FLOAT: return "FLOAT";
            case VarType::BOOL: return "BOOL";
            case VarType::STRING: return "STRING";
            case VarType::PIN: return "PIN";
            default: return "ANY";
        }
    }

    static VarType stringToType(const QString& str) {
        QString s = str.toUpper().trimmed();
        if (s == "INT" || s == "INTEGER") return VarType::INT;
        if (s == "FLOAT" || s == "DOUBLE") return VarType::FLOAT;
        if (s == "BOOL" || s == "BOOLEAN") return VarType::BOOL;
        if (s == "STRING") return VarType::STRING;
        if (s == "PIN") return VarType::PIN;
        return VarType::UNKNOWN;
    }

    QColor getTypeColor() const {
        switch (type) {
            case VarType::INT: return QColor("#10B981");    // Green
            case VarType::FLOAT: return QColor("#3B82F6");  // Blue
            case VarType::BOOL: return QColor("#8B5CF6");   // Purple
            case VarType::STRING: return QColor("#F59E0B"); // Orange
            case VarType::PIN: return QColor("#EC4899");    // Pink
            default: return QColor("#64748B");              // Gray
        }
    }

    QJsonObject toJson() const {
        QJsonObject obj;
        obj["name"] = name;
        obj["type"] = typeToString(type);
        obj["scope"] = static_cast<int>(scope);
        obj["initialValue"] = initialValue;
        return obj;
    }

    static VariableDef fromJson(const QJsonObject& obj) {
        VariableDef def;
        def.name = obj["name"].toString();
        def.type = stringToType(obj["type"].toString());
        def.scope = static_cast<VarScope>(obj["scope"].toInt());
        def.initialValue = obj["initialValue"].toString();
        return def;
    }
};

// Custom visual widget that can be dragged
class VisualVariableItem : public QWidget {
    Q_OBJECT
public:
    VisualVariableItem(const VariableDef& varDef, QWidget* parent = nullptr);
    VariableDef getDef() const { return m_def; }

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;

private:
    VariableDef m_def;
    QPoint m_dragStartPos;
};

// Custom visual widget for draggable mathematical/comparison operators
class VisualOperatorItem : public QWidget {
    Q_OBJECT
public:
    VisualOperatorItem(const QString& opName, const QString& opSymbol, QWidget* parent = nullptr);
    QString getSymbol() const { return m_symbol; }

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;

private:
    QString m_name;
    QString m_symbol;
    QPoint m_dragStartPos;
};

