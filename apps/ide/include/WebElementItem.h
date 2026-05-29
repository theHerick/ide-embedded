#pragma once

#include <QGraphicsRectItem>
#include <QJsonObject>
#include <QObject>
#include <QGraphicsTextItem>

class WebElementItem : public QObject, public QGraphicsRectItem {
    Q_OBJECT
public:
    explicit WebElementItem(const QJsonObject& data, QGraphicsItem* parent = nullptr);
    QJsonObject toJson() const;

    QString elementType() const { return m_type; }
    QString id() const { return m_id; }
    QString boundVar() const { return m_boundVar; }
    void setBoundVar(const QString& var);

    void setType(const QString& type) { m_type = type; }
    void setText(const QString& text);
    
    int formatSize() const { return m_formatSize; }
    QString formatColor() const { return m_formatColor; }
    bool formatBold() const { return m_formatBold; }
    
    void setFormatSize(int s) { m_formatSize = s; update(); }
    void setFormatColor(const QString& c) { m_formatColor = c; update(); }
    void setFormatBold(bool b) { m_formatBold = b; update(); }

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event) override;

private:
    QString m_id;
    QString m_type;
    QString m_boundVar;
    QString m_text;
    
    int m_formatSize = 16;
    QString m_formatColor = "#0284c7"; // Default color
    bool m_formatBold = true;

    QGraphicsTextItem* m_textItem;
    QSizeF m_size;
};
