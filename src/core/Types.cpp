#include "Types.h"

#include <QJsonArray>
#include <QObject>

QString sourceTypeName(SourceType t)
{
    switch (t) {
    case SourceType::Image:     return QObject::tr("Image");
    case SourceType::Video:     return QObject::tr("Video File");
    case SourceType::UsbCamera: return QObject::tr("USB Camera");
    case SourceType::IpCamera:  return QObject::tr("IP Camera");
    }
    return QString();
}

QString zoneTypeName(ZoneType t)
{
    switch (t) {
    case ZoneType::Counting:   return QObject::tr("Counting");
    case ZoneType::Restricted: return QObject::tr("Restricted");
    case ZoneType::Queue:      return QObject::tr("Queue");
    }
    return QString();
}

QString severityName(Severity s)
{
    switch (s) {
    case Severity::Info:     return QObject::tr("Info");
    case Severity::Warning:  return QObject::tr("Warning");
    case Severity::Critical: return QObject::tr("Critical");
    }
    return QString();
}

QString eventTypeName(EventType t)
{
    switch (t) {
    case EventType::System:          return QObject::tr("System");
    case EventType::CameraOnline:    return QObject::tr("Camera Online");
    case EventType::CameraOffline:   return QObject::tr("Camera Offline");
    case EventType::HighDensity:     return QObject::tr("High Density");
    case EventType::Overcrowding:    return QObject::tr("Overcrowding");
    case EventType::QueueAlert:      return QObject::tr("Queue Building");
    case EventType::Intrusion:       return QObject::tr("Intrusion");
    case EventType::Loitering:       return QObject::tr("Loitering");
    case EventType::AbandonedObject: return QObject::tr("Abandoned Object");
    case EventType::WrongWay:        return QObject::tr("Wrong Way");
    case EventType::Running:         return QObject::tr("Running");
    case EventType::Tamper:          return QObject::tr("Camera Tamper");
    case EventType::Snapshot:        return QObject::tr("Snapshot");
    case EventType::Recording:       return QObject::tr("Recording");
    case EventType::Account:         return QObject::tr("User Account");
    case EventType::Count_:          break;
    }
    return QString();
}

QString cameraStatusName(CameraStatus s)
{
    switch (s) {
    case CameraStatus::Stopped:      return QObject::tr("Stopped");
    case CameraStatus::Connecting:   return QObject::tr("Connecting");
    case CameraStatus::Live:         return QObject::tr("Live");
    case CameraStatus::Playing:      return QObject::tr("Playing");
    case CameraStatus::Still:        return QObject::tr("Image");
    case CameraStatus::Ended:        return QObject::tr("Ended");
    case CameraStatus::Reconnecting: return QObject::tr("Reconnecting");
    case CameraStatus::Offline:      return QObject::tr("Offline");
    }
    return QString();
}

QString densityLevelName(DensityLevel l)
{
    switch (l) {
    case DensityLevel::Low:      return QObject::tr("Low");
    case DensityLevel::Moderate: return QObject::tr("Moderate");
    case DensityLevel::High:     return QObject::tr("High");
    case DensityLevel::Critical: return QObject::tr("Critical");
    }
    return QString();
}

DensityLevel densityLevelFor(int count, int capacity, int warnPct, int critPct)
{
    if (capacity <= 0)
        return DensityLevel::Low;
    const double pct = 100.0 * count / capacity;
    if (pct >= critPct)
        return DensityLevel::Critical;
    if (pct >= warnPct)
        return DensityLevel::High;
    if (pct >= warnPct * 0.6)
        return DensityLevel::Moderate;
    return DensityLevel::Low;
}

// ---------------------------------------------------------------------------
// JSON helpers
// ---------------------------------------------------------------------------

static QJsonArray pointToJson(const QPointF& p)
{
    return QJsonArray{ p.x(), p.y() };
}

static QPointF pointFromJson(const QJsonValue& v)
{
    const QJsonArray a = v.toArray();
    if (a.size() < 2)
        return QPointF();
    return QPointF(a.at(0).toDouble(), a.at(1).toDouble());
}

QJsonObject Zone::toJson() const
{
    QJsonObject o;
    o["id"] = id;
    o["name"] = name;
    o["type"] = int(type);
    QJsonArray pts;
    for (const QPointF& p : polygon)
        pts.append(pointToJson(p));
    o["polygon"] = pts;
    o["capacity"] = capacity;
    o["areaSqm"] = areaSqm;
    o["loiterSec"] = loiterSec;
    o["color"] = color.name();
    o["includeInTotals"] = includeInTotals;
    o["enabled"] = enabled;
    return o;
}

Zone Zone::fromJson(const QJsonObject& o)
{
    Zone z;
    z.id = o["id"].toString();
    z.name = o["name"].toString();
    z.type = ZoneType(qBound(0, o["type"].toInt(), 2));
    for (const QJsonValue& v : o["polygon"].toArray())
        z.polygon.append(pointFromJson(v));
    z.capacity = o["capacity"].toInt(50);
    z.areaSqm = o["areaSqm"].toDouble(0.0);
    z.loiterSec = o["loiterSec"].toInt(0);
    z.color = QColor(o["color"].toString("#3987e5"));
    z.includeInTotals = o["includeInTotals"].toBool(true);
    z.enabled = o["enabled"].toBool(true);
    return z;
}

QJsonObject CountLine::toJson() const
{
    QJsonObject o;
    o["id"] = id;
    o["name"] = name;
    o["a"] = pointToJson(a);
    o["b"] = pointToJson(b);
    o["invert"] = invert;
    o["alertOnOut"] = alertOnOut;
    o["includeInTotals"] = includeInTotals;
    o["color"] = color.name();
    o["enabled"] = enabled;
    return o;
}

CountLine CountLine::fromJson(const QJsonObject& o)
{
    CountLine l;
    l.id = o["id"].toString();
    l.name = o["name"].toString();
    l.a = pointFromJson(o["a"]);
    l.b = pointFromJson(o["b"]);
    l.invert = o["invert"].toBool(false);
    l.alertOnOut = o["alertOnOut"].toBool(false);
    l.includeInTotals = o["includeInTotals"].toBool(true);
    l.color = QColor(o["color"].toString("#fab219"));
    l.enabled = o["enabled"].toBool(true);
    return l;
}

QString CameraConfig::sourceDescription() const
{
    switch (type) {
    case SourceType::UsbCamera:
        return QObject::tr("USB device #%1").arg(usbIndex);
    case SourceType::IpCamera: {
        // Hide credentials embedded in the URL.
        QString s = source;
        const int at = s.indexOf('@');
        const int scheme = s.indexOf("://");
        if (at > 0 && scheme > 0 && at > scheme)
            s = s.left(scheme + 3) + QStringLiteral("***@") + s.mid(at + 1);
        return s;
    }
    default:
        return source;
    }
}

QJsonObject CameraConfig::toJson() const
{
    QJsonObject o;
    o["id"] = id;
    o["name"] = name;
    o["location"] = location;
    o["type"] = int(type);
    o["source"] = source;
    o["usbIndex"] = usbIndex;
    o["reqWidth"] = reqWidth;
    o["reqHeight"] = reqHeight;
    o["loop"] = loop;
    o["analysisEnabled"] = analysisEnabled;
    o["capacity"] = capacity;
    QJsonArray zs;
    for (const Zone& z : zones)
        zs.append(z.toJson());
    o["zones"] = zs;
    QJsonArray ls;
    for (const CountLine& l : lines)
        ls.append(l.toJson());
    o["lines"] = ls;
    return o;
}

CameraConfig CameraConfig::fromJson(const QJsonObject& o)
{
    CameraConfig c;
    c.id = o["id"].toInt();
    c.name = o["name"].toString();
    c.location = o["location"].toString();
    c.type = SourceType(qBound(0, o["type"].toInt(1), 3));
    c.source = o["source"].toString();
    c.usbIndex = o["usbIndex"].toInt(0);
    c.reqWidth = o["reqWidth"].toInt(0);
    c.reqHeight = o["reqHeight"].toInt(0);
    c.loop = o["loop"].toBool(true);
    c.analysisEnabled = o["analysisEnabled"].toBool(true);
    c.capacity = o["capacity"].toInt(0);
    for (const QJsonValue& v : o["zones"].toArray())
        c.zones.append(Zone::fromJson(v.toObject()));
    for (const QJsonValue& v : o["lines"].toArray())
        c.lines.append(CountLine::fromJson(v.toObject()));
    return c;
}

void registerMetaTypes()
{
    qRegisterMetaType<AnalysisResult>("AnalysisResult");
    qRegisterMetaType<EventRecord>("EventRecord");
    qRegisterMetaType<CameraStatus>("CameraStatus");
}
