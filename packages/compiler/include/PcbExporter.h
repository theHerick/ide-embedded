#pragma once
#include <QString>
#include <QVector>
#include "ComponentItem.h"
#include "ConnectionCable.h"

class PcbExporter {
public:
    // Exports the workspace schematic components and tracks (cables) to KiCad 6.0+ .kicad_pcb layout format
    static bool exportToKiCad(const QString& filePath,
                              const QVector<ComponentItem*>& components,
                              const QVector<ConnectionCable*>& cables);

    static bool exportToLaserPNG(const QString& filePath,
                                 const QVector<ComponentItem*>& components,
                                 const QVector<ConnectionCable*>& cables,
                                 double trackWidthMil = 80.0,
                                 double lineWidthMil = 10.0);

    // Generate the laser image as a QImage (without saving) — used by the preview dialog
    static QImage generateLaserImage(const QVector<ComponentItem*>& components,
                                     const QVector<ConnectionCable*>& cables,
                                     double trackWidthMil = 80.0,
                                     double lineWidthMil = 10.0);

    static QImage generateDrillImage(const QVector<ComponentItem*>& components);
    
    static bool exportDrillPNG(const QString& filePath, const QVector<ComponentItem*>& components);
};
