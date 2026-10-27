#include "VideoTile.h"
#include "ui/Theme.h"

#include <QContextMenuEvent>
#include <QDateTime>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QLinearGradient>
#include <QMimeData>
#include <QPainter>
#include <QPainterPath>
#include <QTime>

namespace {

bool isMediaFile(const QString& path)
{
    static const QStringList exts = { "mp4", "avi", "mkv", "mov", "wmv", "m4v", "mpg", "mpeg", "ts", "webm", "flv",
                                      "jpg", "jpeg", "png", "bmp", "tif", "tiff", "webp" };
    return exts.contains(QFileInfo(path).suffix().toLower());
}

QRectF drawPill(QPainter& p, const QPointF& topLeft, const QString& text, const QColor& bg, const QColor& fg,
                double h, bool alignRight = false)
{
    const QFontMetricsF fm(p.font());
    const double w = fm.horizontalAdvance(text) + h * 0.9;
    QRectF r(topLeft, QSizeF(w, h));
    if (alignRight)
        r.moveRight(topLeft.x());
    p.setPen(Qt::NoPen);
    p.setBrush(bg);
    p.drawRoundedRect(r, h / 2 - 1, h / 2 - 1);
    p.setPen(fg);
    p.drawText(r, Qt::AlignCenter, text);
    return r;
}

} // namespace

VideoTile::VideoTile(int index, QWidget* parent) : QWidget(parent), m_index(index)
{
    setAcceptDrops(true);
    setMouseTracking(true);
    setMinimumSize(120, 80);
    setAttribute(Qt::WA_OpaquePaintEvent);
}

void VideoTile::setCamera(int cameraId, const CameraConfig* cfg)
{
    const bool changed = cameraId != m_cameraId;
    m_cameraId = cameraId;
    if (cfg) {
        m_name = cfg->name;
        m_type = cfg->type;
        m_zones = cfg->zones;
        m_lines = cfg->lines;
    } else {
        m_name.clear();
        m_zones.clear();
        m_lines.clear();
    }
    if (changed) {
        m_frame = QImage();
        m_heat = QImage();
        m_hasResult = false;
        m_result = AnalysisResult();
        m_alertUntilMs = 0;
        m_recording = false;
        m_in = m_out = 0;
    }
    setCursor(cameraId < 0 && m_canAdd ? Qt::PointingHandCursor : Qt::ArrowCursor);
    setToolTip(cameraId < 0 && m_canAdd ? tr("Click to add a camera, or drop a video/image file here") : QString());
    update();
}

void VideoTile::setCanAdd(bool on)
{
    m_canAdd = on;
    setAcceptDrops(on);
    if (m_cameraId < 0) {
        setCursor(on ? Qt::PointingHandCursor : Qt::ArrowCursor);
        setToolTip(on ? tr("Click to add a camera, or drop a video/image file here") : QString());
    }
    update();
}

void VideoTile::setFrame(const QImage& img)
{
    m_frame = img;
    update();
}

void VideoTile::setStatus(CameraStatus s, const QString& message)
{
    m_status = s;
    m_statusMsg = message;
    update();
}

void VideoTile::setResult(const AnalysisResult& r)
{
    m_result = r;
    m_hasResult = true;
    update();
}

void VideoTile::clearResult()
{
    m_hasResult = false;
    m_result = AnalysisResult();
    m_heat = QImage();
    update();
}

void VideoTile::setAnalyzing(bool on)
{
    if (m_analyzing == on)
        return;
    m_analyzing = on;
    if (!on)
        clearResult();
    update();
}

void VideoTile::setRecording(bool on)
{
    m_recording = on;
    update();
}

void VideoTile::setSelected(bool on)
{
    m_selected = on;
    update();
}

void VideoTile::setHeatmap(const QImage& heat)
{
    m_heat = heat;
    update();
}

void VideoTile::setOverlayOptions(const OverlayOptions& o)
{
    m_opts = o;
    update();
}

void VideoTile::setFlowTotals(qint64 in, qint64 out)
{
    m_in = in;
    m_out = out;
}

void VideoTile::raiseAlert(const QString& text, Severity severity)
{
    m_alertText = text;
    m_alertSeverity = severity;
    m_alertUntilMs = QDateTime::currentMSecsSinceEpoch() + 6000;
    update();
}

bool VideoTile::alertActive() const
{
    return m_alertUntilMs > QDateTime::currentMSecsSinceEpoch();
}

QRectF VideoTile::imageRect() const
{
    const QRectF area = QRectF(rect()).adjusted(1, 1, -1, -1);
    if (m_frame.isNull())
        return area;
    const QSizeF fit = QSizeF(m_frame.size()).scaled(area.size(), Qt::KeepAspectRatio);
    return QRectF(area.center().x() - fit.width() / 2, area.center().y() - fit.height() / 2, fit.width(), fit.height());
}

void VideoTile::paintEmpty(QPainter& p, const QRectF& r)
{
    p.fillRect(rect(), Theme::bg());
    const QRectF box = r.adjusted(3, 3, -3, -3);
    p.setPen(QPen(m_hover || m_dragOver ? Theme::accent() : Theme::border2(), 1.2, Qt::DashLine));
    p.setBrush(m_hover || m_dragOver ? QColor(0x14, 0x22, 0x3a) : QColor(0x0d, 0x17, 0x2a));
    p.drawRoundedRect(box, 8, 8);

    if (!m_canAdd) {
        QFont f = font();
        f.setPixelSize(11);
        p.setFont(f);
        p.setPen(Theme::text3());
        p.drawText(box, Qt::AlignCenter, tr("No camera"));
        return;
    }
    const int iconSize = qBound(18, int(box.height() / 5), 36);
    const QPixmap plus = Theme::pixmap(QStringLiteral("plus"), m_hover ? Theme::accent2() : Theme::text3(), iconSize);
    const bool roomy = box.height() > 110 && box.width() > 170;
    const double blockH = iconSize + (roomy ? 40 : 18);
    double y = box.center().y() - blockH / 2;
    p.drawPixmap(QPointF(box.center().x() - iconSize / 2.0, y), plus);
    y += iconSize + 6;
    QFont f = font();
    f.setPixelSize(12);
    f.setBold(true);
    p.setFont(f);
    p.setPen(m_hover ? Theme::text() : Theme::text2());
    p.drawText(QRectF(box.left(), y, box.width(), 16), Qt::AlignCenter, tr("Add camera"));
    if (roomy) {
        f.setPixelSize(10);
        f.setBold(false);
        p.setFont(f);
        p.setPen(Theme::text3());
        p.drawText(QRectF(box.left(), y + 18, box.width(), 14), Qt::AlignCenter, tr("or drop a video / image file"));
    }
    QFont n = font();
    n.setPixelSize(10);
    p.setFont(n);
    p.setPen(Theme::text3());
    p.drawText(box.adjusted(8, 6, -8, -6), Qt::AlignLeft | Qt::AlignTop, QStringLiteral("%1").arg(m_index + 1, 2, 10, QLatin1Char('0')));
}

void VideoTile::paintHeader(QPainter& p, const QRectF& r)
{
    const double h = qBound(20.0, r.height() * 0.08, 28.0);
    QLinearGradient g(r.topLeft(), QPointF(r.left(), r.top() + h + 8));
    g.setColorAt(0, QColor(3, 8, 18, 215));
    g.setColorAt(1, QColor(3, 8, 18, 0));
    p.fillRect(QRectF(r.left(), r.top(), r.width(), h + 8), g);

    QFont f = font();
    f.setPixelSize(int(h * 0.46));
    f.setBold(true);
    p.setFont(f);

    // Right side badges (drawn right-to-left).
    double x = r.right() - 6;
    const double bh = h * 0.68;
    const double by = r.top() + (h - bh) / 2;
    QFont bf = f;
    bf.setPixelSize(int(bh * 0.62));
    p.setFont(bf);

    const bool online = m_status == CameraStatus::Live || m_status == CameraStatus::Playing || m_status == CameraStatus::Still;
    QString st;
    switch (m_status) {
    case CameraStatus::Live:         st = tr("LIVE"); break;
    case CameraStatus::Playing:      st = tr("VIDEO"); break;
    case CameraStatus::Still:        st = tr("IMAGE"); break;
    case CameraStatus::Connecting:   st = tr("CONNECTING"); break;
    case CameraStatus::Reconnecting: st = tr("RECONNECTING"); break;
    case CameraStatus::Offline:      st = tr("OFFLINE"); break;
    case CameraStatus::Ended:        st = tr("ENDED"); break;
    case CameraStatus::Stopped:      st = tr("STOPPED"); break;
    }
    const QColor sc = Theme::statusColor(m_status);
    {
        const QFontMetricsF fm(bf);
        const double w = fm.horizontalAdvance(st) + bh * 1.3;
        const QRectF br(x - w, by, w, bh);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 120));
        p.drawRoundedRect(br, bh / 2, bh / 2);
        p.setBrush(sc);
        p.drawEllipse(QPointF(br.left() + bh * 0.5, br.center().y()), bh * 0.18, bh * 0.18);
        p.setPen(online ? QColor(Qt::white) : sc.lighter(120));
        p.drawText(br.adjusted(bh * 0.8, 0, -bh * 0.35, 0), Qt::AlignCenter, st);
        x = br.left() - 4;
    }
    if (m_recording) {
        const bool blink = QTime::currentTime().msec() < 600;
        const QRectF br = drawPill(p, QPointF(x, by), tr("REC"), QColor(0xd0, 0x3b, 0x3b, blink ? 230 : 150),
                                   Qt::white, bh, true);
        x = br.left() - 4;
    }
    if (m_analyzing) {
        const QRectF br = drawPill(p, QPointF(x, by), tr("AI"), Theme::accent(), Qt::white, bh, true);
        x = br.left() - 4;
    }

    // Title
    p.setFont(f);
    p.setPen(Qt::white);
    const QString title = QStringLiteral("CAM %1  %2").arg(m_cameraId, 2, 10, QLatin1Char('0')).arg(m_name);
    const QFontMetricsF fm(f);
    const double avail = x - r.left() - 10;
    p.drawText(QRectF(r.left() + 8, r.top(), avail, h), Qt::AlignLeft | Qt::AlignVCenter,
               fm.elidedText(title, Qt::ElideRight, avail));
}

void VideoTile::paintChips(QPainter& p, const QRectF& img)
{
    if (!m_analyzing || !m_hasResult)
        return;
    const double h = qBound(16.0, img.height() * 0.065, 24.0);
    QFont f = font();
    f.setPixelSize(int(h * 0.55));
    f.setBold(true);
    p.setFont(f);
    double x = img.left() + 6;
    const double y = img.bottom() - h - 6;
    const double maxX = img.right() - 6;
    const QColor dark(0x08, 0x10, 0x1e);

    auto chip = [&](const QString& text, const QColor& bg, const QColor& fg) {
        const double w = QFontMetricsF(f).horizontalAdvance(text) + h * 0.9;
        if (x + w > maxX)
            return false;
        drawPill(p, QPointF(x, y), text, bg, fg, h);
        x += w + 4;
        return true;
    };

    QColor totalBg(0x0c, 0x16, 0x28, 220);
    chip(tr("People: %1").arg(m_result.personCount), totalBg, Qt::white);
    if (m_result.viewLevel >= DensityLevel::Moderate) {
        const QColor c = Theme::levelColor(m_result.viewLevel);
        chip(tr("Crowd: %1").arg(densityLevelName(m_result.viewLevel)), c, dark);
    }
    if (m_in > 0 || m_out > 0)
        chip(tr("IN %1 / OUT %2").arg(m_in).arg(m_out), totalBg, Qt::white);
    if (width() < 260)
        return;
    for (const ZoneStat& z : m_result.zones) {
        QColor bg = z.type == ZoneType::Restricted ? Overlay::alertColor() : Theme::levelColor(z.level);
        for (const Zone& cz : m_zones)
            if (cz.id == z.zoneId && z.level == DensityLevel::Low && z.type != ZoneType::Restricted)
                bg = cz.color;
        bg.setAlpha(225);
        if (!chip(QStringLiteral("%1: %2").arg(z.name).arg(z.count), bg, dark))
            break;
    }
}

void VideoTile::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = QRectF(rect());

    if (m_cameraId < 0) {
        paintEmpty(p, r);
        return;
    }

    p.fillRect(rect(), Theme::bg());
    QPainterPath clip;
    clip.addRoundedRect(r.adjusted(1, 1, -1, -1), 7, 7);
    p.setClipPath(clip);
    p.fillRect(r, Theme::videoBg());

    const QRectF img = imageRect();
    if (!m_frame.isNull()) {
        p.setRenderHint(QPainter::SmoothPixmapTransform, img.width() < m_frame.width() * 1.5);
        p.drawImage(img, m_frame);

        if (!m_heat.isNull()) {
            p.setRenderHint(QPainter::SmoothPixmapTransform, true);
            p.setOpacity(0.8);
            p.drawImage(img, m_heat);
            p.setOpacity(1.0);
        }

        OverlayData od;
        od.zones = &m_zones;
        od.lines = &m_lines;
        if (m_analyzing && m_hasResult) {
            od.objects = &m_result.objects;
            od.zoneStats = &m_result.zones;
            od.lineStats = &m_result.lines;
        }
        OverlayOptions o = m_opts;
        o.compact = o.compact || img.width() < 300;
        Overlay::draw(p, img, od, o);
    }

    // Centered status message when there is no picture or the source failed.
    const bool online = m_status == CameraStatus::Live || m_status == CameraStatus::Playing || m_status == CameraStatus::Still;
    if (m_frame.isNull() || !online) {
        if (!m_frame.isNull())
            p.fillRect(img, QColor(5, 10, 20, 170));
        const QString icon = m_status == CameraStatus::Offline ? QStringLiteral("alert") : QStringLiteral("camera");
        const QColor ic = m_status == CameraStatus::Offline ? Theme::critical() : Theme::text3();
        const int is = qBound(18, int(r.height() / 6), 34);
        p.drawPixmap(QPointF(r.center().x() - is / 2.0, r.center().y() - is - 4), Theme::pixmap(icon, ic, is));
        QFont f = font();
        f.setPixelSize(12);
        f.setBold(true);
        p.setFont(f);
        p.setPen(Theme::text2());
        QString msg = cameraStatusName(m_status);
        if (m_status == CameraStatus::Connecting || m_status == CameraStatus::Reconnecting)
            msg += QStringLiteral("...");
        p.drawText(QRectF(r.left() + 8, r.center().y() + 2, r.width() - 16, 16), Qt::AlignCenter, msg);
        if (!m_statusMsg.isEmpty() && r.height() > 120) {
            f.setPixelSize(10);
            f.setBold(false);
            p.setFont(f);
            p.setPen(Theme::text3());
            p.drawText(QRectF(r.left() + 8, r.center().y() + 20, r.width() - 16, 30),
                       Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, m_statusMsg);
        }
    }

    paintHeader(p, r);
    paintChips(p, img.intersected(r));

    if (alertActive()) {
        const double h = qBound(18.0, r.height() * 0.075, 26.0);
        QFont f = font();
        f.setPixelSize(int(h * 0.52));
        f.setBold(true);
        p.setFont(f);
        const QColor c = m_alertSeverity == Severity::Critical ? Theme::critical() : QColor(0xc9, 0x85, 0x00);
        const QString text = QStringLiteral("!  ") + m_alertText;
        const double top = r.top() + qBound(20.0, r.height() * 0.08, 28.0) + 4;
        drawPill(p, QPointF(r.right() - 6, top), text, c, Qt::white, h, true);
    }

    p.setClipping(false);
    QColor borderColor = Theme::border();
    double bw = 1.0;
    if (alertActive()) {
        borderColor = m_alertSeverity == Severity::Critical ? Theme::critical() : Theme::warning();
        bw = 2.0;
    } else if (m_selected) {
        borderColor = Theme::accent();
        bw = 2.0;
    } else if (m_hover) {
        borderColor = Theme::border2();
    }
    p.setPen(QPen(borderColor, bw));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(r.adjusted(bw / 2, bw / 2, -bw / 2, -bw / 2), 7, 7);
}

void VideoTile::mousePressEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton) {
        if (m_cameraId < 0) {
            if (m_canAdd)
                emit addRequested(m_index);
        }
        else
            emit clicked(m_index);
    }
    QWidget::mousePressEvent(e);
}

void VideoTile::mouseDoubleClickEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton && m_cameraId >= 0)
        emit doubleClicked(m_index);
}

void VideoTile::contextMenuEvent(QContextMenuEvent* e)
{
    if (m_cameraId >= 0) {
        emit clicked(m_index);
        emit contextMenuRequested(m_index, e->globalPos());
    }
}

void VideoTile::enterEvent(QEvent*)
{
    m_hover = true;
    update();
}

void VideoTile::leaveEvent(QEvent*)
{
    m_hover = false;
    update();
}

void VideoTile::dragEnterEvent(QDragEnterEvent* e)
{
    if (e->mimeData()->hasUrls()) {
        for (const QUrl& u : e->mimeData()->urls()) {
            if (u.isLocalFile() && isMediaFile(u.toLocalFile())) {
                e->acceptProposedAction();
                m_dragOver = true;
                update();
                return;
            }
        }
    }
    e->ignore();
}

void VideoTile::dragLeaveEvent(QDragLeaveEvent*)
{
    m_dragOver = false;
    update();
}

void VideoTile::dropEvent(QDropEvent* e)
{
    m_dragOver = false;
    for (const QUrl& u : e->mimeData()->urls()) {
        if (u.isLocalFile() && isMediaFile(u.toLocalFile()))
            emit fileDropped(m_index, u.toLocalFile());
    }
    e->acceptProposedAction();
    update();
}
