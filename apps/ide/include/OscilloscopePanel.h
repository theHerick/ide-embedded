#pragma once
#include <QWidget>
#include <QVector>
#include <QColor>
#include <QString>
#include <QTimer>
#include <QElapsedTimer>
#include <QPushButton>
#include <QSlider>
#include <QLabel>

// ─────────────────────────────────────────────────────────────────────────────
// Internal data structures
// ─────────────────────────────────────────────────────────────────────────────
struct OscSample {
    qint64 timestampMs; // ms since simulation started
    bool   isHigh;
};

struct OscChannel {
    QString        id;       // "compId::pinName" unique key
    QString        label;    // human-readable e.g. "LED_1 Anode"
    QColor         color;
    QVector<OscSample> samples;
};

// ─────────────────────────────────────────────────────────────────────────────
// OscilloscopeView — raw waveform renderer using QPainter
// ─────────────────────────────────────────────────────────────────────────────
class OscilloscopeView : public QWidget {
    Q_OBJECT
public:
    explicit OscilloscopeView(QWidget* parent = nullptr);

    void setChannels(const QVector<OscChannel>* channels);
    void setWindowMs(qint64 windowMs);
    void setCurrentTimeMs(qint64 t);
    void setRunning(bool running);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    const QVector<OscChannel>* m_channels = nullptr;
    qint64 m_windowMs    = 5000;
    qint64 m_currentTime = 0;
    bool   m_running     = false;

    static constexpr int k_labelWidth  = 90;
    static constexpr int k_timeAxisH   = 22;
    static constexpr int k_channelMinH = 52;

    void drawTimeAxis(QPainter& p, const QRectF& plotArea);
    void drawChannel(QPainter& p, const QRectF& lane, const OscChannel& ch);
    void drawGrid(QPainter& p, const QRectF& plotArea);
};

// ─────────────────────────────────────────────────────────────────────────────
// OscilloscopePanel — full panel: controls + view
// ─────────────────────────────────────────────────────────────────────────────
class OscilloscopePanel : public QWidget {
    Q_OBJECT
public:
    explicit OscilloscopePanel(QWidget* parent = nullptr);

public slots:
    void onPinStateChanged(const QString& compId, const QString& pinName, bool isHigh);
    void onSimulationStarted();
    void onSimulationStopped();

private slots:
    void clearChannels();
    void onZoomChanged(int value);
    void repaintView();

private:
    QVector<OscChannel>  m_channels;
    QElapsedTimer        m_elapsed;
    qint64               m_windowMs   = 5000;
    bool                 m_running    = false;

    OscilloscopeView*    m_view       = nullptr;
    QPushButton*         m_clearBtn   = nullptr;
    QSlider*             m_zoomSlider = nullptr;
    QLabel*              m_zoomLabel  = nullptr;
    QLabel*              m_statusLabel = nullptr;
    QTimer*              m_repaintTimer = nullptr;

    OscChannel* findOrCreateChannel(const QString& compId, const QString& pinName);
    QColor colorForComponent(const QString& compId, const QString& pinName);
    QString labelForComponent(const QString& compId, const QString& pinName);
};
