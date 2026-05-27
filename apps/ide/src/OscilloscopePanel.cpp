#include "OscilloscopePanel.h"
#include <QPainter>
#include <QPainterPath>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDateTime>
#include <QScrollArea>
#include <cmath>

// ═════════════════════════════════════════════════════════════════════════════
// OscilloscopeView
// ═════════════════════════════════════════════════════════════════════════════

OscilloscopeView::OscilloscopeView(QWidget* parent) : QWidget(parent) {
    setMinimumHeight(80);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void OscilloscopeView::setChannels(const QVector<OscChannel>* channels) {
    m_channels = channels;
}

void OscilloscopeView::setWindowMs(qint64 windowMs) {
    m_windowMs = windowMs;
}

void OscilloscopeView::setCurrentTimeMs(qint64 t) {
    m_currentTime = t;
}

void OscilloscopeView::setRunning(bool running) {
    m_running = running;
}

void OscilloscopeView::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false); // crisp digital lines

    const int W = width();
    const int H = height();

    // Background — light IDE theme
    p.fillRect(rect(), QColor(250, 252, 255));

    if (!m_channels || m_channels->isEmpty()) {
        p.setPen(QColor(148, 163, 184));
        p.setFont(QFont("Segoe UI", 10));
        p.drawText(rect(), Qt::AlignCenter,
                   "Nenhum sinal capturado ainda.\nInicie a simulacao e acione componentes para ver as ondas.");
        return;
    }

    const int nCh = m_channels->size();
    const int plotW = W - k_labelWidth;
    const int plotH = H - k_timeAxisH;

    // Height per channel
    int chH = std::max(k_channelMinH, plotH / nCh);

    QRectF plotArea(k_labelWidth, 0, plotW, (double)chH * nCh);

    drawGrid(p, plotArea);

    // Draw each channel
    for (int i = 0; i < nCh; ++i) {
        QRectF lane(k_labelWidth, i * chH, plotW, chH);
        drawChannel(p, lane, (*m_channels)[i]);

        // Divider line between channels
        if (i > 0) {
            p.setPen(QPen(QColor(226, 232, 240), 1));
            p.drawLine(0, i * chH, W, i * chH);
        }

        // Label area background
        QRectF labelRect(0, i * chH, k_labelWidth - 4, chH);
        p.fillRect(labelRect, QColor(241, 245, 249));

        // Channel label
        QColor labelColor = (*m_channels)[i].color.darker(130);
        p.setPen(labelColor);
        QFont f("Segoe UI", 7);
        f.setBold(true);
        p.setFont(f);
        // Component name (bold, first line)
        QString label = (*m_channels)[i].label;
        QStringList parts = label.split('\n');
        if (parts.size() >= 2) {
            p.drawText(QRectF(4, i * chH + 6, k_labelWidth - 8, chH / 2 - 4),
                       Qt::AlignLeft | Qt::AlignVCenter, parts[0]);
            f.setBold(false);
            f.setPointSize(6);
            p.setFont(f);
            p.setPen(QColor(100, 116, 139));
            p.drawText(QRectF(4, i * chH + chH / 2 + 2, k_labelWidth - 8, chH / 2 - 4),
                       Qt::AlignLeft | Qt::AlignVCenter, parts[1]);
        } else {
            p.drawText(QRectF(4, i * chH, k_labelWidth - 8, chH),
                       Qt::AlignLeft | Qt::AlignVCenter, label);
        }

        // Current state dot
        bool curHigh = !(*m_channels)[i].samples.isEmpty() &&
                       (*m_channels)[i].samples.last().isHigh;
        p.setBrush(curHigh ? (*m_channels)[i].color : QColor(226, 232, 240));
        p.setPen(QPen((*m_channels)[i].color.darker(130), 1));
        p.drawEllipse(QPointF(k_labelWidth - 10, i * chH + chH / 2.0), 5, 5);

        // HIGH / LOW text
        f.setPointSize(5);
        p.setFont(f);
        p.setPen(curHigh ? (*m_channels)[i].color.darker(130) : QColor(148, 163, 184));
        p.drawText(QRectF(4, i * chH + chH - 14, k_labelWidth - 8, 12),
                   Qt::AlignLeft, curHigh ? "HIGH" : "LOW");
    }

    // Vertical separator label/plot
    p.setPen(QPen(QColor(203, 213, 225), 1));
    p.drawLine(k_labelWidth - 1, 0, k_labelWidth - 1, H - k_timeAxisH);

    // Time axis
    QRectF timeAxisRect(k_labelWidth, H - k_timeAxisH, plotW, k_timeAxisH);
    drawTimeAxis(p, timeAxisRect);

    // Live cursor (thin vertical line at right edge while running)
    if (m_running) {
        p.setPen(QPen(QColor(99, 102, 241, 140), 1, Qt::DashLine));
        p.drawLine(W - 1, 0, W - 1, H - k_timeAxisH);
    }
}

void OscilloscopeView::drawGrid(QPainter& p, const QRectF& plotArea) {
    // Vertical time grid lines every 500ms — subtle on light background
    double msPerPx = (double)m_windowMs / plotArea.width();
    qint64 startMs = std::max(0LL, m_currentTime - m_windowMs);
    qint64 gridIntervalMs = 500;

    p.setPen(QPen(QColor(203, 213, 225, 180), 1, Qt::DotLine));

    qint64 firstMark = (startMs / gridIntervalMs) * gridIntervalMs;
    for (qint64 t = firstMark; t <= m_currentTime; t += gridIntervalMs) {
        double x = plotArea.left() + (double)(t - startMs) / msPerPx;
        if (x >= plotArea.left() && x <= plotArea.right()) {
            p.drawLine(QPointF(x, plotArea.top()), QPointF(x, plotArea.bottom()));
        }
    }
}

void OscilloscopeView::drawTimeAxis(QPainter& p, const QRectF& area) {
    p.fillRect(area, QColor(241, 245, 249));
    p.setPen(QPen(QColor(203, 213, 225), 1));
    p.drawLine(area.topLeft(), area.topRight());

    QFont f("Segoe UI", 6);
    p.setFont(f);

    double msPerPx = (double)m_windowMs / area.width();
    qint64 startMs = std::max(0LL, m_currentTime - m_windowMs);
    qint64 gridIntervalMs = 500;

    qint64 firstMark = (startMs / gridIntervalMs) * gridIntervalMs;
    for (qint64 t = firstMark; t <= m_currentTime; t += gridIntervalMs) {
        double x = area.left() + (double)(t - startMs) / msPerPx;
        if (x < area.left() || x > area.right()) continue;

        p.setPen(QPen(QColor(148, 163, 184), 1));
        p.drawLine(QPointF(x, area.top()), QPointF(x, area.top() + 4));

        // Only label every 1000ms to avoid crowding
        if (t % 1000 == 0) {
            p.setPen(QColor(100, 116, 139));
            double sec = t / 1000.0;
            QString label = QString::number(sec, 'f', 1) + "s";
            p.drawText(QRectF(x - 18, area.top() + 5, 36, area.height() - 5),
                       Qt::AlignCenter, label);
        }
    }
}

void OscilloscopeView::drawChannel(QPainter& p, const QRectF& lane, const OscChannel& ch) {
    if (ch.samples.isEmpty()) return;

    // The waveform occupies the middle 60% of the lane
    const double margin = lane.height() * 0.18;
    const double highY  = lane.top()    + margin;
    const double lowY   = lane.bottom() - margin;
    const double midY   = (highY + lowY) * 0.5;

    qint64 startMs = std::max(0LL, m_currentTime - m_windowMs);
    double msPerPx = (double)m_windowMs / lane.width();

    auto timeToX = [&](qint64 t) -> double {
        return lane.left() + (double)(t - startMs) / msPerPx;
    };

    // Build the waveform path
    QPainterPath path;
    bool pathStarted = false;

    double prevX = lane.left();
    double prevY = midY;

    // Find the sample that was active at startMs (so we know what value to begin with)
    bool initialHigh = false;
    for (int i = ch.samples.size() - 1; i >= 0; --i) {
        if (ch.samples[i].timestampMs <= startMs) {
            initialHigh = ch.samples[i].isHigh;
            break;
        }
    }

    double curY = initialHigh ? highY : lowY;
    path.moveTo(lane.left(), curY);
    pathStarted = true;
    prevX = lane.left();
    prevY = curY;

    for (const auto& s : ch.samples) {
        if (s.timestampMs < startMs) continue;

        double x = timeToX(s.timestampMs);
        if (x > lane.right()) {
            x = lane.right();
        }

        double nextY = s.isHigh ? highY : lowY;

        // Horizontal segment at prevY up to transition
        path.lineTo(x, prevY);
        // Vertical transition
        path.lineTo(x, nextY);

        prevX = x;
        prevY = nextY;

        if (x >= lane.right()) break;
    }

    // Extend to right edge
    if (pathStarted) {
        path.lineTo(lane.right(), prevY);
    }

    // Draw glow (wider, more transparent)
    QPen glowPen(ch.color, 4, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin);
    glowPen.setColor(QColor(ch.color.red(), ch.color.green(), ch.color.blue(), 50));
    p.setPen(glowPen);
    p.drawPath(path);

    // Draw main waveform line
    QPen linePen(ch.color, 1.5, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin);
    p.setPen(linePen);
    p.drawPath(path);

    // Fill under waveform (subtle area fill)
    QPainterPath fillPath = path;
    fillPath.lineTo(lane.right(), lowY);
    fillPath.lineTo(lane.left(), lowY);
    fillPath.closeSubpath();
    QColor fillColor(ch.color.red(), ch.color.green(), ch.color.blue(), 18);
    p.fillPath(fillPath, fillColor);

    (void)prevX; // suppress unused warning
}

// ═════════════════════════════════════════════════════════════════════════════
// OscilloscopePanel
// ═════════════════════════════════════════════════════════════════════════════

OscilloscopePanel::OscilloscopePanel(QWidget* parent) : QWidget(parent) {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // ── Toolbar ──────────────────────────────────────────────────────────────
    auto* toolbar = new QWidget(this);
    toolbar->setFixedHeight(36);
    toolbar->setStyleSheet("background: #F8FAFC; border-bottom: 1px solid #E2E8F0;");
    auto* toolbarLayout = new QHBoxLayout(toolbar);
    toolbarLayout->setContentsMargins(8, 4, 8, 4);
    toolbarLayout->setSpacing(8);

    // Title
    auto* titleLabel = new QLabel("OSCILOSCOPIO", toolbar);
    titleLabel->setStyleSheet(
        "color: #64748B; font-size: 9px; font-weight: 800; "
        "letter-spacing: 1.8px; font-family: 'Segoe UI', Arial;");
    toolbarLayout->addWidget(titleLabel);

    toolbarLayout->addStretch();

    // Status indicator
    m_statusLabel = new QLabel("Simulacao parada", toolbar);
    m_statusLabel->setStyleSheet(
        "color: #94A3B8; font-size: 9px; font-family: 'Segoe UI', Arial;");
    toolbarLayout->addWidget(m_statusLabel);

    toolbarLayout->addSpacing(12);

    // Zoom label
    auto* zoomLbl = new QLabel("Janela:", toolbar);
    zoomLbl->setStyleSheet("color: #64748B; font-size: 9px; font-family: 'Segoe UI', Arial;");
    toolbarLayout->addWidget(zoomLbl);

    m_zoomLabel = new QLabel("5s", toolbar);
    m_zoomLabel->setFixedWidth(28);
    m_zoomLabel->setStyleSheet(
        "color: #334155; font-size: 9px; font-weight: bold; font-family: 'Segoe UI', Arial;");
    toolbarLayout->addWidget(m_zoomLabel);

    m_zoomSlider = new QSlider(Qt::Horizontal, toolbar);
    m_zoomSlider->setRange(1, 8);
    m_zoomSlider->setValue(3);
    m_zoomSlider->setFixedWidth(90);
    m_zoomSlider->setStyleSheet(
        "QSlider::groove:horizontal { height: 4px; background: #E2E8F0; border-radius: 2px; }"
        "QSlider::handle:horizontal { width: 12px; height: 12px; margin: -4px 0; "
        "  background: #4F46E5; border-radius: 6px; }"
        "QSlider::sub-page:horizontal { background: #6366F1; border-radius: 2px; }");
    connect(m_zoomSlider, &QSlider::valueChanged, this, &OscilloscopePanel::onZoomChanged);
    toolbarLayout->addWidget(m_zoomSlider);

    toolbarLayout->addSpacing(8);

    m_clearBtn = new QPushButton("Limpar", toolbar);
    m_clearBtn->setFixedHeight(22);
    m_clearBtn->setStyleSheet(
        "QPushButton { background: #F1F5F9; border: 1px solid #CBD5E1; border-radius: 4px; "
        "  color: #475569; font-size: 9px; padding: 0 10px; font-family: 'Segoe UI', Arial; }"
        "QPushButton:hover { background: #EEF2FF; border-color: #6366F1; color: #4338CA; }");
    connect(m_clearBtn, &QPushButton::clicked, this, &OscilloscopePanel::clearChannels);
    toolbarLayout->addWidget(m_clearBtn);

    mainLayout->addWidget(toolbar);

    // ── Waveform view ─────────────────────────────────────────────────────────
    m_view = new OscilloscopeView(this);
    m_view->setChannels(&m_channels);
    m_view->setWindowMs(m_windowMs);
    m_view->setCurrentTimeMs(0);
    m_view->setRunning(false);
    mainLayout->addWidget(m_view, 1);

    // ── Repaint timer (20 fps while running) ──────────────────────────────────
    m_repaintTimer = new QTimer(this);
    m_repaintTimer->setInterval(50);
    connect(m_repaintTimer, &QTimer::timeout, this, &OscilloscopePanel::repaintView);
}

void OscilloscopePanel::onSimulationStarted() {
    m_channels.clear();
    m_elapsed.restart();
    m_running = true;
    m_view->setRunning(true);
    m_view->setCurrentTimeMs(0);
    m_statusLabel->setStyleSheet(
        "color: #22C55E; font-size: 9px; font-weight: bold; font-family: 'Segoe UI', Arial;");
    m_statusLabel->setText("AO VIVO");
    m_repaintTimer->start();
}

void OscilloscopePanel::onSimulationStopped() {
    m_running = false;
    m_view->setRunning(false);
    m_repaintTimer->stop();
    m_view->update();
    m_statusLabel->setStyleSheet(
        "color: #94A3B8; font-size: 9px; font-family: 'Segoe UI', Arial;");
    m_statusLabel->setText("Simulacao parada");
}

void OscilloscopePanel::onPinStateChanged(const QString& compId, const QString& pinName, bool isHigh) {
    if (!m_running) return;

    OscChannel* ch = findOrCreateChannel(compId, pinName);
    if (!ch) return;

    qint64 now = m_elapsed.elapsed();

    // If channel is new, seed with LOW at t=0 so the waveform starts at the baseline
    if (ch->samples.isEmpty()) {
        ch->samples.append({ 0, false });
    }

    // Avoid duplicate consecutive identical states
    if (!ch->samples.isEmpty() && ch->samples.last().isHigh == isHigh) return;

    ch->samples.append({ now, isHigh });
}

void OscilloscopePanel::clearChannels() {
    m_channels.clear();
    if (m_running) {
        m_elapsed.restart();
    }
    m_view->setCurrentTimeMs(0);
    m_view->update();
}

void OscilloscopePanel::onZoomChanged(int value) {
    static const qint64 windowTable[] = { 1000, 2000, 5000, 10000, 15000, 20000, 30000, 60000 };
    int idx = std::max(0, std::min(value - 1, 7));
    m_windowMs = windowTable[idx];

    static const char* labels[] = { "1s", "2s", "5s", "10s", "15s", "20s", "30s", "60s" };
    m_zoomLabel->setText(labels[idx]);

    m_view->setWindowMs(m_windowMs);
    m_view->update();
}

void OscilloscopePanel::repaintView() {
    if (!m_running) return;
    qint64 now = m_elapsed.elapsed();
    m_view->setCurrentTimeMs(now);
    m_view->update();
}

// ─── Helpers ─────────────────────────────────────────────────────────────────

OscChannel* OscilloscopePanel::findOrCreateChannel(const QString& compId, const QString& pinName) {
    QString key = compId + "::" + pinName;
    for (auto& ch : m_channels) {
        if (ch.id == key) return &ch;
    }

    OscChannel newCh;
    newCh.id    = key;
    newCh.label = labelForComponent(compId, pinName);
    newCh.color = colorForComponent(compId, pinName);
    m_channels.append(newCh);
    return &m_channels.last();
}

QColor OscilloscopePanel::colorForComponent(const QString& compId, const QString& pinName) {
    // Color palette cycling through nice vivid hues
    static const QColor palette[] = {
        QColor(239, 68, 68),   // red — LED
        QColor(245, 158, 11),  // amber — Buzzer
        QColor(16, 185, 129),  // emerald — Digital actuator
        QColor(59, 130, 246),  // blue — Generic
        QColor(168, 85, 247),  // violet
        QColor(236, 72, 153),  // pink
        QColor(20, 184, 166),  // teal
        QColor(251, 191, 36),  // yellow
    };

    // Type-based hinting
    if (compId.startsWith("led_") || compId.contains("led"))
        return palette[0];
    if (compId.startsWith("buzzer_") || compId.contains("buzzer"))
        return palette[1];
    if (pinName.contains("GND") || pinName.contains("gnd"))
        return QColor(100, 116, 139);
    if (pinName.contains("VCC") || pinName.contains("5V") || pinName.contains("3V3"))
        return QColor(239, 68, 68);

    // Hash-based cycling for the rest
    int hash = 0;
    for (QChar c : compId) hash += c.unicode();
    for (QChar c : pinName) hash += c.unicode();
    return palette[(hash & 0xFF) % 8];
}

QString OscilloscopePanel::labelForComponent(const QString& compId, const QString& pinName) {
    // Extract a short display name: "led_1" -> "LED 1", "buzzer_2" -> "BUZ 2"
    QString name = compId;
    name = name.replace('_', ' ').toUpper();
    return name + "\n" + pinName;
}
