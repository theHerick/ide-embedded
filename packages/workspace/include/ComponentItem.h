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
    void setName(const QString& name) { m_name = name; update(); }
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
    void hoverMoveEvent(QGraphicsSceneHoverEvent* event) override;
    void hoverLeaveEvent(QGraphicsSceneHoverEvent* event) override;

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

// RGB LED Item
class RGBLEDItem : public ComponentItem {
    Q_OBJECT
public:
    void updateLayoutForSMD(const QString& smdSize) override;
    RGBLEDItem(const QString& id, const QString& name, QGraphicsItem* parent = nullptr);
    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;
    
    QColor color() const { return m_color; }
    void setColor(const QColor& c) { m_color = c; update(); }

private:
    QColor m_color = Qt::black;
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
    double m_resistance = 1000.0;
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

    double capacitance() const { return m_capacitance; }
    void setCapacitance(double value);

    bool isSMD() const { return m_isSMD; }
    void setSMD(bool enabled);

    QString smdSize() const { return m_smdSize; }
    void setSmdSize(const QString& size);

private:
    double m_capacitance = 0.000001; // 1uF default
    bool m_isSMD = false;
    QString m_smdSize = "1206";
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
    double m_value = 0.0;
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

    bool isPassive() const { return m_isPassive; }
    void setPassive(bool passive) { m_isPassive = passive; update(); }

    int frequency() const { return m_frequency; }
    void setFrequency(int freq) { m_frequency = freq; update(); }

protected:
    void contextMenuEvent(QGraphicsSceneContextMenuEvent* event) override;

private:
    bool m_isActive = false;
    bool m_isPassive = false;
    int m_frequency = 1000;
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
    QString m_motorType = "dc"; // e.g. "servo90", "servo180", "servo360", "dc", "stepper"
    double m_currentAngle = 0.0;
};

// Relay Module Item
class RelayItem : public ComponentItem {
    Q_OBJECT
    Q_PROPERTY(bool isOn READ isOn WRITE setOn)
public:
    RelayItem(const QString& id, const QString& name, QGraphicsItem* parent = nullptr);
    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;
    
    bool isOn() const { return m_isOn; }
    void setOn(bool on) { m_isOn = on; update(); }
    
private:
    bool m_isOn = false;
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
    double m_humidity = 50.0;
    double m_temperature = 25.0;
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
    double m_distance = 100.0;
};

// Ground (Terra) Item
class GndItem : public ComponentItem {
    Q_OBJECT
public:
    GndItem(const QString& id, const QString& name, QGraphicsItem* parent = nullptr);
    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;
};



