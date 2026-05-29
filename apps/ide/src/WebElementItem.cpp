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
    m_textItem->setDefaultTextColor(Qt::white);
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
    
    // Frutiger Aero style
    QLinearGradient grad(0, 0, 0, r.height());
    if (m_type == "Button") {
        grad.setColorAt(0, QColor(90, 180, 255));
        grad.setColorAt(1, QColor(40, 100, 200));
    } else if (m_type == "Text") {
        grad.setColorAt(0, QColor(100, 100, 100, 150));
        grad.setColorAt(1, QColor(50, 50, 50, 150));
    } else if (m_type == "Input") {
        grad.setColorAt(0, QColor(255, 255, 255));
        grad.setColorAt(1, QColor(220, 220, 220));
        m_textItem->setDefaultTextColor(Qt::black);
    } else if (m_type == "Chart") {
        grad.setColorAt(0, QColor(100, 200, 100));
        grad.setColorAt(1, QColor(40, 140, 40));
    }
    
    painter->setBrush(grad);
    QPen pen(Qt::white, 2);
    if (isSelected()) {
        pen.setColor(Qt::yellow);
        pen.setStyle(Qt::DashLine);
    }
    painter->setPen(pen);
    
    painter->drawRoundedRect(r, 10, 10);
    
    // Glossy highlight
    QLinearGradient gloss(0, 0, 0, r.height()/2);
    gloss.setColorAt(0, QColor(255, 255, 255, 100));
    gloss.setColorAt(1, QColor(255, 255, 255, 0));
    painter->setBrush(gloss);
    painter->setPen(Qt::NoPen);
    painter->drawRoundedRect(QRectF(r.x(), r.y(), r.width(), r.height()/2), 10, 10);
}

void WebElementItem::mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event) {
    Q_UNUSED(event);
    bool ok;
    QString newText = QInputDialog::getText(nullptr, "Editar Texto", "Texto:", QLineEdit::Normal, m_text, &ok);
    if (ok) {
        setText(newText);
    }
}
