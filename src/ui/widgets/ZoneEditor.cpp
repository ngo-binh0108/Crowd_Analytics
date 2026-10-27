#include "ZoneEditor.h"
#include "core/CameraManager.h"
#include "core/Overlay.h"
#include "ui/Theme.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QtMath>

namespace {

double distToSegment(const QPointF& p, const QPointF& a, const QPointF& b)
{
    const QPointF ab = b - a;
    const double len2 = ab.x() * ab.x() + ab.y() * ab.y();
    if (len2 < 1e-9)
        return std::hypot(p.x() - a.x(), p.y() - a.y());
    const double t = qBound(0.0, ((p.x() - a.x()) * ab.x() + (p.y() - a.y()) * ab.y()) / len2, 1.0);
    const QPointF proj = a + ab * t;
    return std::hypot(p.x() - proj.x(), p.y() - proj.y());
}

QPointF clampNorm(const QPointF& p)
{
    return QPointF(qBound(0.0, p.x(), 1.0), qBound(0.0, p.y(), 1.0));
}

} // namespace

ZoneEditor::ZoneEditor(QWidget* parent) : QWidget(parent)
{
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(320, 200);
}

void ZoneEditor::setFrame(const QImage& img)
{
    m_frame = img;
    update();
}

void ZoneEditor::setShapes(const QVector<Zone>& zones, const QVector<CountLine>& lines)
{
    m_zones = zones;
    m_lines = lines;
    cancelDrawing();
    select(KindNone, -1);
    update();
}

void ZoneEditor::setTool(Tool t)
{
    cancelDrawing();
    m_tool = t;
    setCursor(t == Tool::Select ? Qt::ArrowCursor : Qt::CrossCursor);
    emit toolChanged(t);
    update();
}

void ZoneEditor::select(int kind, int index)
{
    m_selKind = kind;
    m_selIndex = index;
    emit selectionChanged(kind, index);
    update();
}

void ZoneEditor::deleteSelected()
{
    if (m_selKind == KindZone && m_selIndex >= 0 && m_selIndex < m_zones.size())
        m_zones.removeAt(m_selIndex);
    else if (m_selKind == KindLine && m_selIndex >= 0 && m_selIndex < m_lines.size())
        m_lines.removeAt(m_selIndex);
    else
        return;
    select(KindNone, -1);
    emit shapesChanged();
}

void ZoneEditor::clearAll()
{
    m_zones.clear();
    m_lines.clear();
    cancelDrawing();
    select(KindNone, -1);
    emit shapesChanged();
}

void ZoneEditor::updateZone(int index, const Zone& z)
{
    if (index < 0 || index >= m_zones.size())
        return;
    m_zones[index] = z;
    emit shapesChanged();
    update();
}

void ZoneEditor::updateLine(int index, const CountLine& l)
{
    if (index < 0 || index >= m_lines.size())
        return;
    m_lines[index] = l;
    emit shapesChanged();
    update();
}

QRectF ZoneEditor::imageRect() const
{
    const QRectF area = QRectF(rect()).adjusted(8, 8, -8, -8);
    const QSizeF src = m_frame.isNull() ? QSizeF(16, 9) : QSizeF(m_frame.size());
    const QSizeF fit = src.scaled(area.size(), Qt::KeepAspectRatio);
    return QRectF(area.center().x() - fit.width() / 2, area.center().y() - fit.height() / 2, fit.width(), fit.height());
}

QPointF ZoneEditor::toNorm(const QPointF& w) const
{
    const QRectF r = imageRect();
    return QPointF((w.x() - r.left()) / r.width(), (w.y() - r.top()) / r.height());
}

QPointF ZoneEditor::toWidget(const QPointF& n) const
{
    const QRectF r = imageRect();
    return QPointF(r.left() + n.x() * r.width(), r.top() + n.y() * r.height());
}

QString ZoneEditor::nextZoneName() const
{
    for (int i = 0; i < 26 * 4; ++i) {
        const QString name = tr("Zone %1").arg(i < 26 ? QString(QChar('A' + i)) : QString::number(i - 25));
        bool used = false;
        for (const Zone& z : m_zones)
            used = used || z.name == name;
        if (!used)
            return name;
    }
    return tr("Zone");
}

void ZoneEditor::cancelDrawing()
{
    m_draft.clear();
    m_lineDraft = false;
    update();
}

void ZoneEditor::finishPolygon()
{
    if (m_draft.size() < 3) {
        cancelDrawing();
        return;
    }
    Zone z;
    z.id = CameraManager::newObjectId();
    z.name = nextZoneName();
    z.polygon = QPolygonF(m_draft);
    z.color = Theme::categorical(m_zones.size());
    m_zones.push_back(z);
    m_draft.clear();
    emit shapesChanged();
    setTool(Tool::Select);
    select(KindZone, m_zones.size() - 1);
}

void ZoneEditor::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), Theme::videoBg());
    const QRectF img = imageRect();
    if (!m_frame.isNull()) {
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.drawImage(img, m_frame);
    } else {
        p.setPen(QPen(Theme::border2(), 1, Qt::DashLine));
        p.setBrush(QColor(0x0d, 0x17, 0x2a));
        p.drawRect(img);
        p.setPen(Theme::text3());
        p.drawText(img, Qt::AlignCenter, tr("No picture from this camera yet.\nYou can still draw zones; use Refresh Frame when it is online."));
    }

    OverlayData od;
    od.zones = &m_zones;
    od.lines = &m_lines;
    OverlayOptions oo;
    oo.showIds = false;
    Overlay::draw(p, img, od, oo);

    // Selection handles
    const QColor handle(Qt::white);
    if (m_selKind == KindZone && m_selIndex >= 0 && m_selIndex < m_zones.size()) {
        const Zone& z = m_zones[m_selIndex];
        QPolygonF poly;
        for (const QPointF& pt : z.polygon)
            poly << toWidget(pt);
        p.setPen(QPen(Theme::accent2(), 2));
        p.setBrush(Qt::NoBrush);
        p.drawPolygon(poly);
        for (const QPointF& pt : poly) {
            p.setPen(QPen(Theme::accent(), 2));
            p.setBrush(handle);
            p.drawRect(QRectF(pt.x() - 5, pt.y() - 5, 10, 10));
        }
    } else if (m_selKind == KindLine && m_selIndex >= 0 && m_selIndex < m_lines.size()) {
        const CountLine& l = m_lines[m_selIndex];
        for (const QPointF& pt : { toWidget(l.a), toWidget(l.b) }) {
            p.setPen(QPen(Theme::accent(), 2));
            p.setBrush(handle);
            p.drawEllipse(pt, 6, 6);
        }
    }

    // Drafts
    if (!m_draft.isEmpty()) {
        QPolygonF poly;
        for (const QPointF& pt : m_draft)
            poly << toWidget(pt);
        p.setPen(QPen(Theme::accent2(), 2, Qt::DashLine));
        p.setBrush(Qt::NoBrush);
        p.drawPolyline(poly);
        p.drawLine(poly.last(), m_cursor);
        const bool nearFirst = poly.size() >= 3 && QLineF(poly.first(), m_cursor).length() < 12;
        for (int i = 0; i < poly.size(); ++i) {
            p.setPen(QPen(Theme::accent(), 2));
            p.setBrush(i == 0 && nearFirst ? Theme::good() : QColor(Qt::white));
            p.drawEllipse(poly[i], i == 0 && nearFirst ? 7 : 4.5, i == 0 && nearFirst ? 7 : 4.5);
        }
    }
    if (m_lineDraft) {
        p.setPen(QPen(Theme::warning(), 2.5, Qt::DashLine, Qt::RoundCap));
        p.drawLine(toWidget(m_lineStart), m_cursor);
        p.setBrush(Theme::warning());
        p.drawEllipse(toWidget(m_lineStart), 5, 5);
    }

    // Tool hint
    QString hint;
    switch (m_tool) {
    case Tool::Select:
        hint = tr("Select: click a zone or line, drag to move, drag handles to reshape, Del to delete");
        break;
    case Tool::Polygon:
        hint = tr("Draw zone: click to add corners, double-click / Enter or click the first corner to finish, "
                  "right-click removes the last corner, Esc cancels");
        break;
    case Tool::Line:
        hint = tr("Draw counting line: click the start and end points (or drag). The arrow points to the IN side.");
        break;
    }
    QFont f = font();
    f.setPixelSize(11);
    p.setFont(f);
    const QRectF hr(img.left() + 8, img.bottom() - 26, img.width() - 16, 20);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(3, 8, 18, 190));
    const double tw = QFontMetricsF(f).horizontalAdvance(hint) + 16;
    p.drawRoundedRect(QRectF(hr.left(), hr.top(), qMin(tw, hr.width()), hr.height()), 6, 6);
    p.setPen(Theme::text());
    p.drawText(hr.adjusted(8, 0, -8, 0), Qt::AlignLeft | Qt::AlignVCenter,
               QFontMetricsF(f).elidedText(hint, Qt::ElideRight, hr.width() - 16));
}

void ZoneEditor::mousePressEvent(QMouseEvent* e)
{
    setFocus();
    const QPointF pos = e->localPos();
    const QPointF n = clampNorm(toNorm(pos));
    m_cursor = pos;

    if (m_tool == Tool::Polygon) {
        if (e->button() == Qt::RightButton) {
            if (!m_draft.isEmpty())
                m_draft.removeLast();
            update();
            return;
        }
        if (e->button() != Qt::LeftButton)
            return;
        if (m_draft.size() >= 3 && QLineF(toWidget(m_draft.first()), pos).length() < 12) {
            finishPolygon();
            return;
        }
        if (m_draft.isEmpty() || QLineF(toWidget(m_draft.last()), pos).length() > 4)
            m_draft.push_back(n);
        update();
        return;
    }

    if (m_tool == Tool::Line) {
        if (e->button() == Qt::RightButton) {
            cancelDrawing();
            return;
        }
        if (e->button() != Qt::LeftButton)
            return;
        if (!m_lineDraft) {
            m_lineDraft = true;
            m_lineStart = n;
        }
        update();
        return;
    }

    // Select tool: handles first, then bodies (topmost = last drawn).
    if (e->button() != Qt::LeftButton)
        return;
    m_drag = Drag::None;
    m_dragLast = n;
    if (m_selKind == KindZone && m_selIndex >= 0 && m_selIndex < m_zones.size()) {
        const QPolygonF& poly = m_zones[m_selIndex].polygon;
        for (int v = 0; v < poly.size(); ++v) {
            if (QLineF(toWidget(poly[v]), pos).length() <= 8) {
                m_drag = Drag::ZoneVertex;
                m_dragIndex = m_selIndex;
                m_dragVertex = v;
                return;
            }
        }
    }
    for (int i = m_lines.size() - 1; i >= 0; --i) {
        const QPointF a = toWidget(m_lines[i].a);
        const QPointF b = toWidget(m_lines[i].b);
        if (QLineF(a, pos).length() <= 9) {
            select(KindLine, i);
            m_drag = Drag::LineEndA;
            m_dragIndex = i;
            return;
        }
        if (QLineF(b, pos).length() <= 9) {
            select(KindLine, i);
            m_drag = Drag::LineEndB;
            m_dragIndex = i;
            return;
        }
        if (distToSegment(pos, a, b) <= 6) {
            select(KindLine, i);
            m_drag = Drag::LineBody;
            m_dragIndex = i;
            return;
        }
    }
    for (int i = m_zones.size() - 1; i >= 0; --i) {
        if (m_zones[i].polygon.containsPoint(n, Qt::OddEvenFill)) {
            select(KindZone, i);
            m_drag = Drag::ZoneBody;
            m_dragIndex = i;
            return;
        }
    }
    select(KindNone, -1);
}

void ZoneEditor::mouseMoveEvent(QMouseEvent* e)
{
    m_cursor = e->localPos();
    const QPointF n = clampNorm(toNorm(m_cursor));
    if (m_drag != Drag::None && (e->buttons() & Qt::LeftButton)) {
        const QPointF d = n - m_dragLast;
        m_dragLast = n;
        switch (m_drag) {
        case Drag::ZoneVertex:
            m_zones[m_dragIndex].polygon[m_dragVertex] = n;
            break;
        case Drag::ZoneBody: {
            QPolygonF& poly = m_zones[m_dragIndex].polygon;
            const QRectF br = poly.boundingRect();
            // Keep the whole polygon inside the frame.
            const double dx = qBound(-br.left(), d.x(), 1.0 - br.right());
            const double dy = qBound(-br.top(), d.y(), 1.0 - br.bottom());
            poly.translate(dx, dy);
            break;
        }
        case Drag::LineEndA:
            m_lines[m_dragIndex].a = n;
            break;
        case Drag::LineEndB:
            m_lines[m_dragIndex].b = n;
            break;
        case Drag::LineBody: {
            CountLine& l = m_lines[m_dragIndex];
            const double dx = qBound(-qMin(l.a.x(), l.b.x()), d.x(), 1.0 - qMax(l.a.x(), l.b.x()));
            const double dy = qBound(-qMin(l.a.y(), l.b.y()), d.y(), 1.0 - qMax(l.a.y(), l.b.y()));
            l.a += QPointF(dx, dy);
            l.b += QPointF(dx, dy);
            break;
        }
        case Drag::None:
            break;
        }
        update();
        return;
    }
    if (m_tool != Tool::Select)
        update();
}

void ZoneEditor::mouseReleaseEvent(QMouseEvent* e)
{
    if (m_tool == Tool::Line && m_lineDraft && e->button() == Qt::LeftButton) {
        const QPointF end = clampNorm(toNorm(e->localPos()));
        if (QLineF(toWidget(m_lineStart), toWidget(end)).length() > 12) {
            CountLine l;
            l.id = CameraManager::newObjectId();
            l.name = tr("Line %1").arg(m_lines.size() + 1);
            l.a = m_lineStart;
            l.b = end;
            l.color = QColor(0xfa, 0xb2, 0x19);
            m_lines.push_back(l);
            m_lineDraft = false;
            emit shapesChanged();
            setTool(Tool::Select);
            select(KindLine, m_lines.size() - 1);
        }
        return;
    }
    if (m_drag != Drag::None) {
        m_drag = Drag::None;
        emit shapesChanged();
    }
}

void ZoneEditor::mouseDoubleClickEvent(QMouseEvent* e)
{
    if (m_tool == Tool::Polygon && e->button() == Qt::LeftButton) {
        // The first click of the double-click already placed the final corner.
        finishPolygon();
    }
}

void ZoneEditor::keyPressEvent(QKeyEvent* e)
{
    switch (e->key()) {
    case Qt::Key_Escape:
        if (!m_draft.isEmpty() || m_lineDraft)
            cancelDrawing();
        else
            setTool(Tool::Select);
        break;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (m_tool == Tool::Polygon)
            finishPolygon();
        break;
    case Qt::Key_Delete:
    case Qt::Key_Backspace:
        if (m_tool == Tool::Polygon && !m_draft.isEmpty()) {
            m_draft.removeLast();
            update();
        } else {
            deleteSelected();
        }
        break;
    default:
        QWidget::keyPressEvent(e);
    }
}
