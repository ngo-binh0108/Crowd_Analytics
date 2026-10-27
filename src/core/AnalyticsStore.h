#pragma once

#include "Database.h"
#include "Types.h"

#include <QDate>
#include <QHash>
#include <QObject>
#include <QTimer>

// Today's live counters for one zone or counting line.
struct TargetLive {
    int cameraId = 0;
    QString targetId;
    bool isLine = false;
    bool inTotals = true;
    qint64 in = 0;
    qint64 out = 0;
    int current = 0;
    int peak = 0;
    double dwellSum = 0.0;
    int dwellCount = 0;
    DensityLevel level = DensityLevel::Low;

    double avgDwell() const { return dwellCount > 0 ? dwellSum / dwellCount : 0.0; }
};

struct CameraLive {
    int personCount = 0;
    qint64 lastUpdateMs = 0;
    double inferenceMs = 0.0;
    DensityLevel viewLevel = DensityLevel::Low;
    bool active = false;
};

// Aggregates analysis results into "today" counters (since midnight or the
// last manual reset) and per-minute history rows stored in SQLite.
class AnalyticsStore : public QObject {
    Q_OBJECT
public:
    explicit AnalyticsStore(QObject* parent = nullptr);

    void loadToday();
    void onResult(const AnalysisResult& r);
    void setCameraInactive(int cameraId);
    void removeCamera(int cameraId);
    void resetCounters();
    void flush();

    qint64 totalIn(int cameraId = -1) const;
    qint64 totalOut(int cameraId = -1) const;
    int occupancy() const;                 // max(0, in - out) for the site
    int peopleInView(int cameraId = -1) const;
    int peakPeople() const { return m_peakPeople; }
    QDateTime peakTime() const { return m_peakTime; }
    double avgDwellSec(int cameraId = -1) const;
    int activeCameras() const;

    QVector<TargetLive> targets(int cameraId = -1) const;
    TargetLive target(int cameraId, const QString& targetId) const;
    CameraLive camera(int cameraId) const { return m_cameras.value(cameraId); }

    // History (flushes pending rows first). Buckets start at `from`.
    QVector<MinuteRow> series(const QDateTime& from, const QDateTime& to, int bucketMinutes, int cameraId);
    QVector<double> hourlyIn(const QDateTime& from, const QDateTime& to, int cameraId);

    QDateTime countingSince() const;

signals:
    void updated();
    void dayChanged();

private:
    struct Acc {
        qint64 minute = 0;
        int cameraId = 0;
        QString targetId;
        bool isLine = false;
        bool inTotals = true;
        int in = 0;
        int out = 0;
        int occMax = 0;
        double occSum = 0.0;
        int occN = 0;
        double dwellSum = 0.0;
        int dwellCount = 0;
    };

    static QString key(int cameraId, const QString& id) { return QString::number(cameraId) + QLatin1Char('|') + id; }
    Acc& acc(qint64 minute, int cameraId, const QString& id, bool isLine, bool inTotals);
    void scheduleNotify();
    void checkDayRollover();

    QHash<QString, TargetLive> m_targets;
    QHash<int, CameraLive> m_cameras;
    QHash<QString, Acc> m_acc;             // key: minute|camera|target
    int m_peakPeople = 0;
    QDateTime m_peakTime;
    QDate m_day;

    QTimer m_flushTimer;
    QTimer m_notifyTimer;
};
