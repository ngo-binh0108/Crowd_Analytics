#include "Overlay.h"

#include <QFontMetricsF>
#include <QPainter>
#include <QtMath>

namespace Overlay {

QColor levelColor(DensityLevel level)
{
    switch (level) {
    case DensityLevel::Low:      return QColor(0x0c, 0xa3, 0x0c);
    case DensityLevel::Moderate: return QColor(0xfa, 0xb2, 0x19);
    case DensityLevel::High:     return QColor(0xec, 0x83, 0x5a);
    case DensityLevel::Critical: return QColor(0xd0, 0x3b, 0x3b);
    }
    return QColor(0x0c, 0xa3, 0x0c);
}

QColor personColor() { return QColor(0x3d, 0xdc, 0x84); }
QColor alertColor() { return QColor(0xff, 0x4d, 0x4f); }
QColor bagColor() { return QColor(0x90, 0x85, 0xe9); }
static QColor crowdColor() { return QColor(0xfa, 0xb2, 0x19); }

static QPointF mapPt(const QRectF& t, const QPointF& n)
{
    return QPointF(t.x() + n.x() * t.width(), t.y() + n.y() * t.height());
}

static QRectF drawChip(QPainter& p, QPointF topLeft, const QString& text, const QColor& bg, const QColor& fg,
                       double s, const QRectF& bounds)
{
    const QFontMetricsF fm(p.font());
    QRectF r(topLeft, QSizeF(fm.horizontalAdvance(text) + 10 * s, fm.height() + 3 * s));
    // Keep the chip inside the video area.
    if (r.right() > bounds.right())
        r.moveRight(bounds.right() - 1);
    if (r.left() < bounds.left())
        r.moveLeft(bounds.left() + 1);
    if (r.top() < bounds.top())
        r.moveTop(bounds.top() + 1);
    if (r.bottom() > bounds.bottom())
        r.moveBottom(bounds.bottom() - 1);
    p.setPen(Qt::NoPen);
    p.setBrush(bg);
    p.drawRoundedRect(r, 3 * s, 3 * s);
    p.setPen(fg);
    p.drawText(r, Qt::AlignCenter, text);
    return r;
}

void lineNormal(const CountLine& line, double aspect, QPointF& unitNormal)
{
    const double dx = (line.b.x() - line.a.x()) * aspect;
    const double dy = line.b.y() - line.a.y();
    const double len = std::hypot(dx, dy);
    if (len < 1e-9) {
        unitNormal = QPointF(0, 1);
        return;
    }
    // Positive side of cross(b - a, p - a) is the "In" side.
    QPointF n(-dy / len, dx / len);
    if (line.invert)
        n = -n;
    unitNormal = n;
}

static const ZoneStat* findZoneStat(const OverlayData& d, const QString& id)
{
    if (!d.zoneStats)
        return nullptr;
    for (const ZoneStat& s : *d.zoneStats)
        if (s.zoneId == id)
            return &s;
    return nullptr;
}

static const LineStat* findLineStat(const OverlayData& d, const QString& id)
{
    if (!d.lineStats)
        return nullptr;
    for (const LineStat& s : *d.lineStats)
        if (s.lineId == id)
            return &s;
    return nullptr;
}

static void drawArrow(QPainter& p, const QPointF& from, const QPointF& to, const QColor& c, double s)
{
    p.setPen(QPen(c, 2.2 * s, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawLine(from, to);
    const QPointF d = to - from;
    const double len = std::hypot(d.x(), d.y());
    if (len < 1e-6)
        return;
    const QPointF u = d / len;
    const QPointF perp(-u.y(), u.x());
    const double head = 7 * s;
    QPolygonF tri;
    tri << to << (to - u * head + perp * head * 0.6) << (to - u * head - perp * head * 0.6);
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    p.drawPolygon(tri);
}

void draw(QPainter& p, const QRectF& t, const OverlayData& d, const OverlayOptions& opt)
{
    if (t.width() < 2 || t.height() < 2)
        return;
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    const double s = qBound(0.7, t.height() / 540.0, 2.2);
    QFont f = p.font();
    f.setPixelSize(qMax(9, int(std::lround(11 * s))));
    f.setBold(true);
    p.setFont(f);
    const QColor darkText(0x08, 0x10, 0x1e);

    // ---- zones -----------------------------------------------------------
    if (opt.showZones && d.zones) {
        for (const Zone& z : *d.zones) {
            if (!z.enabled || z.polygon.size() < 3)
                continue;
            QPolygonF poly;
            for (const QPointF& pt : z.polygon)
                poly << mapPt(t, pt);
            const ZoneStat* st = findZoneStat(d, z.id);
            QColor c = z.color.isValid() ? z.color : QColor(0x39, 0x87, 0xe5);
            const bool restricted = z.type == ZoneType::Restricted;
            if (restricted)
                c = alertColor();
            QColor edge = c;
            if (st && !restricted && st->level >= DensityLevel::High)
                edge = levelColor(st->level);
            QColor fill = c;
            fill.setAlpha(restricted ? 34 : 30);
            p.setPen(QPen(edge, 1.6 * s, restricted ? Qt::DashLine : Qt::SolidLine));
            p.setBrush(fill);
            p.drawPolygon(poly);
            if (restricted) {
                QColor hatch = c;
                hatch.setAlpha(70);
                p.setPen(Qt::NoPen);
                p.setBrush(QBrush(hatch, Qt::BDiagPattern));
                p.drawPolygon(poly);
            }
            if (!opt.compact) {
                QString label = z.name;
                if (st) {
                    if (!restricted && z.capacity > 0)
                        label += QStringLiteral("  %1/%2").arg(st->count).arg(z.capacity);
                    else
                        label += QStringLiteral("  %1").arg(st->count);
                }
                QColor bg = edge;
                bg.setAlpha(215);
                const QRectF br = poly.boundingRect();
                drawChip(p, br.topLeft() + QPointF(4 * s, 4 * s), label, bg, darkText, s, t);
            }
        }
    }

    // ---- counting lines --------------------------------------------------
    if (opt.showZones && d.lines) {
        const double aspect = t.width() / t.height();
        for (const CountLine& l : *d.lines) {
            if (!l.enabled)
                continue;
            const QPointF a = mapPt(t, l.a);
            const QPointF b = mapPt(t, l.b);
            const QColor c = l.color.isValid() ? l.color : QColor(0xfa, 0xb2, 0x19);
            p.setPen(QPen(c, 2.6 * s, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(a, b);
            p.setPen(Qt::NoPen);
            p.setBrush(c);
            p.drawEllipse(a, 3.5 * s, 3.5 * s);
            p.drawEllipse(b, 3.5 * s, 3.5 * s);

            QPointF n;
            lineNormal(l, aspect, n);
            const QPointF mid = (a + b) / 2;
            drawArrow(p, mid, mid + n * 24 * s, c, s);

            if (!opt.compact) {
                QString label = l.name;
                if (const LineStat* st = findLineStat(d, l.id))
                    label += QStringLiteral("  IN %1 / OUT %2").arg(st->in).arg(st->out);
                QColor bg = c;
                bg.setAlpha(215);
                drawChip(p, mid - n * 30 * s - QPointF(20 * s, 8 * s), label, bg, darkText, s, t);
            }
        }
    }

    // ---- tracked objects --------------------------------------------------
    if (d.objects) {
        QFont small = f;
        small.setPixelSize(qMax(8, int(std::lround(10 * s))));
        p.setFont(small);
        for (const TrackedObject& o : *d.objects) {
            const QRectF r(t.x() + o.box.x() * t.width(), t.y() + o.box.y() * t.height(), o.box.width() * t.width(),
                           o.box.height() * t.height());
            QColor c = o.cls == ClassBag ? bagColor() : personColor();
            const bool alert = o.flags & (FlagIntrusion | FlagLoitering | FlagRunning | FlagAbandoned);
            if (alert)
                c = alertColor();
            else if (o.flags & FlagInCrowd)
                c = crowdColor();

            if (opt.showTrails && o.cls == ClassPerson && o.trail.size() > 1) {
                const int n = o.trail.size();
                for (int k = 1; k < n; ++k) {
                    QColor tc = c;
                    tc.setAlpha(40 + 180 * k / n);
                    p.setPen(QPen(tc, 1.6 * s, Qt::SolidLine, Qt::RoundCap));
                    p.drawLine(mapPt(t, o.trail[k - 1]), mapPt(t, o.trail[k]));
                }
            }

            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(c, (opt.compact ? 1.2 : 1.7) * s));
            p.drawRect(r);

            if (!opt.compact && opt.showIds) {
                QString label = o.cls == ClassBag ? QStringLiteral("BAG %1").arg(o.id)
                                                  : QStringLiteral("P-%1").arg(o.id, 3, 10, QLatin1Char('0'));
                if (opt.showConfidence)
                    label += QStringLiteral(" %1").arg(double(o.score), 0, 'f', 2);
                if (o.flags & FlagIntrusion)
                    label += QStringLiteral(" INTRUSION");
                else if (o.flags & FlagAbandoned)
                    label += QStringLiteral(" UNATTENDED");
                else if (o.flags & FlagLoitering)
                    label += QStringLiteral(" LOITERING");
                else if (o.flags & FlagRunning)
                    label += QStringLiteral(" RUNNING");
                const QFontMetricsF fm(small);
                drawChip(p, QPointF(r.left(), r.top() - fm.height() - 4 * s), label, c, darkText, s, t);
            }
        }
    }
    p.restore();
}

} // namespace Overlay
