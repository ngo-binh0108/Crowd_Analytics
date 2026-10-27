#pragma once

#include <QColor>
#include <QDateTime>
#include <QJsonObject>
#include <QMetaType>
#include <QPointF>
#include <QPolygonF>
#include <QRectF>
#include <QString>
#include <QVector>

// ---------------------------------------------------------------------------
// Shared data types used by the capture/analysis threads and the UI.
// All geometry exchanged with the UI is normalized to [0,1] relative to the
// source frame so it is independent of resolution and display scaling.
// ---------------------------------------------------------------------------

enum class SourceType { Image = 0, Video = 1, UsbCamera = 2, IpCamera = 3 };

enum class ZoneType { Counting = 0, Restricted = 1, Queue = 2 };

enum class Severity { Info = 0, Warning = 1, Critical = 2 };

enum class EventType {
    System = 0,
    CameraOnline,
    CameraOffline,
    HighDensity,
    Overcrowding,
    QueueAlert,
    Intrusion,
    Loitering,
    AbandonedObject,
    WrongWay,
    Running,
    Tamper,
    Snapshot,
    Recording,
    Account,        // sign-in / sign-out / user management (audit trail)
    Count_
};

enum class CameraStatus { Stopped = 0, Connecting, Live, Playing, Still, Ended, Reconnecting, Offline };

enum class DensityLevel { Low = 0, Moderate = 1, High = 2, Critical = 3 };

// Object classes produced by the detector (mapped from the model's classes).
enum ObjectClass { ClassPerson = 0, ClassBag = 1 };

// Flags attached to tracked objects for rendering.
enum ObjectFlag : quint32 {
    FlagNone       = 0,
    FlagIntrusion  = 1u << 0,
    FlagLoitering  = 1u << 1,
    FlagRunning    = 1u << 2,
    FlagAbandoned  = 1u << 3,
    FlagInCrowd    = 1u << 4,
};

QString sourceTypeName(SourceType t);
QString zoneTypeName(ZoneType t);
QString severityName(Severity s);
QString eventTypeName(EventType t);
QString cameraStatusName(CameraStatus s);
QString densityLevelName(DensityLevel l);
DensityLevel densityLevelFor(int count, int capacity, int warnPct, int critPct);

struct Zone {
    QString id;
    QString name;
    ZoneType type = ZoneType::Counting;
    QPolygonF polygon;        // normalized coordinates
    int capacity = 50;        // people (queue length for Queue zones)
    double areaSqm = 0.0;     // optional, enables people/m2
    int loiterSec = 0;        // 0 = loitering rule disabled
    QColor color;
    bool includeInTotals = true;
    bool enabled = true;

    QJsonObject toJson() const;
    static Zone fromJson(const QJsonObject& o);
};

struct CountLine {
    QString id;
    QString name;
    QPointF a;                // normalized
    QPointF b;                // normalized
    bool invert = false;      // swap the In / Out direction
    bool alertOnOut = false;  // one-way line: crossing in the Out direction raises "Wrong way"
    bool includeInTotals = true;
    QColor color;
    bool enabled = true;

    QJsonObject toJson() const;
    static CountLine fromJson(const QJsonObject& o);
};

struct CameraConfig {
    int id = 0;
    QString name;
    QString location;
    SourceType type = SourceType::Video;
    QString source;           // file path or URL
    int usbIndex = 0;
    int reqWidth = 0;         // requested capture size for USB cameras (0 = default)
    int reqHeight = 0;
    bool loop = true;         // loop video files
    bool analysisEnabled = true;
    int capacity = 0;         // whole-view capacity (0 = not used)
    QVector<Zone> zones;
    QVector<CountLine> lines;

    QString sourceDescription() const;
    QJsonObject toJson() const;
    static CameraConfig fromJson(const QJsonObject& o);
};

struct TrackedObject {
    int id = 0;
    int cls = ClassPerson;
    QRectF box;               // normalized
    float score = 0.f;
    quint32 flags = FlagNone;
    float dwellSec = 0.f;
    QVector<QPointF> trail;   // normalized foot points (oldest first)
};

struct ZoneStat {
    QString zoneId;
    QString name;
    ZoneType type = ZoneType::Counting;
    int count = 0;            // people currently inside
    int capacity = 0;
    qint64 in = 0;            // cumulative since the engine was reset
    qint64 out = 0;
    DensityLevel level = DensityLevel::Low;
    double densityPerSqm = 0.0;
    bool inTotals = true;     // this zone's in/out count toward the site totals
};

struct LineStat {
    QString lineId;
    QString name;
    qint64 in = 0;
    qint64 out = 0;
    bool inTotals = true;
};

// Per-step deltas used by the analytics store.
struct FlowDelta {
    QString targetId;
    bool isLine = false;
    bool inTotals = true;
    int in = 0;
    int out = 0;
};

struct DwellSample {
    QString zoneId;
    double seconds = 0.0;
};

struct EventRecord {
    qint64 id = 0;
    QDateTime time;
    int cameraId = -1;
    QString cameraName;
    EventType type = EventType::System;
    Severity severity = Severity::Info;
    QString zoneName;
    QString message;
    QString snapshotPath;
    bool acknowledged = false;
};

struct AnalysisResult {
    int cameraId = -1;
    qint64 timestampMs = 0;
    double inferenceMs = 0.0;
    int personCount = 0;
    DensityLevel viewLevel = DensityLevel::Low;   // whole-view crowd level (camera capacity)
    QVector<TrackedObject> objects;
    QVector<ZoneStat> zones;
    QVector<LineStat> lines;
    QVector<FlowDelta> deltas;
    QVector<DwellSample> dwell;
    QVector<EventRecord> events;
};

Q_DECLARE_METATYPE(AnalysisResult)
Q_DECLARE_METATYPE(EventRecord)
Q_DECLARE_METATYPE(CameraStatus)

void registerMetaTypes();
