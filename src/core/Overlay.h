#pragma once

#include "Types.h"

#include <QColor>
#include <QRectF>

class QPainter;

struct OverlayOptions {
    bool showZones = true;
    bool showIds = true;
    bool showTrails = true;
    bool showConfidence = false;
    bool compact = false;      // small tiles: no labels
};

struct OverlayData {
    const QVector<Zone>* zones = nullptr;
    const QVector<CountLine>* lines = nullptr;
    const QVector<TrackedObject>* objects = nullptr;
    const QVector<ZoneStat>* zoneStats = nullptr;
    const QVector<LineStat>* lineStats = nullptr;
};

// Draws zones, counting lines and tracked objects over a video frame that is
// displayed inside `target`. Shared by the live tiles and saved snapshots.
namespace Overlay {

QColor levelColor(DensityLevel level);
QColor personColor();
QColor alertColor();
QColor bagColor();

void draw(QPainter& p, const QRectF& target, const OverlayData& data, const OverlayOptions& opt);

// Short direction arrow for a counting line (normal pointing to the "In" side).
void lineNormal(const CountLine& line, double aspect, QPointF& unitNormal);

} // namespace Overlay
