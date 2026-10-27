#include "Charts.h"
#include "ui/Theme.h"

#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

namespace ChartUtil {

double niceCeil(double v)
{
    if (v <= 0)
        return 1;
    const double e = std::pow(10.0, std::floor(std::log10(v)));
    const double f = v / e;
    const double nf = f <= 1 ? 1 : f <= 2 ? 2 : f <= 2.5 ? 2.5 : f <= 5 ? 5 : 10;
    return nf * e;
}

QColor infernoColor(double t)
{
    static const QColor stops[] = { QColor("#000004"), QColor("#420a68"), QColor("#932667"),
                                    QColor("#dd513a"), QColor("#fca50a"), QColor("#fcffa4") };
    static const double pos[] = { 0.0, 0.25, 0.5, 0.75, 0.9, 1.0 };
    t = qBound(0.0, t, 1.0);
    for (int i = 1; i < 6; ++i) {
        if (t <= pos[i]) {
            const double f = (t - pos[i - 1]) / (pos[i] - pos[i - 1]);
            const QColor& a = stops[i - 1];
            const QColor& b = stops[i];
            return QColor(int(a.red() + (b.red() - a.red()) * f), int(a.green() + (b.green() - a.green()) * f),
                          int(a.blue() + (b.blue() - a.blue()) * f));
        }
    }
    return stops[5];
}

} // namespace ChartUtil

namespace {

QFont smallFont(const QWidget* w, int px = 10, bool bold = false)
{
    QFont f = w->font();
    f.setPixelSize(px);
    f.setBold(bold);
    return f;
}

QString fmtValue(double v, int decimals)
{
    if (decimals <= 0)
        return QString::number(qRound64(v));
    return QString::number(v, 'f', decimals);
}

// Step for 4 horizontal grid intervals.
double gridStep(double maxValue, bool integers)
{
    double step = ChartUtil::niceCeil(qMax(maxValue, 1e-9) / 4.0);
    if (integers)
        step = qMax(1.0, std::ceil(step));
    return step;
}

void drawTooltip(QPainter& p, const QRectF& bounds, QPointF anchor, const QString& title,
                 const QVector<QPair<QColor, QString>>& rows)
{
    QFont bold = p.font();
    bold.setBold(true);
    QFont normal = p.font();
    normal.setBold(false);
    const QFontMetricsF fmB(bold);
    const QFontMetricsF fm(normal);
    double w = fmB.horizontalAdvance(title);
    for (const auto& r : rows)
        w = qMax(w, fm.horizontalAdvance(r.second) + 16);
    w += 20;
    const double lineH = fm.height() + 3;
    const double h = 12 + fmB.height() + rows.size() * lineH;
    QRectF box(anchor.x() + 12, anchor.y() - h / 2, w, h);
    if (box.right() > bounds.right())
        box.moveRight(anchor.x() - 12);
    if (box.top() < bounds.top())
        box.moveTop(bounds.top());
    if (box.bottom() > bounds.bottom())
        box.moveBottom(bounds.bottom());

    p.setPen(QPen(Theme::border2(), 1));
    p.setBrush(QColor(0x0f, 0x1b, 0x30, 240));
    p.drawRoundedRect(box, 6, 6);
    p.setFont(bold);
    p.setPen(Theme::text());
    p.drawText(QRectF(box.left() + 10, box.top() + 6, w - 20, fmB.height()), Qt::AlignLeft | Qt::AlignVCenter, title);
    p.setFont(normal);
    double y = box.top() + 8 + fmB.height();
    for (const auto& r : rows) {
        p.setPen(Qt::NoPen);
        p.setBrush(r.first);
        p.drawEllipse(QPointF(box.left() + 14, y + lineH / 2), 3.5, 3.5);
        p.setPen(Theme::text2());
        p.drawText(QRectF(box.left() + 24, y, w - 30, lineH), Qt::AlignLeft | Qt::AlignVCenter, r.second);
        y += lineH;
    }
}

} // namespace

// ===========================================================================
// LineChart
// ===========================================================================

LineChart::LineChart(QWidget* parent) : QWidget(parent)
{
    setMouseTracking(true);
    setAttribute(Qt::WA_Hover);
    m_emptyText = tr("No data yet");
}

void LineChart::setData(const QStringList& xLabels, const QVector<ChartSeries>& series)
{
    m_labels = xLabels;
    m_series = series;
    if (m_hover >= m_labels.size())
        m_hover = -1;
    update();
}

QRectF LineChart::plotRect() const
{
    double maxV = 0;
    for (const ChartSeries& s : m_series)
        for (double v : s.values)
            maxV = qMax(maxV, v);
    const double yMax = gridStep(maxV, m_decimals == 0) * 4;
    const QFontMetricsF fm(smallFont(this));
    const double left = fm.horizontalAdvance(fmtValue(yMax, m_decimals)) + 12;
    const bool legend = m_series.size() > 1;
    return QRectF(left, legend ? 24 : 10, width() - left - 14, height() - (legend ? 24 : 10) - 22);
}

void LineChart::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF pr = plotRect();
    if (pr.width() < 20 || pr.height() < 20)
        return;

    double maxV = 0;
    bool hasData = false;
    for (const ChartSeries& s : m_series)
        for (double v : s.values) {
            maxV = qMax(maxV, v);
            hasData = hasData || v > 0;
        }
    const double step = gridStep(maxV, m_decimals == 0);
    const double yMax = step * 4;
    const int n = m_labels.size();

    // Grid & y labels
    p.setFont(smallFont(this));
    for (int i = 0; i <= 4; ++i) {
        const double y = pr.bottom() - pr.height() * i / 4.0;
        p.setPen(QPen(i == 0 ? Theme::border2() : QColor(0x1a, 0x28, 0x40), 1));
        p.drawLine(QPointF(pr.left(), y), QPointF(pr.right(), y));
        p.setPen(Theme::text3());
        p.drawText(QRectF(0, y - 8, pr.left() - 6, 16), Qt::AlignRight | Qt::AlignVCenter, fmtValue(step * i, m_decimals));
    }

    auto xAt = [&](int i) { return n <= 1 ? pr.center().x() : pr.left() + pr.width() * i / double(n - 1); };
    auto yAt = [&](double v) { return pr.bottom() - pr.height() * (v / yMax); };

    // X labels (thinned so they never overlap)
    if (n > 0) {
        const QFontMetricsF fm(p.font());
        double maxW = 0;
        for (const QString& l : m_labels)
            maxW = qMax(maxW, fm.horizontalAdvance(l));
        const int stepL = qMax(1, int(std::ceil((maxW + 14) * n / qMax(1.0, pr.width()))));
        p.setPen(Theme::text3());
        for (int i = 0; i < n; i += stepL) {
            const double x = xAt(i);
            p.drawText(QRectF(x - maxW / 2 - 4, pr.bottom() + 5, maxW + 8, 14), Qt::AlignCenter, m_labels[i]);
        }
    }

    // Legend (identity never by colour alone: swatch + name)
    if (m_series.size() > 1) {
        double x = pr.right();
        p.setFont(smallFont(this, 11));
        const QFontMetricsF fm(p.font());
        for (int s = m_series.size() - 1; s >= 0; --s) {
            const double tw = fm.horizontalAdvance(m_series[s].name);
            x -= tw;
            p.setPen(Theme::text2());
            p.drawText(QRectF(x, 2, tw + 2, 16), Qt::AlignLeft | Qt::AlignVCenter, m_series[s].name);
            x -= 18;
            p.setPen(QPen(m_series[s].color, 2.5, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(QPointF(x, 10), QPointF(x + 12, 10));
            x -= 14;
        }
    }

    // Series
    for (const ChartSeries& s : m_series) {
        if (s.values.isEmpty())
            continue;
        QPainterPath line;
        for (int i = 0; i < s.values.size() && i < n; ++i) {
            const QPointF pt(xAt(i), yAt(s.values[i]));
            if (i == 0)
                line.moveTo(pt);
            else
                line.lineTo(pt);
        }
        if (s.area && n > 1) {
            QPainterPath area = line;
            area.lineTo(xAt(qMin(n, s.values.size()) - 1), pr.bottom());
            area.lineTo(xAt(0), pr.bottom());
            area.closeSubpath();
            QLinearGradient g(0, pr.top(), 0, pr.bottom());
            QColor c1 = s.color;
            c1.setAlpha(90);
            QColor c2 = s.color;
            c2.setAlpha(0);
            g.setColorAt(0, c1);
            g.setColorAt(1, c2);
            p.fillPath(area, g);
        }
        p.setPen(QPen(s.color, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        p.drawPath(line);
    }

    // Selective direct labels: latest value of each series.
    if (hasData && n > 1 && m_series.size() <= 4) {
        p.setFont(smallFont(this, 10, true));
        QVector<QPair<double, int>> ends;
        for (int s = 0; s < m_series.size(); ++s)
            if (!m_series[s].values.isEmpty())
                ends.push_back({ yAt(m_series[s].values.value(qMin(n, m_series[s].values.size()) - 1)), s });
        std::sort(ends.begin(), ends.end());
        for (int i = 1; i < ends.size(); ++i)
            if (ends[i].first - ends[i - 1].first < 13)
                ends[i].first = ends[i - 1].first + 13;
        for (const auto& e : ends) {
            const ChartSeries& s = m_series[e.second];
            const double v = s.values.value(qMin(n, s.values.size()) - 1);
            p.setPen(Theme::text());
            p.drawText(QRectF(pr.right() - 60, e.first - 16, 58, 14), Qt::AlignRight | Qt::AlignVCenter,
                       fmtValue(v, m_decimals));
        }
    }

    if (!hasData) {
        p.setFont(smallFont(this, 12));
        p.setPen(Theme::text3());
        p.drawText(pr, Qt::AlignCenter, m_emptyText);
        return;
    }

    // Hover crosshair + tooltip
    if (m_hover >= 0 && m_hover < n) {
        const double x = xAt(m_hover);
        p.setPen(QPen(Theme::border2(), 1, Qt::DashLine));
        p.drawLine(QPointF(x, pr.top()), QPointF(x, pr.bottom()));
        QVector<QPair<QColor, QString>> rows;
        double yAnchor = pr.center().y();
        for (const ChartSeries& s : m_series) {
            if (m_hover >= s.values.size())
                continue;
            const QPointF pt(x, yAt(s.values[m_hover]));
            p.setPen(QPen(Theme::surface(), 2));
            p.setBrush(s.color);
            p.drawEllipse(pt, 4.5, 4.5);
            rows.push_back({ s.color, s.name + QStringLiteral(":  ") + fmtValue(s.values[m_hover], m_decimals) });
            yAnchor = pt.y();
        }
        p.setFont(smallFont(this, 11));
        drawTooltip(p, QRectF(rect()), QPointF(x, yAnchor), m_labels[m_hover], rows);
    }
}

void LineChart::mouseMoveEvent(QMouseEvent* e)
{
    const QRectF pr = plotRect();
    const int n = m_labels.size();
    int idx = -1;
    if (n > 0 && pr.adjusted(-10, -10, 10, 10).contains(e->pos())) {
        idx = n == 1 ? 0 : qBound(0, int(std::lround((e->pos().x() - pr.left()) / pr.width() * (n - 1))), n - 1);
    }
    if (idx != m_hover) {
        m_hover = idx;
        update();
    }
}

void LineChart::leaveEvent(QEvent*)
{
    m_hover = -1;
    update();
}

// ===========================================================================
// BarChart
// ===========================================================================

BarChart::BarChart(QWidget* parent) : QWidget(parent)
{
    setMouseTracking(true);
    m_emptyText = tr("No data yet");
}

void BarChart::setData(const QStringList& labels, const QVector<double>& values, const QColor& color)
{
    m_labels = labels;
    m_values = values;
    m_color = color;
    update();
}

void BarChart::setHighlight(int index, const QColor& color, const QString& note)
{
    m_highlight = index;
    m_highlightColor = color;
    m_highlightNote = note;
    update();
}

QRectF BarChart::plotRect() const
{
    double maxV = 0;
    for (double v : m_values)
        maxV = qMax(maxV, v);
    const double yMax = gridStep(maxV, true) * 4;
    const QFontMetricsF fm(smallFont(this));
    const double left = fm.horizontalAdvance(fmtValue(yMax, 0)) + 12;
    return QRectF(left, 12, width() - left - 10, height() - 12 - 22);
}

void BarChart::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF pr = plotRect();
    if (pr.width() < 20 || pr.height() < 20)
        return;
    double maxV = 0;
    for (double v : m_values)
        maxV = qMax(maxV, v);
    const double step = gridStep(maxV, true);
    const double yMax = step * 4;
    const int n = m_values.size();

    p.setFont(smallFont(this));
    for (int i = 0; i <= 4; ++i) {
        const double y = pr.bottom() - pr.height() * i / 4.0;
        p.setPen(QPen(i == 0 ? Theme::border2() : QColor(0x1a, 0x28, 0x40), 1));
        p.drawLine(QPointF(pr.left(), y), QPointF(pr.right(), y));
        p.setPen(Theme::text3());
        p.drawText(QRectF(0, y - 8, pr.left() - 6, 16), Qt::AlignRight | Qt::AlignVCenter, fmtValue(step * i, 0));
    }
    if (n == 0)
        return;

    const double slot = pr.width() / n;
    const double bw = qMax(2.0, slot - 2.0);   // 2px surface gap between bars
    const QFontMetricsF fm(p.font());
    double maxLabelW = 0;
    for (const QString& l : m_labels)
        maxLabelW = qMax(maxLabelW, fm.horizontalAdvance(l));
    const int labelStep = qMax(1, int(std::ceil((maxLabelW + 8) / slot)));

    for (int i = 0; i < n; ++i) {
        const double x = pr.left() + i * slot + (slot - bw) / 2;
        const double h = pr.height() * (m_values[i] / yMax);
        QColor c = i == m_highlight ? m_highlightColor : m_color;
        if (m_hover >= 0 && i != m_hover)
            c.setAlpha(150);
        if (h > 0.5) {
            const QRectF bar(x, pr.bottom() - h, bw, h);
            QPainterPath path;
            const double r = qMin(4.0, qMin(bw / 2, h));
            path.addRoundedRect(bar, r, r);
            path.addRect(QRectF(bar.left(), bar.bottom() - qMin(h, r), bar.width(), qMin(h, r)));   // square base
            path.setFillRule(Qt::WindingFill);
            p.fillPath(path, c);
        }
        if (i % labelStep == 0 && i < m_labels.size()) {
            p.setPen(Theme::text3());
            p.drawText(QRectF(pr.left() + i * slot + slot / 2 - maxLabelW / 2 - 4, pr.bottom() + 5, maxLabelW + 8, 14),
                       Qt::AlignCenter, m_labels[i]);
        }
    }

    if (maxV <= 0) {
        p.setFont(smallFont(this, 12));
        p.setPen(Theme::text3());
        p.drawText(pr, Qt::AlignCenter, m_emptyText);
        return;
    }

    // Annotate the highlighted (peak) bar.
    if (m_highlight >= 0 && m_highlight < n && !m_highlightNote.isEmpty() && m_hover < 0) {
        const double x = pr.left() + m_highlight * slot + slot / 2;
        const double y = pr.bottom() - pr.height() * (m_values[m_highlight] / yMax);
        p.setFont(smallFont(this, 10, true));
        p.setPen(Theme::text());
        const double tw = QFontMetricsF(p.font()).horizontalAdvance(m_highlightNote);
        QRectF tr(x - tw / 2 - 2, y - 17, tw + 4, 14);
        if (tr.left() < pr.left())
            tr.moveLeft(pr.left());
        if (tr.right() > pr.right())
            tr.moveRight(pr.right());
        if (tr.top() < 0)
            tr.moveTop(0);
        p.drawText(tr, Qt::AlignCenter, m_highlightNote);
    }

    if (m_hover >= 0 && m_hover < n) {
        const double x = pr.left() + m_hover * slot + slot / 2;
        const double y = pr.bottom() - pr.height() * (m_values[m_hover] / yMax);
        p.setFont(smallFont(this, 11));
        const QString title = m_hover < m_labels.size() ? m_labels[m_hover] : QString::number(m_hover);
        drawTooltip(p, QRectF(rect()), QPointF(x, y), title,
                    { { m_hover == m_highlight ? m_highlightColor : m_color,
                        tr("Entries: %1").arg(fmtValue(m_values[m_hover], 0)) } });
    }
}

void BarChart::mouseMoveEvent(QMouseEvent* e)
{
    const QRectF pr = plotRect();
    int idx = -1;
    if (!m_values.isEmpty() && pr.contains(e->pos()))
        idx = qBound(0, int((e->pos().x() - pr.left()) / (pr.width() / m_values.size())), m_values.size() - 1);
    if (idx != m_hover) {
        m_hover = idx;
        update();
    }
}

void BarChart::leaveEvent(QEvent*)
{
    m_hover = -1;
    update();
}

// ===========================================================================
// GaugeWidget
// ===========================================================================

GaugeWidget::GaugeWidget(QWidget* parent) : QWidget(parent)
{
    setMinimumSize(160, 130);
}

void GaugeWidget::setValue(int value, int capacity, DensityLevel level, const QString& caption)
{
    m_value = value;
    m_capacity = capacity;
    m_level = level;
    m_caption = caption;
    update();
}

void GaugeWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const double side = qMin(width() - 20.0, (height() - 20.0) * 1.35);
    const QRectF arc((width() - side) / 2, 12, side, side);
    const double pen = qMax(8.0, side / 14);

    p.setPen(QPen(QColor(0x1b, 0x2a, 0x44), pen, Qt::SolidLine, Qt::RoundCap));
    p.drawArc(arc.adjusted(pen / 2, pen / 2, -pen / 2, -pen / 2), 210 * 16, -240 * 16);

    const double frac = m_capacity > 0 ? double(m_value) / m_capacity : 0.0;
    const QColor c = Theme::levelColor(m_level);
    if (frac > 0) {
        p.setPen(QPen(c, pen, Qt::SolidLine, Qt::RoundCap));
        p.drawArc(arc.adjusted(pen / 2, pen / 2, -pen / 2, -pen / 2), 210 * 16, -int(240 * 16 * qMin(1.0, frac)));
    }

    QFont big = font();
    big.setPixelSize(int(qMax(18.0, side / 6)));
    big.setBold(true);
    p.setFont(big);
    p.setPen(Theme::text());
    const QRectF center(arc.left(), arc.center().y() - side * 0.16, arc.width(), side * 0.2);
    p.drawText(center, Qt::AlignCenter, QString::number(m_value));

    QFont small = font();
    small.setPixelSize(11);
    p.setFont(small);
    p.setPen(Theme::text2());
    const QString sub = m_capacity > 0 ? tr("of %1  (%2%)").arg(m_capacity).arg(qRound(frac * 100)) : tr("no capacity set");
    p.drawText(QRectF(arc.left(), center.bottom() + 2, arc.width(), 16), Qt::AlignCenter, sub);

    // Level pill: colour + text label (never colour alone)
    const QString lvl = densityLevelName(m_level);
    QFont pill = small;
    pill.setBold(true);
    p.setFont(pill);
    const double tw = QFontMetricsF(pill).horizontalAdvance(lvl) + 18;
    const QRectF pr(arc.center().x() - tw / 2, center.bottom() + 22, tw, 18);
    QColor bgc = c;
    bgc.setAlpha(50);
    p.setPen(Qt::NoPen);
    p.setBrush(bgc);
    p.drawRoundedRect(pr, 9, 9);
    p.setPen(c.lighter(130));
    p.drawText(pr, Qt::AlignCenter, lvl);

    if (!m_caption.isEmpty()) {
        p.setFont(small);
        p.setPen(Theme::text3());
        p.drawText(QRectF(0, height() - 18, width(), 16), Qt::AlignCenter, m_caption);
    }
}

// ===========================================================================
// ZoneBars
// ===========================================================================

ZoneBars::ZoneBars(QWidget* parent) : QWidget(parent)
{
    m_emptyText = tr("No zones configured");
}

void ZoneBars::setItems(const QVector<ZoneBarItem>& items)
{
    m_items = items;
    updateGeometry();
    update();
}

QSize ZoneBars::sizeHint() const
{
    return QSize(260, qMax(80, m_items.size() * 42 + 4));
}

void ZoneBars::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    if (m_items.isEmpty()) {
        QFont f = font();
        f.setPixelSize(12);
        p.setFont(f);
        p.setPen(Theme::text3());
        p.drawText(rect(), Qt::AlignCenter | Qt::TextWordWrap, m_emptyText);
        return;
    }
    const int rowH = 42;
    const int visibleRows = qMax(1, height() / rowH);
    QFont nameF = font();
    nameF.setPixelSize(12);
    nameF.setBold(true);
    QFont subF = font();
    subF.setPixelSize(10);
    for (int i = 0; i < m_items.size() && i < visibleRows; ++i) {
        const ZoneBarItem& it = m_items[i];
        const double y = i * rowH + 2;
        const QColor c = Theme::levelColor(it.level);

        p.setFont(nameF);
        p.setPen(Theme::text());
        const QString value = it.capacity > 0 ? QStringLiteral("%1 / %2").arg(it.count).arg(it.capacity)
                                              : QString::number(it.count);
        const double vw = QFontMetricsF(nameF).horizontalAdvance(value);
        const QString lvl = densityLevelName(it.level);
        p.setFont(subF);
        const double lw = QFontMetricsF(subF).horizontalAdvance(lvl) + 12;
        p.setFont(nameF);
        p.drawText(QRectF(0, y, width() - vw - lw - 16, 16), Qt::AlignLeft | Qt::AlignVCenter,
                   QFontMetricsF(nameF).elidedText(it.name, Qt::ElideRight, width() - vw - lw - 16));
        p.drawText(QRectF(width() - vw - lw - 8, y, vw, 16), Qt::AlignRight | Qt::AlignVCenter, value);

        QColor pillBg = c;
        pillBg.setAlpha(50);
        const QRectF pill(width() - lw, y + 1, lw, 15);
        p.setPen(Qt::NoPen);
        p.setBrush(pillBg);
        p.drawRoundedRect(pill, 7, 7);
        p.setFont(subF);
        p.setPen(c.lighter(130));
        p.drawText(pill, Qt::AlignCenter, lvl);

        const QRectF track(0, y + 21, width(), 6);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0x1b, 0x2a, 0x44));
        p.drawRoundedRect(track, 3, 3);
        const double frac = it.capacity > 0 ? qMin(1.0, double(it.count) / it.capacity) : (it.count > 0 ? 1.0 : 0.0);
        if (frac > 0) {
            p.setBrush(c);
            p.drawRoundedRect(QRectF(track.left(), track.top(), qMax(6.0, track.width() * frac), track.height()), 3, 3);
        }
        if (!it.subtitle.isEmpty()) {
            p.setPen(Theme::text3());
            p.drawText(QRectF(0, y + 27, width(), 13), Qt::AlignLeft | Qt::AlignVCenter, it.subtitle);
        }
    }
}

// ===========================================================================
// HeatmapView
// ===========================================================================

HeatmapView::HeatmapView(QWidget* parent) : QWidget(parent)
{
    m_emptyText = tr("Heatmap builds up while analysis is running");
}

void HeatmapView::setImages(const QImage& background, const QImage& heat)
{
    m_bg = background;
    m_heat = heat;
    update();
}

void HeatmapView::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    const QRectF area = QRectF(rect()).adjusted(0, 0, 0, -18);
    p.setPen(Qt::NoPen);
    p.setBrush(Theme::videoBg());
    p.drawRoundedRect(area, 8, 8);

    const QSize src = !m_bg.isNull() ? m_bg.size() : (!m_heat.isNull() ? QSize(16, 9) : QSize());
    if (src.isEmpty()) {
        QFont f = font();
        f.setPixelSize(12);
        p.setFont(f);
        p.setPen(Theme::text3());
        p.drawText(area, Qt::AlignCenter | Qt::TextWordWrap, m_emptyText);
        return;
    }
    const QSizeF fit = QSizeF(src).scaled(area.size(), Qt::KeepAspectRatio);
    const QRectF target(area.center().x() - fit.width() / 2, area.center().y() - fit.height() / 2, fit.width(),
                        fit.height());
    if (!m_bg.isNull()) {
        p.drawImage(target, m_bg);
        p.fillRect(target, QColor(5, 10, 20, 120));
    }
    if (!m_heat.isNull())
        p.drawImage(target, m_heat);
    else {
        QFont f = font();
        f.setPixelSize(11);
        p.setFont(f);
        p.setPen(Theme::text2());
        p.drawText(target, Qt::AlignCenter | Qt::TextWordWrap, m_emptyText);
    }

    // Sequential legend
    QFont f = font();
    f.setPixelSize(10);
    p.setFont(f);
    const double lw = qMin(120.0, width() * 0.4);
    const QRectF bar(width() - lw - 34, height() - 12, lw, 6);
    QLinearGradient g(bar.topLeft(), bar.topRight());
    for (int i = 0; i <= 10; ++i)
        g.setColorAt(i / 10.0, ChartUtil::infernoColor(0.15 + 0.85 * i / 10.0));
    p.setBrush(g);
    p.drawRoundedRect(bar, 3, 3);
    p.setPen(Theme::text3());
    p.drawText(QRectF(bar.left() - 34, bar.top() - 5, 30, 16), Qt::AlignRight | Qt::AlignVCenter, tr("Low"));
    p.drawText(QRectF(bar.right() + 4, bar.top() - 5, 30, 16), Qt::AlignLeft | Qt::AlignVCenter, tr("High"));
}
