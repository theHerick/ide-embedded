#pragma once
#include <QGraphicsObject>
#include <QPen>
#include <QBrush>
#include <QString>
#include <QVector>
#include <QPointF>
#include <QColor>
#include <QJsonObject>

struct Pin {
    QString name;
    QPointF localPos;
    bool isOutput;
    QString connectedToComponent;
    QString connectedToPin;
    QColor color;
    bool generateCode = true;
};

class ComponentItem : public QGraphicsObject {
    Q_OBJECT
public:
    ComponentItem(const QString& id, const QString& name, const QString& type, QGraphicsItem* parent = nullptr);
    virtual ~ComponentItem() = default;

    QString id() const { return m_id; }
    QString name() const { return m_name; }
    QString componentType() const { return m_type; }
    virtual void updateLayoutForSMD(const QString& smdSize) {}
    QVector<Pin>& pins() { return m_pins; }
    void setPins(QVector<Pin> pins);

    virtual QRectF boundingRect() const override = 0;
    virtual void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override = 0;

    Pin* findPinAt(const QPointF& scenePos, double threshold = 15.0);
    QPointF getPinScenePos(const Pin& pin) const;
    Pin* getPinByName(const QString& name);

    // Opens a modal dialog allowing full-featured editing of pins
    void openPinEditor();

    // Applies microcontroller configuration (dimensions, pins, and pitch)
    virtual void applyMicrocontrollerConfig(const QJsonObject& cfg);

    bool isSimulating() const;

signals:
    void rightClicked(const QPointF& globalPos);
    void doubleClicked(const QPointF& globalPos);
    void componentMoved();

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) override;
    void contextMenuEvent(QGraphicsSceneContextMenuEvent* event) override;

    QString m_id;
    QString m_name;
    QString m_type;
    QVector<Pin> m_pins;
    bool m_dragging = false;
};

// ESP32 Item
class ESP32Item : public ComponentItem {
    Q_OBJECT
public:
    ESP32Item(const QString& id, const QString& name, QGraphicsItem* parent = nullptr);
    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;
    void applyMicrocontrollerConfig(const QJsonObject& cfg) override;

signals:
    void resetTriggered();
    void bootPressed(bool pressed);

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;

private:
    QRectF m_btnResetRect;
    QRectF m_btnBootRect;
    bool m_resetPressed = false;
    bool m_bootPressed = false;
};

// LED Item
class LEDItem : public ComponentItem {
    Q_OBJECT
public:
    void updateLayoutForSMD(const QString& smdSize) override;
    LEDItem(const QString& id, const QString& name, QGraphicsItem* parent = nullptr);
    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;
    
    bool isOn() const { return m_isOn; }
    void setOn(bool on) { m_isOn = on; update(); }

private:
    bool m_isOn = false;
};

// Button Item
class ButtonItem : public ComponentItem {
    Q_OBJECT
public:
    ButtonItem(const QString& id, const QString& name, QGraphicsItem* parent = nullptr);
    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

    bool isPressed() const { return m_isPressed; }

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;

signals:
    void stateChanged(bool pressed);

private:
    bool m_isPressed = false;
};

// Resistor Item
class ResistorItem : public ComponentItem {
    Q_OBJECT
public:
    void updateLayoutForSMD(const QString& smdSize) override;
    ResistorItem(const QString& id, const QString& name, QGraphicsItem* parent = nullptr);
    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

    double resistance() const { return m_resistance; }
    void setResistance(double value);
    void setCustomLabels(const QString& left, const QString& right);

private:
    double m_resistance;
    QString m_customTextL;
    QString m_customTextR;
};

// Capacitor Item
class CapacitorItem : public ComponentItem {
    Q_OBJECT
public:
    void updateLayoutForSMD(const QString& smdSize) override;
    CapacitorItem(const QString& id, const QString& name, QGraphicsItem* parent = nullptr);
    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;
};

// Potentiometer Item
class PotentiometerItem : public ComponentItem {
    Q_OBJECT
public:
    PotentiometerItem(const QString& id, const QString& name, QGraphicsItem* parent = nullptr);
    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

    double value() const { return m_value; }
    void setValue(double val);

signals:
    void valueChanged(double newValue);

protected:
    void wheelEvent(QGraphicsSceneWheelEvent* event) override;

private:
    double m_value;
};

// Buzzer Item
class BuzzerItem : public ComponentItem {
    Q_OBJECT
public:
    BuzzerItem(const QString& id, const QString& name, QGraphicsItem* parent = nullptr);
    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

    bool isActive() const { return m_isActive; }
    void setActive(bool active) { m_isActive = active; update(); }

private:
    bool m_isActive = false;
};

// Motor Item
class MotorItem : public ComponentItem {
    Q_OBJECT
    Q_PROPERTY(QString motorType READ motorType WRITE setMotorType)
public:
    MotorItem(const QString& id, const QString& name, QGraphicsItem* parent = nullptr);
    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

    QString motorType() const { return m_motorType; }
    void setMotorType(const QString& type);

    double currentAngle() const { return m_currentAngle; }
    void setCurrentAngle(double angle);

private:
    QString m_motorType; // e.g. "servo90", "servo180", "servo360", "dc", "stepper"
    double m_currentAngle;
};

// BESS (Battery Energy Storage System) Item
class BessItem : public ComponentItem {
    Q_OBJECT
    Q_PROPERTY(double chargeLevel READ chargeLevel WRITE setChargeLevel)
public:
    BessItem(const QString& id, const QString& name, QGraphicsItem* parent = nullptr);
    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;
    
    double chargeLevel() const { return m_chargeLevel; }
    void setChargeLevel(double level);
    
private:
    double m_chargeLevel; // 0.0 to 100.0
};

// BESS Charger Item (TP4056 style)
class BessChargerItem : public ComponentItem {
    Q_OBJECT
    Q_PROPERTY(bool isPluggedIn READ isPluggedIn WRITE setPluggedIn)
public:
    BessChargerItem(const QString& id, const QString& name, QGraphicsItem* parent = nullptr);
    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;
    
    bool isPluggedIn() const { return m_isPluggedIn; }
    void setPluggedIn(bool plugged);
    
protected:
    void contextMenuEvent(QGraphicsSceneContextMenuEvent* event) override;
    
private:
    bool m_isPluggedIn;
};

// DHT22 (Temperature & Humidity Sensor) Item
class DHT22Item : public ComponentItem {
    Q_OBJECT
public:
    DHT22Item(const QString& id, const QString& name, QGraphicsItem* parent = nullptr);
    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

    double humidity() const { return m_humidity; }
    double temperature() const { return m_temperature; }
    void setHumidity(double h);
    void setTemperature(double t);

signals:
    void valueChanged();

protected:
    void wheelEvent(QGraphicsSceneWheelEvent* event) override;

private:
    double m_humidity;
    double m_temperature;
};

// HC-SR04 (Ultrasonic Distance Sensor) Item
class HCSR04Item : public ComponentItem {
    Q_OBJECT
public:
    HCSR04Item(const QString& id, const QString& name, QGraphicsItem* parent = nullptr);
    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

    double distance() const { return m_distance; }
    void setDistance(double d);

signals:
    void valueChanged();

protected:
    void wheelEvent(QGraphicsSceneWheelEvent* event) override;

private:
    double m_distance;
};

// Ground (Terra) Item
class GndItem : public ComponentItem {
    Q_OBJECT
public:
    GndItem(const QString& id, const QString& name, QGraphicsItem* parent = nullptr);
    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;
};



