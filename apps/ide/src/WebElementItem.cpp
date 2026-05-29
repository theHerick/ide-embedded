#include "WebElementItem.h"
#include <QPainter>
#include <QGraphicsSceneMouseEvent>
#include <QInputDialog>
#include <QMenu>
#include <QUuid>

WebElementItem::WebElementItem(const QJsonObject& data, QGraphicsItem* parent)
    : QObject(), QGraphicsRectItem(parent)
{
    setFlags(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsMovable | QGraphicsItem::ItemSendsGeometryChanges);
    
    m_id = data.contains("id") ? data["id"].toString() : QUuid::createUuid().toString();
    m_type = data.contains("type") ? data["type"].toString() : "Button";
    m_boundVar = data.contains("bound_var") ? data["bound_var"].toString() : "";
    m_text = data.contains("text") ? data["text"].toString() : m_type;
    
    double x = data.contains("x") ? data["x"].toDouble() : 0;
    double y = data.contains("y") ? data["y"].toDouble() : 0;
    double w = data.contains("width") ? data["width"].toDouble() : 120;
    double h = data.contains("height") ? data["height"].toDouble() : 40;
    
    setRect(0, 0, w, h);
    setPos(x, y);

    m_textItem = new QGraphicsTextItem(this);
    
    QFont f = m_textItem->font();
    if (m_type == "Text") {
        f.setPixelSize(16);
        f.setBold(true);
    } else {
        f.setBold(true);
    }
    m_textItem->setFont(f);
    
    if (m_type == "Button") {
        m_textItem->setDefaultTextColor(Qt::white);
    } else if (m_type == "Text") {
        m_textItem->setDefaultTextColor(QColor("#01579b"));
    } else if (m_type == "Chart") {
        m_textItem->setDefaultTextColor(QColor("#0288d1"));
    }
    
    setText(m_text);
}

void WebElementItem::setText(const QString& text) {
    m_text = text;
    QString display = text;
    if (!m_boundVar.isEmpty()) {
        display += "\n[" + m_boundVar + "]";
    }
    m_textItem->setPlainText(display);
    
    // Center text
    QRectF r = rect();
    QRectF tr = m_textItem->boundingRect();
    m_textItem->setPos(r.width()/2 - tr.width()/2, r.height()/2 - tr.height()/2);
}

void WebElementItem::setBoundVar(const QString& var) {
    m_boundVar = var;
    setText(m_text);
}

QJsonObject WebElementItem::toJson() const {
    QJsonObject obj;
    obj["id"] = m_id;
    obj["type"] = m_type;
    obj["bound_var"] = m_boundVar;
    obj["text"] = m_text;
    obj["x"] = pos().x();
    obj["y"] = pos().y();
    obj["width"] = rect().width();
    obj["height"] = rect().height();
    return obj;
}

void WebElementItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) {
    Q_UNUSED(option);
    Q_UNUSED(widget);
    
    painter->setRenderHint(QPainter::Antialiasing);
    QRectF r = rect();
    
    QPen pen(Qt::NoPen);
    if (isSelected()) {
        pen = QPen(Qt::yellow, 2, Qt::DashLine);
    }
    
    if (m_type == "Button") {
        QLinearGradient grad(0, 0, 0, r.height());
        grad.setColorAt(0, QColor("#4fc3f7"));
        grad.setColorAt(1, QColor("#0288d1"));
        painter->setBrush(grad);
        if (!isSelected()) painter->setPen(Qt::NoPen);
        else painter->setPen(pen);
        painter->drawRoundedRect(r, r.height()/2, r.height()/2);
        
        // Glossy highlight
        QLinearGradient gloss(0, 0, 0, r.height()/2);
        gloss.setColorAt(0, QColor(255, 255, 255, 100));
        gloss.setColorAt(1, QColor(255, 255, 255, 0));
        painter->setBrush(gloss);
        painter->setPen(Qt::NoPen);
        painter->drawRoundedRect(QRectF(r.x(), r.y(), r.width(), r.height()/2), r.height()/2, r.height()/2);
        
    } else if (m_type == "Text") {
        painter->setBrush(Qt::NoBrush);
        if (isSelected()) painter->setPen(pen);
        else painter->setPen(Qt::NoPen);
        painter->drawRect(r); // Just for selection bounding box
        
    } else if (m_type == "Chart") {
        painter->setBrush(QColor(255, 255, 255, 204));
        if (!isSelected()) painter->setPen(QPen(QColor("#4fc3f7"), 1));
        else painter->setPen(pen);
        painter->drawRoundedRect(r, 10, 10);
    }
}

void WebElementItem::mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event) {
    Q_UNUSED(event);
    bool ok;
    QString newText = QInputDialog::getText(nullptr, "Editar Texto", "Texto:", QLineEdit::Normal, m_text, &ok);
    if (ok) {
        setText(newText);
    }
}
