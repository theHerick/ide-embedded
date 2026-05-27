#include "VariableSystem.h"
#include <QLabel>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QApplication>
#include <QDrag>
#include <QMimeData>
#include <QJsonDocument>

VisualVariableItem::VisualVariableItem(const VariableDef& varDef, QWidget* parent)
    : QWidget(parent), m_def(varDef) {
    
    setAttribute(Qt::WA_StyledBackground, true);
    
    // Style as a draggable premium capsule pill
    QColor typeColor = m_def.getTypeColor();
    QString borderCol = typeColor.name(QColor::HexRgb);
    
    setStyleSheet(QString(
        "VisualVariableItem { "
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 %1, stop:1 %2); "
        "  border: 1.5px solid %3; "
        "  border-radius: 12px; "
        "}"
        "VisualVariableItem:hover { "
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 %3, stop:1 %1); "
        "  border-color: #FFFFFF; "
        "}"
    ).arg(typeColor.darker(120).name(QColor::HexRgb))
     .arg(typeColor.darker(150).name(QColor::HexRgb))
     .arg(borderCol));

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(10, 4, 10, 4);
    layout->setSpacing(6);

    auto* typeLabel = new QLabel(QString("[%1]").arg(VariableDef::typeToString(m_def.type)), this);
    typeLabel->setObjectName("typeLabel");
    typeLabel->setStyleSheet("color: rgba(255,255,255,0.85); font-size: 9px; font-weight: bold; font-family: monospace; background: transparent;");
    
    auto* nameLabel = new QLabel(m_def.name, this);
    nameLabel->setObjectName("nameLabel");
    nameLabel->setStyleSheet("color: white; font-size: 11px; font-weight: bold; font-family: 'Segoe UI', Arial; background: transparent;");

    layout->addWidget(typeLabel);
    layout->addWidget(nameLabel);
    
    setCursor(Qt::OpenHandCursor);
    setToolTip(m_def.description);
}

void VisualVariableItem::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        m_dragStartPos = event->pos();
        setCursor(Qt::ClosedHandCursor);
    }
    QWidget::mousePressEvent(event);
}

void VisualVariableItem::mouseMoveEvent(QMouseEvent* event) {
    if (!(event->buttons() & Qt::LeftButton)) return;
    if ((event->pos() - m_dragStartPos).manhattanLength() < QApplication::startDragDistance()) return;

    QDrag* drag = new QDrag(this);
    QMimeData* mimeData = new QMimeData;
    
    // Pass the variable string format as text
    mimeData->setText(m_def.name);
    
    // Also pass JSON data for advanced dropping
    mimeData->setData("application/x-embedded-variable", m_def.toJson().toVariantMap().empty() ? QByteArray() : QJsonDocument(m_def.toJson()).toJson(QJsonDocument::Compact));

    drag->setMimeData(mimeData);

    // Create drag pixmap
    QPixmap pixmap = grab();
    drag->setPixmap(pixmap);
    drag->setHotSpot(event->pos());

    drag->exec(Qt::CopyAction | Qt::MoveAction);
    setCursor(Qt::OpenHandCursor);
}

VisualOperatorItem::VisualOperatorItem(const QString& opName, const QString& opSymbol, QWidget* parent)
    : QWidget(parent), m_name(opName), m_symbol(opSymbol) {
    
    setAttribute(Qt::WA_StyledBackground, true);
    
    QColor typeColor("#EC4899"); // Beautiful Pink/Magenta
    QString borderCol = typeColor.name(QColor::HexRgb);
    
    setStyleSheet(QString(
        "VisualOperatorItem { "
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 %1, stop:1 %2); "
        "  border: 1.5px solid %3; "
        "  border-radius: 12px; "
        "}"
        "VisualOperatorItem:hover { "
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 %3, stop:1 %1); "
        "  border-color: #FFFFFF; "
        "}"
    ).arg(typeColor.darker(120).name(QColor::HexRgb))
     .arg(typeColor.darker(150).name(QColor::HexRgb))
     .arg(borderCol));

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(10, 4, 10, 4);
    layout->setSpacing(6);

    auto* symbolLabel = new QLabel(QString("[%1]").arg(m_symbol), this);
    symbolLabel->setStyleSheet("color: rgba(255,255,255,0.95); font-size: 10px; font-weight: bold; font-family: monospace; background: transparent;");
    
    auto* nameLabel = new QLabel(m_name, this);
    nameLabel->setStyleSheet("color: white; font-size: 11px; font-weight: bold; font-family: 'Segoe UI', Arial; background: transparent;");

    layout->addWidget(symbolLabel);
    layout->addWidget(nameLabel);
    
    setCursor(Qt::OpenHandCursor);
    setToolTip(QString("Arrastar operador '%1'").arg(m_symbol));
}

void VisualOperatorItem::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        m_dragStartPos = event->pos();
        setCursor(Qt::ClosedHandCursor);
    }
    QWidget::mousePressEvent(event);
}

void VisualOperatorItem::mouseMoveEvent(QMouseEvent* event) {
    if (!(event->buttons() & Qt::LeftButton)) return;
    if ((event->pos() - m_dragStartPos).manhattanLength() < QApplication::startDragDistance()) return;

    QDrag* drag = new QDrag(this);
    QMimeData* mimeData = new QMimeData;
    
    mimeData->setText(m_symbol);
    drag->setMimeData(mimeData);

    QPixmap pixmap = grab();
    drag->setPixmap(pixmap);
    drag->setHotSpot(event->pos());

    drag->exec(Qt::CopyAction | Qt::MoveAction);
    setCursor(Qt::OpenHandCursor);
}

