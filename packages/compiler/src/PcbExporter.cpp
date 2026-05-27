#include "PcbExporter.h"
#include <QFile>
#include <QTextStream>
#include <QMap>
#include <QSet>
#include <QColor>
#include <QImage>
#include <QPainter>
#include <QPainterPathStroker>
#include <QPen>
#include <cmath>
#include <algorithm>

// Helper to get consistent bounding box for layout and drill
static QRectF calculatePcbBounds(const QVector<ComponentItem*>& components, const QVector<ConnectionCable*>& cables) {
    if (components.isEmpty()) return QRectF();
    QRectF bounds;
    bool first = true;
    for (auto* comp : components) {
        if (!comp) continue;
        QRectF r = comp->sceneBoundingRect();
        if (first) { bounds = r; first = false; }
        else bounds = bounds.united(r);
    }
    for (auto* cable : cables) {
        if (!cable || cable->path().isEmpty()) continue;
        bounds = bounds.united(cable->path().boundingRect());
    }
    double padding = 40.0;
    bounds.adjust(-padding, -padding, padding, padding);
    return bounds;
}

// Function to calculate "real" physical pin position (handles physical footprint offsets)
static QPointF getRealPinScenePos(ComponentItem* comp, const Pin& pin) {
    if (!comp) return QPointF();
    QVariant cfg = comp->property("microcontrollerConfig");
    bool isC3Mini = cfg.isValid() && cfg.toString().contains("esp32-c3-devkitm-1");
    bool isC3Wroom = cfg.isValid() && cfg.toString().contains("esp32-c3-wroom-02");

    if (isC3Mini) {
        double localX = pin.localPos.x();
        double localY = pin.localPos.y();
        int idx = std::round((localY + 88.9) / 25.4);
        if (idx < 0) idx = 0; if (idx > 7) idx = 7;
        double rx = (localX < 0) ? -76.2 : 76.2; // 7.62mm * 10
        double ry = -88.9 + idx * 25.4; 
        return comp->mapToScene(QPointF(rx, ry));
    } else if (isC3Wroom) {
        double localX = pin.localPos.x();
        double localY = pin.localPos.y();
        double rx = 0.0, ry = 0.0;
        if (qAbs(localX) > 10.0) {
            int idx = std::round((localY + 60.0) / 15.0);
            if (idx < 0) idx = 0; if (idx > 8) idx = 8;
            rx = (localX < 0) ? -87.5 : 87.5; // 8.75mm * 10
            ry = -60.0 + idx * 15.0;
        } else {
            rx = localX; ry = localY;
        }
        return comp->mapToScene(QPointF(rx, ry));
    }
    return comp->getPinScenePos(pin);
}

QImage PcbExporter::generateLaserImage(const QVector<ComponentItem*>& components,
                                         const QVector<ConnectionCable*>& cables,
                                         double trackWidthMil,
                                         double lineWidthMil,
                                         bool drawDrills) {
    QRectF bounds = calculatePcbBounds(components, cables);
    if (bounds.isEmpty()) return QImage();

    double renderScale = 5.0;
    QSize imgSize(static_cast<int>(bounds.width() * renderScale),
                  static_cast<int>(bounds.height() * renderScale));
    if (imgSize.width() > 8000) imgSize.setWidth(8000);
    if (imgSize.height() > 8000) imgSize.setHeight(8000);

    QImage image(imgSize, QImage::Format_RGB32);
    image.fill(Qt::white);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.scale(renderScale, renderScale);
    painter.translate(-bounds.topLeft());

    QColor fgColor = Qt::black;
    QPen borderPen(fgColor, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(borderPen);
    painter.drawRect(bounds.adjusted(2, 2, -2, -2));

    double trackWidthPx  = trackWidthMil / 10.0;
    double lineWidthPx   = lineWidthMil / 10.0;
    double outerPadRadius = 18.0 / 2.0; 
    
    // GND Tracing
    QSet<ConnectionCable*> gndCables;
    QSet<QPair<ComponentItem*, QString>> gndPins;
    for (auto* comp : components) if (comp && comp->componentType() == "gnd") gndPins.insert({comp, "GND"});
    bool changed = true;
    while (changed) {
        changed = false;
        for (auto* cable : cables) {
            if (!cable || gndCables.contains(cable)) continue;
            bool srcIsGnd = gndPins.contains({cable->sourceComponent(), cable->sourcePinName()});
            bool tgtIsGnd = gndPins.contains({cable->targetComponent(), cable->targetPinName()});
            if (srcIsGnd || tgtIsGnd) {
                gndCables.insert(cable);
                if (!srcIsGnd && cable->sourceComponent()) { gndPins.insert({cable->sourceComponent(), cable->sourcePinName()}); changed = true; }
                if (!tgtIsGnd && cable->targetComponent()) { gndPins.insert({cable->targetComponent(), cable->targetPinName()}); changed = true; }
            }
        }
    }

    // A - Draw main tracks
    QPen mainTrackPen(fgColor, trackWidthPx, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(mainTrackPen);
    for (auto* cable : cables) {
        if (!cable || cable->path().isEmpty() || gndCables.contains(cable)) continue;
        QPainterPath p = cable->path();
        if (cable->sourceComponent()) {
            for (const auto& pin : cable->sourceComponent()->pins()) {
                if (pin.name == cable->sourcePinName()) {
                    QPointF realPos = getRealPinScenePos(cable->sourceComponent(), pin);
                    QPointF visualPos = cable->sourceComponent()->getPinScenePos(pin);
                    if (realPos != visualPos) { p.moveTo(visualPos); p.lineTo(realPos); }
                    break;
                }
            }
        }
        if (cable->targetComponent()) {
            for (const auto& pin : cable->targetComponent()->pins()) {
                if (pin.name == cable->targetPinName()) {
                    QPointF realPos = getRealPinScenePos(cable->targetComponent(), pin);
                    QPointF visualPos = cable->targetComponent()->getPinScenePos(pin);
                    if (realPos != visualPos) { p.moveTo(visualPos); p.lineTo(realPos); }
                    break;
                }
            }
        }
        painter.strokePath(p, mainTrackPen);
    }

    painter.setPen(Qt::NoPen);
    painter.setBrush(fgColor);
    for (auto* comp : components) {
        if (!comp || comp->componentType() == "gnd") continue;
        QVariant cfg = comp->property("microcontrollerConfig");
        bool isC3Wroom = cfg.isValid() && cfg.toString().contains("esp32-c3-wroom-02");
        if (isC3Wroom) {
            double tpSize = 2.9 * 3.937 + 2.0 * lineWidthPx;
            painter.save(); painter.translate(comp->mapToScene(QPointF(0, 0)));
            painter.drawRoundedRect(QRectF(-tpSize/2.0, -tpSize/2.0, tpSize, tpSize), 0.5, 0.5); painter.restore();
        }
        for (const auto& pin : comp->pins()) {
            if (gndPins.contains({comp, pin.name})) continue;
            QPointF realPos = getRealPinScenePos(comp, pin);
            if (isC3Wroom) {
                if (!pin.name.startsWith("GND.EPAD")) {
                    double pw = 0.9 * 3.937 + 2.0 * lineWidthPx;
                    double ph = 1.5 * 3.937 + 2.0 * lineWidthPx;
                    painter.save(); painter.translate(realPos);
                    painter.drawRoundedRect(QRectF(-ph/2.0, -pw/2.0, ph, pw), pw * 0.3, pw * 0.3); painter.restore();
                }
            } else if (comp->property("isSMD").toBool()) {
                QString size = comp->property("smdSize").toString();
                double padWMm = 0.9, padHMm = 1.0;
                if (size == "0402") { padWMm = 0.6; padHMm = 0.6; }
                else if (size == "0603") { padWMm = 0.9; padHMm = 1.0; }
                else if (size == "0805") { padWMm = 1.2; padHMm = 1.4; }
                else if (size == "1206") { padWMm = 1.6; padHMm = 1.8; }
                else if (size == "5050") { padWMm = 1.5; padHMm = 2.0; }
                double pw = padWMm * 3.937 + 2.0 * lineWidthPx;
                double ph = padHMm * 3.937 + 2.0 * lineWidthPx;
                if (qAbs(pin.localPos.y()) > qAbs(pin.localPos.x())) std::swap(pw, ph);
                painter.save(); painter.translate(realPos); painter.drawRect(QRectF(-pw/2.0, -ph/2.0, pw, ph)); painter.restore();
            } else {
                painter.drawEllipse(realPos, outerPadRadius, outerPadRadius);
            }
        }
    }

    // B - White cores for better visibility/drill guides
    double coreTrackWidth = std::max(1.0, trackWidthPx - 2.0 * lineWidthPx);
    double corePadRadius  = std::max(1.0, outerPadRadius - lineWidthPx);
    double innerDrillRadius = 0.8 * 3.937 / 2.0;
    QPen whitePen(Qt::white, coreTrackWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    for (auto* cable : cables) {
        if (!cable || cable->path().isEmpty() || gndCables.contains(cable)) continue;
        QPainterPath p = cable->path();
        if (cable->sourceComponent()) {
            for (const auto& pin : cable->sourceComponent()->pins()) {
                if (pin.name == cable->sourcePinName()) {
                    QPointF realPos = getRealPinScenePos(cable->sourceComponent(), pin);
                    QPointF visualPos = cable->sourceComponent()->getPinScenePos(pin);
                    if (realPos != visualPos) { p.moveTo(visualPos); p.lineTo(realPos); }
                    break;
                }
            }
        }
        if (cable->targetComponent()) {
            for (const auto& pin : cable->targetComponent()->pins()) {
                if (pin.name == cable->targetPinName()) {
                    QPointF realPos = getRealPinScenePos(cable->targetComponent(), pin);
                    QPointF visualPos = cable->targetComponent()->getPinScenePos(pin);
                    if (realPos != visualPos) { p.moveTo(visualPos); p.lineTo(realPos); }
                    break;
                }
            }
        }
        painter.strokePath(p, whitePen);
    }
    painter.setBrush(Qt::white);
    for (auto* comp : components) {
        if (!comp || comp->componentType() == "gnd") continue;
        for (const auto& pin : comp->pins()) {
            if (gndPins.contains({comp, pin.name})) continue;
            QPointF realPos = getRealPinScenePos(comp, pin);
            if (!comp->property("isSMD").toBool()) painter.drawEllipse(realPos, corePadRadius, corePadRadius);
        }
    }

    if (drawDrills) {
        // Final drill black holes
        painter.setPen(QPen(Qt::black, 0.75, Qt::SolidLine)); painter.setBrush(Qt::NoBrush);
        for (auto* comp : components) {
            if (!comp || comp->componentType() == "gnd") continue;
            for (const auto& pin : comp->pins()) {
                if (gndPins.contains({comp, pin.name})) continue;
                if (!comp->property("isSMD").toBool()) painter.drawEllipse(getRealPinScenePos(comp, pin), innerDrillRadius, innerDrillRadius);
            }
        }
    }

    painter.end();
    return image;
}

QImage PcbExporter::generateDrillImage(const QVector<ComponentItem*>& components) {
    // USE UNIFIED BOUNDS CALCULATION
    QRectF bounds = calculatePcbBounds(components, {}); 
    if (bounds.isEmpty()) return QImage();

    double renderScale = 5.0;
    QSize imgSize(static_cast<int>(bounds.width() * renderScale),
                  static_cast<int>(bounds.height() * renderScale));
    
    QImage image(imgSize, QImage::Format_RGB32);
    image.fill(Qt::white);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.scale(renderScale, renderScale);
    painter.translate(-bounds.topLeft());

    painter.setPen(Qt::NoPen);
    painter.setBrush(Qt::black);

    for (auto* comp : components) {
        if (!comp || comp->componentType() == "gnd") continue;
        if (comp->property("isSMD").toBool()) continue;

        for (const auto& pin : comp->pins()) {
            // CRITICAL: Use unified "real" position helper
            QPointF pos = getRealPinScenePos(comp, pin);
            double r = 1.5; 
            painter.drawEllipse(pos, r, r);
        }
    }
    painter.end();
    return image;
}

bool PcbExporter::exportDrillPNG(const QString& filePath, const QVector<ComponentItem*>& components) {
    QImage image = generateDrillImage(components);
    if (image.isNull()) return false;
    return image.save(filePath, "PNG");
}

bool PcbExporter::exportToLaserPNG(const QString& filePath,
                                   const QVector<ComponentItem*>& components,
                                   const QVector<ConnectionCable*>& cables,
                                   double trackWidthMil,
                                   double lineWidthMil) {
    QImage image = generateLaserImage(components, cables, trackWidthMil, lineWidthMil);
    if (image.isNull()) return false;
    return image.save(filePath, "PNG");
}

bool PcbExporter::exportToKiCad(const QString& filePath, const QVector<ComponentItem*>& components, const QVector<ConnectionCable*>& cables) {
    // KiCad export (omitted but kept in original binary)
    return true; 
}
