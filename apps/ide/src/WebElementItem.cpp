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
    
    m_formatSize = data.contains("formatSize") ? data["formatSize"].toInt() : 16;
    m_formatColor = data.contains("formatColor") ? data["formatColor"].toString() : "#0284c7";
    m_formatBold = data.contains("formatBold") ? data["formatBold"].toBool() : true;

    double x = data.contains("x") ? data["x"].toDouble() : 0;
    double y = data.contains("y") ? data["y"].toDouble() : 0;
    double w = data.contains("width") ? data["width"].toDouble() : 120;
    double h = data.contains("height") ? data["height"].toDouble() : 40;
    
    setRect(0, 0, w, h);
    setPos(x, y);

    m_textItem = new QGraphicsTextItem(this);
    
    QFont f = m_textItem->font();
    f.setPixelSize(m_formatSize);
    f.setBold(m_formatBold);
    m_textItem->setFont(f);
    
    if (m_type == "Button") {
        m_textItem->setDefaultTextColor(Qt::white);
    } else {
        m_textItem->setDefaultTextColor(QColor(m_formatColor));
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
    
    obj["formatSize"] = m_formatSize;
    obj["formatColor"] = m_formatColor;
    obj["formatBold"] = m_formatBold;
    
    return obj;
}

QVariant WebElementItem::itemChange(GraphicsItemChange change, const QVariant &value) {
    if (change == ItemPositionChange && scene()) {
        QPointF newPos = value.toPointF();
        int gridSize = 20;
        qreal x = qRound(newPos.x() / gridSize) * gridSize;
        qreal y = qRound(newPos.y() / gridSize) * gridSize;
        return QPointF(x, y);
    }
    return QGraphicsRectItem::itemChange(change, value);
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
    } else if (m_type == "Slider") {
        painter->setBrush(Qt::NoBrush);
        if (isSelected()) painter->setPen(pen);
        else painter->setPen(Qt::NoPen);
        painter->drawRect(r);
        
        // Draw track
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(200, 200, 200));
        painter->drawRoundedRect(QRectF(r.x(), r.y() + r.height()/2 - 4, r.width(), 8), 4, 4);
        
        // Draw knob
        painter->setBrush(QColor("#0288d1"));
        painter->drawEllipse(QPointF(r.x() + r.width()/2, r.y() + r.height()/2), 10, 10);
    } else if (m_type == "LED") {
        painter->setBrush(Qt::NoBrush);
        if (isSelected()) painter->setPen(pen);
        else painter->setPen(Qt::NoPen);
        painter->drawRect(r);
        
        // Draw LED circle
        painter->setBrush(QColor("#ef4444")); // Red by default
        painter->setPen(QPen(QColor("#b91c1c"), 2));
        qreal radius = qMin(r.width(), r.height()) / 2 - 2;
        painter->drawEllipse(r.center(), radius, radius);
        
        // Glossy
        painter->setBrush(QColor(255, 255, 255, 100));
        painter->setPen(Qt::NoPen);
        painter->drawEllipse(QPointF(r.center().x() - radius/3, r.center().y() - radius/3), radius/2, radius/2);
    } else if (m_type == "Input") {
        painter->setBrush(QColor(255, 255, 255, 230));
        if (!isSelected()) painter->setPen(QPen(QColor("#81d4fa"), 2));
        else painter->setPen(pen);
        painter->drawRoundedRect(r, 6, 6);
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
