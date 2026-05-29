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

    QString text() const { return m_text; }
    void setText(const QString& text);

protected:
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event) override;

private:
    QString m_id;
    QString m_type;
    QString m_boundVar;
    QString m_text;
    QGraphicsTextItem* m_textItem;
};
