#include "Theme.h"

#include <QApplication>
#include <QFile>
#include <QHash>
#include <QPainter>
#include <QPalette>
#include <QStyle>
#include <QStyleFactory>
#include <QSvgRenderer>
#include <QWidget>

namespace Theme {

namespace {

// 24x24 line icons, stroke = currentColor (substituted at render time).
const QHash<QString, QString>& iconBodies()
{
    static const QHash<QString, QString> icons = {
        { "live", R"(<rect x="2.5" y="6" width="13" height="12" rx="2.5"/><path d="M15.5 10.5l6-3.5v10l-6-3.5z"/>)" },
        { "analytics", R"(<path d="M3.5 3.5v17h17"/><path d="M7.5 15l4-5 3 3 5-6.5"/>)" },
        { "zones", R"(<path d="M4 8.5V4h4.5M15.5 4H20v4.5M20 15.5V20h-4.5M8.5 20H4v-4.5"/><path d="M9 9h6v6H9z"/>)" },
        { "events", R"(<path d="M6 16.5V11a6 6 0 0 1 12 0v5.5l1.8 1.8H4.2z"/><path d="M10 20.5a2 2 0 0 0 4 0"/>)" },
        { "media", R"(<rect x="3" y="4.5" width="18" height="15" rx="2.5"/><circle cx="9" cy="10" r="1.8"/><path d="M21 16l-5-5-9.5 8.5"/>)" },
        { "camera", R"(<path d="M3 8.2 15.6 5l1.9 6.6L4.9 14.8z"/><path d="M17 8.4l3.8-1v5.2l-3.2.8"/><path d="M8.6 14l1.2 4.2H4.5v2.5"/>)" },
        { "settings", R"(<circle cx="12" cy="12" r="3"/><path d="M12 2.8v2.6M12 18.6v2.6M2.8 12h2.6M18.6 12h2.6M5.5 5.5l1.8 1.8M16.7 16.7l1.8 1.8M5.5 18.5l1.8-1.8M16.7 7.3l1.8-1.8"/><circle cx="12" cy="12" r="6.6"/>)" },
        { "play", R"(<path d="M7.5 4.8v14.4L19 12z" fill="currentColor"/>)" },
        { "stop", R"(<rect x="6" y="6" width="12" height="12" rx="2" fill="currentColor"/>)" },
        { "snapshot", R"(<path d="M4 8h3.2l1.8-2.8h6L16.8 8H20a1 1 0 0 1 1 1v9.5a1 1 0 0 1-1 1H4a1 1 0 0 1-1-1V9a1 1 0 0 1 1-1z"/><circle cx="12" cy="13.2" r="3.4"/>)" },
        { "record", R"(<circle cx="12" cy="12" r="8.2"/><circle cx="12" cy="12" r="4" fill="currentColor"/>)" },
        { "tour", R"(<path d="M20 12a8 8 0 1 1-2.4-5.7"/><path d="M20 4.2v4.3h-4.3"/>)" },
        { "fullscreen", R"(<path d="M4 9V4h5M15 4h5v5M20 15v5h-5M9 20H4v-5"/>)" },
        { "people", R"(<circle cx="9" cy="8" r="3.2"/><path d="M3 20c0-3.5 2.7-6 6-6s6 2.5 6 6"/><circle cx="17" cy="9" r="2.5"/><path d="M16.2 14.1c2.8.2 4.8 2.4 4.8 5.9"/>)" },
        { "in", R"(<path d="M12 20V5M6 11l6-6 6 6"/>)" },
        { "out", R"(<path d="M12 4v15M6 13l6 6 6-6"/>)" },
        { "alert", R"(<path d="M12 3.5 2.5 20h19z"/><path d="M12 10v4.2M12 17.2v.3"/>)" },
        { "shield", R"(<path d="M12 3 4.5 6v6c0 4.8 3.3 7.9 7.5 9 4.2-1.1 7.5-4.2 7.5-9V6z"/><path d="M9 12l2.2 2.2L15.2 10"/>)" },
        { "intrusion", R"(<path d="M12 3 4.5 6v6c0 4.8 3.3 7.9 7.5 9 4.2-1.1 7.5-4.2 7.5-9V6z"/><path d="M12 8.2v5M12 16.2v.3"/>)" },
        { "user", R"(<circle cx="12" cy="8" r="4"/><path d="M4 21c0-4.4 3.6-7 8-7s8 2.6 8 7"/>)" },
        { "clock", R"(<circle cx="12" cy="12" r="8.8"/><path d="M12 7v5l3.2 2"/>)" },
        { "cpu", R"(<rect x="6" y="6" width="12" height="12" rx="1.8"/><rect x="9.5" y="9.5" width="5" height="5" rx=".6"/><path d="M9 2.5V6M15 2.5V6M9 18v3.5M15 18v3.5M2.5 9H6M2.5 15H6M18 9h3.5M18 15h3.5"/>)" },
        { "memory", R"(<rect x="3" y="7" width="18" height="10" rx="1.5"/><path d="M7 7v10M11 7v10M15 7v10M5 17v2.5M19 17v2.5"/>)" },
        { "plus", R"(<path d="M12 5v14M5 12h14"/>)" },
        { "edit", R"(<path d="M4 20h4L19 9l-4-4L4 16z"/><path d="M13.5 6.5l4 4"/>)" },
        { "trash", R"(<path d="M4 7h16M9.5 7V4.5h5V7M6 7l1 13h10l1-13M10 11v5M14 11v5"/>)" },
        { "folder", R"(<path d="M3 6.5a1.5 1.5 0 0 1 1.5-1.5h4.8l2 2.2h8.2A1.5 1.5 0 0 1 21 8.7v9.8a1.5 1.5 0 0 1-1.5 1.5h-15A1.5 1.5 0 0 1 3 18.5z"/>)" },
        { "export", R"(<path d="M12 15V3.5M7.5 8 12 3.5 16.5 8"/><path d="M4.5 13.5v6h15v-6"/>)" },
        { "download", R"(<path d="M12 3.5V15M7.5 10.5 12 15l4.5-4.5"/><path d="M4.5 16.5v3h15v-3"/>)" },
        { "refresh", R"(<path d="M19.5 11A7.6 7.6 0 0 0 6 7.2L4.5 8.8"/><path d="M4.5 4.5v4.3h4.3"/><path d="M4.5 13A7.6 7.6 0 0 0 18 16.8l1.5-1.6"/><path d="M19.5 19.5v-4.3h-4.3"/>)" },
        { "flame", R"(<path d="M12 3c1.2 4 5.5 5.6 5.5 10.5a5.5 5.5 0 0 1-11 0c0-2.8 1.6-4 2.2-5.6 1 1.6 2 2.2 2.6 2.2C11.4 8 11 5.5 12 3z"/>)" },
        { "privacy", R"(<path d="M2.5 12S6 6 12 6s9.5 6 9.5 6-3.5 6-9.5 6-9.5-6-9.5-6z"/><circle cx="12" cy="12" r="2.8"/><path d="M4 4l16 16"/>)" },
        { "eye", R"(<path d="M2.5 12S6 6 12 6s9.5 6 9.5 6-3.5 6-9.5 6-9.5-6-9.5-6z"/><circle cx="12" cy="12" r="2.8"/>)" },
        { "route", R"(<circle cx="6" cy="18" r="2.2"/><circle cx="18" cy="6" r="2.2"/><path d="M8.2 18H15a3 3 0 0 0 0-6H9a3 3 0 0 1 0-6h6.8"/>)" },
        { "bag", R"(<rect x="4" y="8" width="16" height="12" rx="2.2"/><path d="M9 8V5.5h6V8M4 13h16"/>)" },
        { "run", R"(<circle cx="14.5" cy="4.5" r="2"/><path d="M5.5 20.5l3.8-5 3 2.2 1.4-5 2.8 3h3"/><path d="M7.5 9.5l3.5-2.8 3.4 2.2"/>)" },
        { "loiter", R"(<circle cx="12" cy="4.8" r="2"/><path d="M12 8v7M8.2 21l3.8-6 3.8 6M8 11.2h8"/>)" },
        { "lock", R"(<rect x="5" y="11" width="14" height="10" rx="2"/><path d="M8 11V8a4 4 0 0 1 8 0v3"/>)" },
        { "check", R"(<path d="M5 12.5l4.5 4.5L19 7.5"/>)" },
        { "close", R"(<path d="M6 6l12 12M18 6 6 18"/>)" },
        { "chevron-left", R"(<path d="M15 5l-7 7 7 7"/>)" },
        { "chevron-right", R"(<path d="M9 5l7 7-7 7"/>)" },
        { "chevron-down", R"(<path d="M5 9l7 7 7-7"/>)" },
        { "video", R"(<rect x="3" y="5" width="18" height="14" rx="2.5"/><path d="M10 9v6l5-3z" fill="currentColor"/>)" },
        { "image", R"(<rect x="3" y="4.5" width="18" height="15" rx="2.5"/><circle cx="9" cy="10" r="1.8"/><path d="M21 16l-5-5-9.5 8.5"/>)" },
        { "usb", R"(<path d="M12 3v13.5"/><circle cx="12" cy="18.8" r="2.2"/><path d="M12 13l-4-2.8V7.5M12 11l4-2V6"/><rect x="6.5" y="5" width="3" height="2.5"/><circle cx="16" cy="5" r="1.2"/>)" },
        { "ipcam", R"(<rect x="4" y="3.5" width="16" height="10.5" rx="2.5"/><circle cx="12" cy="8.7" r="2.6"/><path d="M12 14v3.5M6.5 20.5h11M12 17.5v3"/>)" },
        { "line", R"(<path d="M5 18.5 19 5.5"/><circle cx="5" cy="18.5" r="2"/><circle cx="19" cy="5.5" r="2"/><path d="M13 14.5l2.2 2.4M16 11.8l2.2 2.4" stroke-width="1.4"/>)" },
        { "polygon", R"(<path d="M5 8l7-4.5 7 5-2.2 11H7.2z"/><circle cx="5" cy="8" r="1.4" fill="currentColor"/><circle cx="12" cy="3.5" r="1.4" fill="currentColor"/><circle cx="19" cy="8.5" r="1.4" fill="currentColor"/>)" },
        { "pointer", R"(<path d="M5.5 3.5l13 7.5-5.8 1.6-3 5.9z"/>)" },
        { "save", R"(<path d="M5 3.5h11l3.5 3.5v13.5h-14.5z"/><path d="M8 3.5v5h7.5v-5M8 20.5v-6.5h8.5v6.5"/>)" },
        { "filter", R"(<path d="M3.5 5h17l-6.8 8v6l-3.4 1.8V13z"/>)" },
        { "search", R"(<circle cx="11" cy="11" r="6.5"/><path d="M16 16l4.5 4.5"/>)" },
        { "pin", R"(<path d="M12 21s-7-6.2-7-11.5a7 7 0 0 1 14 0C19 14.8 12 21 12 21z"/><circle cx="12" cy="9.5" r="2.5"/>)" },
        { "density", R"(<circle cx="6.5" cy="6.5" r="1.7"/><circle cx="12" cy="6.5" r="1.7"/><circle cx="17.5" cy="6.5" r="1.7"/><circle cx="6.5" cy="12" r="1.7"/><circle cx="12" cy="12" r="1.7" fill="currentColor"/><circle cx="17.5" cy="12" r="1.7" fill="currentColor"/><circle cx="6.5" cy="17.5" r="1.7"/><circle cx="12" cy="17.5" r="1.7" fill="currentColor"/><circle cx="17.5" cy="17.5" r="1.7" fill="currentColor"/>)" },
        { "info", R"(<circle cx="12" cy="12" r="8.8"/><path d="M12 11v5.5M12 7.6v.4"/>)" },
        { "sound", R"(<path d="M4 9.2h3.8L13 5v14l-5.2-4.2H4z"/><path d="M16.5 8.8a4.8 4.8 0 0 1 0 6.4M19 6.3a8.4 8.4 0 0 1 0 11.4"/>)" },
        { "flow", R"(<path d="M4 8h13M13.5 4.5 17 8l-3.5 3.5M20 16H7M10.5 12.5 7 16l3.5 3.5"/>)" },
        { "layers", R"(<path d="M12 3.5 3 8.2l9 4.8 9-4.8z"/><path d="M3 12.2l9 4.8 9-4.8M3 16.2l9 4.8 9-4.8"/>)" },
        { "calendar", R"(<rect x="3.5" y="5" width="17" height="15.5" rx="2"/><path d="M3.5 10h17M8 3v4M16 3v4"/>)" },
        { "report", R"(<path d="M6 3.5h8.5L19 8v12.5H6z"/><path d="M14 3.5V8h5M9 17v-3M12 17v-5.5M15 17v-2"/>)" },
        { "open", R"(<path d="M14 4h6v6M20 4l-8.5 8.5"/><path d="M18 14v6H4V6h6"/>)" },
        { "grid", R"(<rect x="4" y="4" width="7" height="7" rx="1"/><rect x="13" y="4" width="7" height="7" rx="1"/><rect x="4" y="13" width="7" height="7" rx="1"/><rect x="13" y="13" width="7" height="7" rx="1"/>)" },
        { "power", R"(<path d="M12 3.5v8"/><path d="M7 6.5a7.5 7.5 0 1 0 10 0"/>)" },
        { "gauge", R"(<path d="M4 17a8 8 0 1 1 16 0"/><path d="M12 17l4-5.5"/><circle cx="12" cy="17" r="1.3" fill="currentColor"/>)" },
    };
    return icons;
}

const char* kLogoSvg = R"SVG(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 48 48">
<defs><linearGradient id="g" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#5b9bff"/><stop offset="1" stop-color="#2553d8"/></linearGradient></defs>
<path d="M24 3 L41 9 V22 C41 33 33.5 41.5 24 45 C14.5 41.5 7 33 7 22 V9 Z" fill="url(#g)"/>
<path d="M11 24 C14.5 17.5 19 14.5 24 14.5 C29 14.5 33.5 17.5 37 24 C33.5 30.5 29 33.5 24 33.5 C19 33.5 14.5 30.5 11 24 Z" fill="#0c1628"/>
<circle cx="24" cy="24" r="6" fill="#5b9bff"/><circle cx="24" cy="24" r="2.6" fill="#e6edf7"/>
<circle cx="36" cy="11.5" r="2.6" fill="#3ddc84" stroke="#0c1628" stroke-width="1.2"/>
</svg>)SVG";

QByteArray svgFor(const QString& name, const QColor& color)
{
    if (name == QLatin1String("logo"))
        return QByteArray(kLogoSvg);
    QString body = iconBodies().value(name);
    if (body.isEmpty())
        body = iconBodies().value(QStringLiteral("info"));
    const QString hex = color.name();
    body.replace(QLatin1String("currentColor"), hex);
    return QStringLiteral("<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"%1\" "
                          "stroke-width=\"1.8\" stroke-linecap=\"round\" stroke-linejoin=\"round\">%2</svg>")
        .arg(hex, body)
        .toUtf8();
}

qreal dpr()
{
    return qApp ? qMax<qreal>(1.0, qApp->devicePixelRatio()) : 1.0;
}

} // namespace

QColor categorical(int index)
{
    static const QColor c[] = { QColor("#3987e5"), QColor("#d95926"), QColor("#199e70"), QColor("#c98500"),
                                QColor("#d55181"), QColor("#008300"), QColor("#9085e9"), QColor("#e66767") };
    return c[((index % 8) + 8) % 8];
}

QColor severityColor(Severity s)
{
    switch (s) {
    case Severity::Critical: return critical();
    case Severity::Warning:  return warning();
    case Severity::Info:     return accent2();
    }
    return text2();
}

QColor levelColor(DensityLevel l)
{
    switch (l) {
    case DensityLevel::Low:      return good();
    case DensityLevel::Moderate: return warning();
    case DensityLevel::High:     return serious();
    case DensityLevel::Critical: return critical();
    }
    return good();
}

QColor statusColor(CameraStatus s)
{
    switch (s) {
    case CameraStatus::Live:
    case CameraStatus::Playing:
    case CameraStatus::Still:        return good();
    case CameraStatus::Connecting:
    case CameraStatus::Reconnecting: return warning();
    case CameraStatus::Offline:      return critical();
    case CameraStatus::Ended:
    case CameraStatus::Stopped:      return text3();
    }
    return text3();
}

QPixmap pixmap(const QString& name, const QColor& color, int size)
{
    static QHash<QString, QPixmap> cache;
    const qreal ratio = dpr();
    const QString key = QStringLiteral("%1|%2|%3|%4").arg(name, color.name(QColor::HexArgb)).arg(size).arg(ratio);
    auto it = cache.constFind(key);
    if (it != cache.constEnd())
        return it.value();

    QPixmap pm(int(size * ratio), int(size * ratio));
    pm.fill(Qt::transparent);
    QSvgRenderer r(svgFor(name, color));
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    r.render(&p);
    p.end();
    pm.setDevicePixelRatio(ratio);
    cache.insert(key, pm);
    return pm;
}

QIcon icon(const QString& name, const QColor& color, const QColor& activeColor)
{
    QIcon ic;
    for (int s : { 16, 20, 24, 32 }) {
        ic.addPixmap(pixmap(name, color, s), QIcon::Normal, QIcon::Off);
        if (activeColor.isValid()) {
            ic.addPixmap(pixmap(name, activeColor, s), QIcon::Normal, QIcon::On);
            ic.addPixmap(pixmap(name, activeColor, s), QIcon::Active, QIcon::On);
        }
        ic.addPixmap(pixmap(name, text3(), s), QIcon::Disabled, QIcon::Off);
    }
    return ic;
}

static QPixmap gridPixmap(int cols, int rows, const QColor& color, int size)
{
    const qreal ratio = dpr();
    QPixmap pm(int(size * ratio), int(size * ratio));
    pm.fill(Qt::transparent);
    pm.setDevicePixelRatio(ratio);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    const qreal margin = size * 0.12;
    const qreal gap = size * 0.07;
    const qreal w = (size - 2 * margin - (cols - 1) * gap) / cols;
    const qreal h = (size - 2 * margin - (rows - 1) * gap) / rows;
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < cols; ++c)
            p.drawRoundedRect(QRectF(margin + c * (w + gap), margin + r * (h + gap), w, h), 1.2, 1.2);
    return pm;
}

QIcon gridIcon(int cols, int rows, const QColor& color, const QColor& activeColor)
{
    QIcon ic;
    for (int s : { 16, 20, 24 }) {
        ic.addPixmap(gridPixmap(cols, rows, color, s), QIcon::Normal, QIcon::Off);
        if (activeColor.isValid())
            ic.addPixmap(gridPixmap(cols, rows, activeColor, s), QIcon::Normal, QIcon::On);
    }
    return ic;
}

void setRole(QWidget* w, const char* role)
{
    w->setProperty("role", QString::fromLatin1(role));
}

void repolish(QWidget* w)
{
    w->style()->unpolish(w);
    w->style()->polish(w);
    w->update();
}

void apply(QApplication& app)
{
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    QPalette pal;
    pal.setColor(QPalette::Window, bg());
    pal.setColor(QPalette::WindowText, text());
    pal.setColor(QPalette::Base, surface2());
    pal.setColor(QPalette::AlternateBase, surface());
    pal.setColor(QPalette::Text, text());
    pal.setColor(QPalette::Button, surface2());
    pal.setColor(QPalette::ButtonText, text());
    pal.setColor(QPalette::Highlight, accent());
    pal.setColor(QPalette::HighlightedText, Qt::white);
    pal.setColor(QPalette::ToolTipBase, surface2());
    pal.setColor(QPalette::ToolTipText, text());
    pal.setColor(QPalette::PlaceholderText, text3());
    pal.setColor(QPalette::Disabled, QPalette::Text, text3());
    pal.setColor(QPalette::Disabled, QPalette::ButtonText, text3());
    pal.setColor(QPalette::Disabled, QPalette::WindowText, text3());
    app.setPalette(pal);

    QFont f(QStringLiteral("Segoe UI"));
    f.setPointSizeF(9.5);
    app.setFont(f);

    QFile qss(QStringLiteral(":/style/dark.qss"));
    if (qss.open(QIODevice::ReadOnly))
        app.setStyleSheet(QString::fromUtf8(qss.readAll()));
}

} // namespace Theme
