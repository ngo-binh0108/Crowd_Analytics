#include "AnalyticsStore.h"
#include "Settings.h"

#include <cmath>

static const QString kViewTarget = QStringLiteral("__view__");

AnalyticsStore::AnalyticsStore(QObject* parent) : QObject(parent)
{
    m_flushTimer.setInterval(5000);
    connect(&m_flushTimer, &QTimer::timeout, this, [this] {
        checkDayRollover();
        flush();
    });
    m_flushTimer.start();

    m_notifyTimer.setSingleShot(true);
    m_notifyTimer.setInterval(400);
    connect(&m_notifyTimer, &QTimer::timeout, this, &AnalyticsStore::updated);
}

QDateTime AnalyticsStore::countingSince() const
{
    const QDateTime midnight(QDate::currentDate(), QTime(0, 0));
    const qint64 reset = AppSettings::instance().countersResetEpoch;
    if (reset > 0) {
        const QDateTime r = QDateTime::fromSecsSinceEpoch(reset);
        if (r > midnight)
            return r;
    }
    return midnight;
}

void AnalyticsStore::loadToday()
{
    m_targets.clear();
    m_day = QDate::currentDate();
    const qint64 fromMin = Database::minuteOf(countingSince());
    for (const TargetTotal& t : Database::instance().targetTotals(fromMin, -1)) {
        if (t.targetId == kViewTarget)
            continue;
        TargetLive& tl = m_targets[key(t.cameraId, t.targetId)];
        tl.cameraId = t.cameraId;
        tl.targetId = t.targetId;
        tl.isLine = t.isLine;
        tl.inTotals = t.inTotals;
        tl.in = t.in;
        tl.out = t.out;
        tl.peak = t.occMax;
        tl.dwellSum = t.dwellSum;
        tl.dwellCount = t.dwellCount;
    }
    const QPair<qint64, int> peak = Database::instance().peakPeople(fromMin);
    m_peakPeople = peak.second;
    m_peakTime = peak.first > 0 ? Database::timeOfMinute(peak.first) : QDateTime();
    emit updated();
}

AnalyticsStore::Acc& AnalyticsStore::acc(qint64 minute, int cameraId, const QString& id, bool isLine, bool inTotals)
{
    const QString k = QString::number(minute) + QLatin1Char('|') + key(cameraId, id);
    auto it = m_acc.find(k);
    if (it == m_acc.end()) {
        Acc a;
        a.minute = minute;
        a.cameraId = cameraId;
        a.targetId = id;
        a.isLine = isLine;
        a.inTotals = inTotals;
        it = m_acc.insert(k, a);
    }
    return it.value();
}

void AnalyticsStore::onResult(const AnalysisResult& r)
{
    checkDayRollover();
    const QDateTime now = QDateTime::currentDateTime();
    const qint64 minute = Database::minuteOf(now);
    const int cam = r.cameraId;

    CameraLive& cl = m_cameras[cam];
    cl.personCount = r.personCount;
    cl.lastUpdateMs = now.toMSecsSinceEpoch();
    cl.inferenceMs = r.inferenceMs;
    cl.viewLevel = r.viewLevel;
    cl.active = true;

    Acc& view = acc(minute, cam, kViewTarget, false, false);
    view.occMax = qMax(view.occMax, r.personCount);
    view.occSum += r.personCount;
    view.occN++;

    for (const ZoneStat& z : r.zones) {
        TargetLive& t = m_targets[key(cam, z.zoneId)];
        t.cameraId = cam;
        t.targetId = z.zoneId;
        t.isLine = false;
        t.inTotals = z.inTotals;
        t.current = z.count;
        t.peak = qMax(t.peak, z.count);
        t.level = z.level;
        Acc& a = acc(minute, cam, z.zoneId, false, t.inTotals);
        a.inTotals = t.inTotals;
        a.occMax = qMax(a.occMax, z.count);
        a.occSum += z.count;
        a.occN++;
    }
    for (const LineStat& l : r.lines) {
        TargetLive& t = m_targets[key(cam, l.lineId)];
        t.cameraId = cam;
        t.targetId = l.lineId;
        t.isLine = true;
        t.inTotals = l.inTotals;
    }
    for (const FlowDelta& d : r.deltas) {
        TargetLive& t = m_targets[key(cam, d.targetId)];
        t.cameraId = cam;
        t.targetId = d.targetId;
        t.isLine = d.isLine;
        t.inTotals = d.inTotals;
        t.in += d.in;
        t.out += d.out;
        Acc& a = acc(minute, cam, d.targetId, d.isLine, d.inTotals);
        a.inTotals = d.inTotals;
        a.in += d.in;
        a.out += d.out;
    }
    for (const DwellSample& s : r.dwell) {
        TargetLive& t = m_targets[key(cam, s.zoneId)];
        t.dwellSum += s.seconds;
        t.dwellCount++;
        Acc& a = acc(minute, cam, s.zoneId, false, t.inTotals);
        a.dwellSum += s.seconds;
        a.dwellCount++;
    }

    const int people = peopleInView();
    if (people > m_peakPeople) {
        m_peakPeople = people;
        m_peakTime = now;
    }
    scheduleNotify();
}

void AnalyticsStore::setCameraInactive(int cameraId)
{
    auto it = m_cameras.find(cameraId);
    if (it != m_cameras.end()) {
        it->active = false;
        it->personCount = 0;
        it->viewLevel = DensityLevel::Low;
    }
    for (TargetLive& t : m_targets)
        if (t.cameraId == cameraId) {
            t.current = 0;
            t.level = DensityLevel::Low;
        }
    scheduleNotify();
}

void AnalyticsStore::removeCamera(int cameraId)
{
    m_cameras.remove(cameraId);
    for (auto it = m_targets.begin(); it != m_targets.end();) {
        if (it->cameraId == cameraId)
            it = m_targets.erase(it);
        else
            ++it;
    }
    scheduleNotify();
}

void AnalyticsStore::resetCounters()
{
    flush();
    m_acc.clear();
    for (TargetLive& t : m_targets) {
        t.in = t.out = 0;
        t.peak = t.current;
        t.dwellSum = 0;
        t.dwellCount = 0;
    }
    m_peakPeople = peopleInView();
    m_peakTime = QDateTime::currentDateTime();
    AppSettings::instance().countersResetEpoch = QDateTime::currentSecsSinceEpoch();
    AppSettings::instance().save();
    emit updated();
}

void AnalyticsStore::checkDayRollover()
{
    const QDate today = QDate::currentDate();
    if (m_day.isValid() && today == m_day)
        return;
    const bool rolled = m_day.isValid();
    m_day = today;
    if (!rolled)
        return;
    flush();
    for (TargetLive& t : m_targets) {
        t.in = t.out = 0;
        t.peak = t.current;
        t.dwellSum = 0;
        t.dwellCount = 0;
    }
    m_peakPeople = 0;
    m_peakTime = QDateTime();
    emit dayChanged();
    emit updated();
}

void AnalyticsStore::flush()
{
    if (m_acc.isEmpty())
        return;
    const qint64 currentMinute = Database::minuteOf(QDateTime::currentDateTime());
    QVector<FlowRow> rows;
    rows.reserve(m_acc.size());
    for (auto it = m_acc.begin(); it != m_acc.end();) {
        Acc& a = it.value();
        FlowRow r;
        r.minute = a.minute;
        r.cameraId = a.cameraId;
        r.targetId = a.targetId;
        r.isLine = a.isLine;
        r.inTotals = a.inTotals;
        r.in = a.in;
        r.out = a.out;
        r.occMax = a.occMax;
        r.occAvg = a.occN > 0 ? a.occSum / a.occN : 0.0;
        r.dwellSum = a.dwellSum;
        r.dwellCount = a.dwellCount;
        rows.push_back(r);
        // Counts are written as increments: clear what has been flushed.
        // Occupancy samples stay so the minute's average/max remain complete.
        a.in = a.out = 0;
        a.dwellSum = 0.0;
        a.dwellCount = 0;
        // Finished minutes are final once written.
        if (a.minute < currentMinute)
            it = m_acc.erase(it);
        else
            ++it;
    }
    Database::instance().upsertFlow(rows);
}

void AnalyticsStore::scheduleNotify()
{
    if (!m_notifyTimer.isActive())
        m_notifyTimer.start();
}

qint64 AnalyticsStore::totalIn(int cameraId) const
{
    qint64 n = 0;
    for (const TargetLive& t : m_targets)
        if (t.inTotals && (cameraId < 0 || t.cameraId == cameraId))
            n += t.in;
    return n;
}

qint64 AnalyticsStore::totalOut(int cameraId) const
{
    qint64 n = 0;
    for (const TargetLive& t : m_targets)
        if (t.inTotals && (cameraId < 0 || t.cameraId == cameraId))
            n += t.out;
    return n;
}

int AnalyticsStore::occupancy() const
{
    return int(qMax<qint64>(0, totalIn() - totalOut()));
}

int AnalyticsStore::peopleInView(int cameraId) const
{
    int n = 0;
    for (auto it = m_cameras.cbegin(); it != m_cameras.cend(); ++it)
        if (it->active && (cameraId < 0 || it.key() == cameraId))
            n += it->personCount;
    return n;
}

int AnalyticsStore::activeCameras() const
{
    int n = 0;
    for (const CameraLive& c : m_cameras)
        n += c.active ? 1 : 0;
    return n;
}

double AnalyticsStore::avgDwellSec(int cameraId) const
{
    double sum = 0;
    int cnt = 0;
    for (const TargetLive& t : m_targets)
        if (!t.isLine && (cameraId < 0 || t.cameraId == cameraId)) {
            sum += t.dwellSum;
            cnt += t.dwellCount;
        }
    return cnt > 0 ? sum / cnt : 0.0;
}

QVector<TargetLive> AnalyticsStore::targets(int cameraId) const
{
    QVector<TargetLive> out;
    for (const TargetLive& t : m_targets)
        if (cameraId < 0 || t.cameraId == cameraId)
            out.push_back(t);
    return out;
}

TargetLive AnalyticsStore::target(int cameraId, const QString& targetId) const
{
    return m_targets.value(key(cameraId, targetId));
}

QVector<MinuteRow> AnalyticsStore::series(const QDateTime& from, const QDateTime& to, int bucketMinutes, int cameraId)
{
    flush();
    bucketMinutes = qMax(1, bucketMinutes);
    const qint64 fromSec = from.toSecsSinceEpoch();
    const qint64 span = qMax<qint64>(1, to.toSecsSinceEpoch() - fromSec);
    const int buckets = int(std::ceil(double(span) / (bucketMinutes * 60.0)));
    QVector<MinuteRow> out(buckets);
    QVector<int> minutesWithData(buckets, 0);
    for (int i = 0; i < buckets; ++i)
        out[i].minute = (fromSec / 60) + qint64(i) * bucketMinutes;

    const QVector<MinuteRow> rows =
        Database::instance().flowPerMinute(Database::minuteOf(from), Database::minuteOf(to) + 1, cameraId);
    for (const MinuteRow& r : rows) {
        const qint64 idx = (r.minute * 60 - fromSec) / (qint64(bucketMinutes) * 60);
        if (idx < 0 || idx >= buckets)
            continue;
        MinuteRow& b = out[int(idx)];
        b.in += r.in;
        b.out += r.out;
        b.people += r.people;
        b.peopleMax = qMax(b.peopleMax, r.peopleMax);
        minutesWithData[int(idx)]++;
    }
    for (int i = 0; i < buckets; ++i)
        if (minutesWithData[i] > 0)
            out[i].people /= minutesWithData[i];
    return out;
}

QVector<double> AnalyticsStore::hourlyIn(const QDateTime& from, const QDateTime& to, int cameraId)
{
    flush();
    QVector<double> hours(24, 0.0);
    const QVector<MinuteRow> rows =
        Database::instance().flowPerMinute(Database::minuteOf(from), Database::minuteOf(to) + 1, cameraId);
    for (const MinuteRow& r : rows) {
        const int h = Database::timeOfMinute(r.minute).toLocalTime().time().hour();
        hours[h] += double(r.in);
    }
    return hours;
}
