#include "Database.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

Database& Database::instance()
{
    static Database db;
    return db;
}

void Database::close()
{
    if (m_db.isOpen())
        m_db.close();
    m_db = QSqlDatabase();
    QSqlDatabase::removeDatabase(QStringLiteral("main"));
}

bool Database::isOpen() const
{
    return m_db.isOpen();
}

bool Database::open(const QString& path, QString* error)
{
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("main"));
    m_db.setDatabaseName(path);
    if (!m_db.open()) {
        if (error)
            *error = m_db.lastError().text();
        return false;
    }
    QSqlQuery q(m_db);
    q.exec(QStringLiteral("PRAGMA journal_mode=WAL"));
    q.exec(QStringLiteral("PRAGMA synchronous=NORMAL"));
    const char* schema[] = {
        "CREATE TABLE IF NOT EXISTS events ("
        " id INTEGER PRIMARY KEY AUTOINCREMENT, ts INTEGER NOT NULL, camera_id INTEGER, camera_name TEXT,"
        " type INTEGER, severity INTEGER, zone TEXT, message TEXT, snapshot TEXT, ack INTEGER DEFAULT 0)",
        "CREATE INDEX IF NOT EXISTS idx_events_ts ON events(ts)",
        "CREATE TABLE IF NOT EXISTS flow ("
        " minute INTEGER NOT NULL, camera_id INTEGER NOT NULL, target_id TEXT NOT NULL,"
        " is_line INTEGER, in_totals INTEGER, in_count INTEGER, out_count INTEGER,"
        " occ_max INTEGER, occ_avg REAL, dwell_sum REAL, dwell_cnt INTEGER,"
        " PRIMARY KEY (minute, camera_id, target_id))",
        "CREATE INDEX IF NOT EXISTS idx_flow_minute ON flow(minute)",
    };
    for (const char* sql : schema) {
        if (!q.exec(QString::fromLatin1(sql))) {
            if (error)
                *error = q.lastError().text();
            return false;
        }
    }
    return true;
}

qint64 Database::insertEvent(const EventRecord& e)
{
    if (!m_db.isOpen())
        return 0;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("INSERT INTO events (ts, camera_id, camera_name, type, severity, zone, message, snapshot, ack)"
                             " VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    q.addBindValue(e.time.toMSecsSinceEpoch());
    q.addBindValue(e.cameraId);
    q.addBindValue(e.cameraName);
    q.addBindValue(int(e.type));
    q.addBindValue(int(e.severity));
    q.addBindValue(e.zoneName);
    q.addBindValue(e.message);
    q.addBindValue(e.snapshotPath);
    q.addBindValue(e.acknowledged ? 1 : 0);
    if (!q.exec())
        return 0;
    return q.lastInsertId().toLongLong();
}

QVector<EventRecord> Database::queryEvents(const EventFilter& f, int limit) const
{
    QVector<EventRecord> out;
    if (!m_db.isOpen())
        return out;
    QString sql = QStringLiteral("SELECT id, ts, camera_id, camera_name, type, severity, zone, message, snapshot, ack "
                                 "FROM events WHERE 1=1");
    QVariantList binds;
    if (f.from.isValid()) {
        sql += QStringLiteral(" AND ts >= ?");
        binds << f.from.toMSecsSinceEpoch();
    }
    if (f.to.isValid()) {
        sql += QStringLiteral(" AND ts < ?");
        binds << f.to.toMSecsSinceEpoch();
    }
    if (f.severity >= 0) {
        sql += QStringLiteral(" AND severity = ?");
        binds << f.severity;
    }
    if (f.type >= 0) {
        sql += QStringLiteral(" AND type = ?");
        binds << f.type;
    }
    if (f.cameraId >= 0) {
        sql += QStringLiteral(" AND camera_id = ?");
        binds << f.cameraId;
    }
    if (!f.text.trimmed().isEmpty()) {
        sql += QStringLiteral(" AND (message LIKE ? OR zone LIKE ? OR camera_name LIKE ?)");
        const QString like = QLatin1Char('%') + f.text.trimmed() + QLatin1Char('%');
        binds << like << like << like;
    }
    if (f.unacknowledgedOnly)
        sql += QStringLiteral(" AND ack = 0");
    sql += QStringLiteral(" ORDER BY ts DESC LIMIT %1").arg(limit);

    QSqlQuery q(m_db);
    q.prepare(sql);
    for (const QVariant& v : binds)
        q.addBindValue(v);
    if (!q.exec())
        return out;
    while (q.next()) {
        EventRecord e;
        e.id = q.value(0).toLongLong();
        e.time = QDateTime::fromMSecsSinceEpoch(q.value(1).toLongLong());
        e.cameraId = q.value(2).toInt();
        e.cameraName = q.value(3).toString();
        e.type = EventType(q.value(4).toInt());
        e.severity = Severity(q.value(5).toInt());
        e.zoneName = q.value(6).toString();
        e.message = q.value(7).toString();
        e.snapshotPath = q.value(8).toString();
        e.acknowledged = q.value(9).toInt() != 0;
        out.push_back(e);
    }
    return out;
}

EventCounts Database::eventCounts(const QDateTime& since) const
{
    EventCounts c;
    if (!m_db.isOpen())
        return c;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT severity, COUNT(*), SUM(CASE WHEN ack = 0 THEN 1 ELSE 0 END) "
                             "FROM events WHERE ts >= ? GROUP BY severity"));
    q.addBindValue(since.toMSecsSinceEpoch());
    if (!q.exec())
        return c;
    while (q.next()) {
        const int n = q.value(1).toInt();
        switch (Severity(q.value(0).toInt())) {
        case Severity::Critical: c.critical = n; break;
        case Severity::Warning:  c.warning = n; break;
        case Severity::Info:     c.info = n; break;
        }
        // Informational events do not need operator attention.
        if (Severity(q.value(0).toInt()) != Severity::Info)
            c.unacknowledged += q.value(2).toInt();
    }
    return c;
}

void Database::acknowledge(qint64 id)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("UPDATE events SET ack = 1 WHERE id = ?"));
    q.addBindValue(id);
    q.exec();
}

void Database::acknowledgeAll()
{
    QSqlQuery q(m_db);
    q.exec(QStringLiteral("UPDATE events SET ack = 1 WHERE ack = 0"));
}

int Database::deleteEventsBefore(const QDateTime& t)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("DELETE FROM events WHERE ts < ?"));
    q.addBindValue(t.toMSecsSinceEpoch());
    q.exec();
    return q.numRowsAffected();
}

void Database::upsertFlow(const QVector<FlowRow>& rows)
{
    if (!m_db.isOpen() || rows.isEmpty())
        return;
    m_db.transaction();
    QSqlQuery q(m_db);
    // Rows carry increments since the previous flush, so nothing already
    // written for a minute is lost after a counter reset or a restart.
    q.prepare(QStringLiteral(
        "INSERT INTO flow (minute, camera_id, target_id, is_line, in_totals, in_count, out_count, occ_max, occ_avg,"
        " dwell_sum, dwell_cnt) VALUES (?,?,?,?,?,?,?,?,?,?,?)"
        " ON CONFLICT(minute, camera_id, target_id) DO UPDATE SET"
        " is_line = excluded.is_line, in_totals = excluded.in_totals,"
        " in_count = in_count + excluded.in_count, out_count = out_count + excluded.out_count,"
        " occ_max = MAX(occ_max, excluded.occ_max), occ_avg = excluded.occ_avg,"
        " dwell_sum = dwell_sum + excluded.dwell_sum, dwell_cnt = dwell_cnt + excluded.dwell_cnt"));
    for (const FlowRow& r : rows) {
        q.addBindValue(r.minute);
        q.addBindValue(r.cameraId);
        q.addBindValue(r.targetId);
        q.addBindValue(r.isLine ? 1 : 0);
        q.addBindValue(r.inTotals ? 1 : 0);
        q.addBindValue(r.in);
        q.addBindValue(r.out);
        q.addBindValue(r.occMax);
        q.addBindValue(r.occAvg);
        q.addBindValue(r.dwellSum);
        q.addBindValue(r.dwellCount);
        q.exec();
    }
    m_db.commit();
}

QVector<MinuteRow> Database::flowPerMinute(qint64 fromMinute, qint64 toMinute, int cameraId) const
{
    QVector<MinuteRow> out;
    if (!m_db.isOpen())
        return out;
    QString sql = QStringLiteral(
        "SELECT minute,"
        " SUM(CASE WHEN target_id <> '__view__' AND in_totals = 1 THEN in_count ELSE 0 END),"
        " SUM(CASE WHEN target_id <> '__view__' AND in_totals = 1 THEN out_count ELSE 0 END),"
        " SUM(CASE WHEN target_id = '__view__' THEN occ_avg ELSE 0 END),"
        " SUM(CASE WHEN target_id = '__view__' THEN occ_max ELSE 0 END)"
        " FROM flow WHERE minute >= ? AND minute < ?");
    if (cameraId >= 0)
        sql += QStringLiteral(" AND camera_id = ?");
    sql += QStringLiteral(" GROUP BY minute ORDER BY minute");
    QSqlQuery q(m_db);
    q.prepare(sql);
    q.addBindValue(fromMinute);
    q.addBindValue(toMinute);
    if (cameraId >= 0)
        q.addBindValue(cameraId);
    if (!q.exec())
        return out;
    while (q.next()) {
        MinuteRow r;
        r.minute = q.value(0).toLongLong();
        r.in = q.value(1).toLongLong();
        r.out = q.value(2).toLongLong();
        r.people = q.value(3).toDouble();
        r.peopleMax = q.value(4).toInt();
        out.push_back(r);
    }
    return out;
}

QVector<TargetTotal> Database::targetTotals(qint64 fromMinute, int cameraId) const
{
    QVector<TargetTotal> out;
    if (!m_db.isOpen())
        return out;
    QString sql = QStringLiteral(
        "SELECT camera_id, target_id, MAX(is_line), MAX(in_totals), SUM(in_count), SUM(out_count), MAX(occ_max),"
        " SUM(dwell_sum), SUM(dwell_cnt) FROM flow WHERE minute >= ?");
    if (cameraId >= 0)
        sql += QStringLiteral(" AND camera_id = ?");
    sql += QStringLiteral(" GROUP BY camera_id, target_id");
    QSqlQuery q(m_db);
    q.prepare(sql);
    q.addBindValue(fromMinute);
    if (cameraId >= 0)
        q.addBindValue(cameraId);
    if (!q.exec())
        return out;
    while (q.next()) {
        TargetTotal t;
        t.cameraId = q.value(0).toInt();
        t.targetId = q.value(1).toString();
        t.isLine = q.value(2).toInt() != 0;
        t.inTotals = q.value(3).toInt() != 0;
        t.in = q.value(4).toLongLong();
        t.out = q.value(5).toLongLong();
        t.occMax = q.value(6).toInt();
        t.dwellSum = q.value(7).toDouble();
        t.dwellCount = q.value(8).toInt();
        out.push_back(t);
    }
    return out;
}

QPair<qint64, int> Database::peakPeople(qint64 fromMinute) const
{
    if (!m_db.isOpen())
        return { 0, 0 };
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT minute, SUM(occ_max) AS p FROM flow WHERE target_id = '__view__' AND minute >= ?"
                             " GROUP BY minute ORDER BY p DESC LIMIT 1"));
    q.addBindValue(fromMinute);
    if (q.exec() && q.next())
        return { q.value(0).toLongLong(), q.value(1).toInt() };
    return { 0, 0 };
}

void Database::purgeOlderThan(int days)
{
    if (!m_db.isOpen() || days <= 0)
        return;
    const QDateTime cutoff = QDateTime::currentDateTime().addDays(-days);
    deleteEventsBefore(cutoff);
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("DELETE FROM flow WHERE minute < ?"));
    q.addBindValue(minuteOf(cutoff));
    q.exec();
}
