#pragma once

#include "Types.h"

#include <QDateTime>
#include <QSqlDatabase>
#include <QString>
#include <QVector>

struct EventFilter {
    QDateTime from;
    QDateTime to;
    int severity = -1;     // -1 = any
    int type = -1;         // -1 = any
    int cameraId = -1;     // -1 = any
    QString text;
    bool unacknowledgedOnly = false;
};

struct EventCounts {
    int critical = 0;
    int warning = 0;
    int info = 0;
    int unacknowledged = 0;
};

// One aggregated row per (minute, camera, zone/line). The pseudo target
// "__view__" stores the number of people visible in the whole camera view.
// upsertFlow() adds in/out/dwell to an existing row (values are increments).
struct FlowRow {
    qint64 minute = 0;     // minutes since epoch (UTC)
    int cameraId = 0;
    QString targetId;
    bool isLine = false;
    bool inTotals = true;
    int in = 0;
    int out = 0;
    int occMax = 0;
    double occAvg = 0.0;
    double dwellSum = 0.0;
    int dwellCount = 0;
};

struct MinuteRow {
    qint64 minute = 0;
    qint64 in = 0;
    qint64 out = 0;
    double people = 0.0;   // average people in view (summed over cameras)
    int peopleMax = 0;
};

struct TargetTotal {
    int cameraId = 0;
    QString targetId;
    bool isLine = false;
    bool inTotals = true;
    qint64 in = 0;
    qint64 out = 0;
    int occMax = 0;
    double dwellSum = 0.0;
    int dwellCount = 0;
};

class Database {
public:
    static Database& instance();

    bool open(const QString& path, QString* error);
    bool isOpen() const;
    void close();

    // Events
    qint64 insertEvent(const EventRecord& e);
    QVector<EventRecord> queryEvents(const EventFilter& f, int limit = 2000) const;
    EventCounts eventCounts(const QDateTime& since) const;
    void acknowledge(qint64 id);
    void acknowledgeAll();
    int deleteEventsBefore(const QDateTime& t);

    // People flow
    void upsertFlow(const QVector<FlowRow>& rows);
    QVector<MinuteRow> flowPerMinute(qint64 fromMinute, qint64 toMinute, int cameraId) const;
    QVector<TargetTotal> targetTotals(qint64 fromMinute, int cameraId) const;
    QPair<qint64, int> peakPeople(qint64 fromMinute) const;   // (minute, people)

    void purgeOlderThan(int days);

    static qint64 minuteOf(const QDateTime& t) { return t.toSecsSinceEpoch() / 60; }
    static QDateTime timeOfMinute(qint64 m) { return QDateTime::fromSecsSinceEpoch(m * 60); }

private:
    Database() = default;
    QSqlDatabase m_db;
};
