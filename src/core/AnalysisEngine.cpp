#include "AnalysisEngine.h"
#include "Detector.h"
#include "Overlay.h"

#include <QDir>
#include <QElapsedTimer>
#include <QMutexLocker>
#include <QObject>
#include <QPainter>

#include <cmath>

#include <opencv2/imgproc.hpp>

namespace {

constexpr int kHeatW = 96;
constexpr int kHeatH = 54;

// Signed distance of p from the line through a->b, measured in frame heights
// (x is scaled by the aspect ratio so the metric is isotropic). Positive on
// the side the line normal points to. `t` is the projection along a->b
// (0 at a, 1 at b).
double signedDistance(const CountLine& l, const cv::Point2f& p, float aspect, double& t)
{
    const double dx = (l.b.x() - l.a.x()) * aspect;
    const double dy = l.b.y() - l.a.y();
    const double px = (p.x - l.a.x()) * aspect;
    const double py = p.y - l.a.y();
    const double len2 = dx * dx + dy * dy;
    if (len2 < 1e-12) {
        t = -1.0;
        return 0.0;
    }
    t = (px * dx + py * dy) / len2;
    return (dx * py - dy * px) / std::sqrt(len2);
}

QImage matToImage(const cv::Mat& bgr)
{
    if (bgr.empty())
        return QImage();
    const QImage view(bgr.data, bgr.cols, bgr.rows, int(bgr.step), QImage::Format_BGR888);
    return view.convertToFormat(QImage::Format_RGB32);
}

} // namespace

AnalysisEngine::AnalysisEngine(int cameraId)
    : m_cameraId(cameraId),
      m_zones(std::make_shared<ZoneSet>()),
      m_persons(1500, 2, 24),
      m_bags(6000, 3, 4),
      m_heat(kHeatH, kHeatW, CV_32F, cv::Scalar(0))
{
}

void AnalysisEngine::setCameraInfo(const QString& name, int capacity)
{
    QMutexLocker lock(&m_cfgMutex);
    m_cameraName = name;
    m_cameraCapacity = capacity;
}

void AnalysisEngine::setZones(const QVector<Zone>& zones, const QVector<CountLine>& lines)
{
    auto zs = std::make_shared<ZoneSet>();
    zs->zones = zones;
    zs->lines = lines;
    QMutexLocker lock(&m_cfgMutex);
    m_zones = zs;
}

void AnalysisEngine::setParams(const AnalysisParams& params)
{
    QMutexLocker lock(&m_cfgMutex);
    m_params = params;
}

void AnalysisEngine::requestCounterReset()
{
    m_resetRequested = true;
}

void AnalysisEngine::resetHeatmap()
{
    QMutexLocker lock(&m_heatMutex);
    m_heat.setTo(cv::Scalar(0));
}

void AnalysisEngine::addEvent(AnalysisResult& r, EventType type, Severity sev, const QString& zone,
                              const QString& msg) const
{
    EventRecord e;
    e.time = QDateTime::currentDateTime();
    e.cameraId = m_cameraId;
    {
        QMutexLocker lock(&m_cfgMutex);
        e.cameraName = m_cameraName;
    }
    e.type = type;
    e.severity = sev;
    e.zoneName = zone;
    e.message = msg;
    r.events.push_back(e);
}

void AnalysisEngine::addHeat(const cv::Point2f& foot)
{
    const int gx = qBound(0, int(foot.x * kHeatW), kHeatW - 1);
    const int gy = qBound(0, int(foot.y * kHeatH), kHeatH - 1);
    QMutexLocker lock(&m_heatMutex);
    for (int dy = -2; dy <= 2; ++dy) {
        for (int dx = -2; dx <= 2; ++dx) {
            const int x = gx + dx;
            const int y = gy + dy;
            if (x < 0 || y < 0 || x >= kHeatW || y >= kHeatH)
                continue;
            m_heat.at<float>(y, x) += float(std::exp(-(dx * dx + dy * dy) / 2.5));
        }
    }
}

QImage AnalysisEngine::heatmapImage() const
{
    cv::Mat heat;
    {
        QMutexLocker lock(&m_heatMutex);
        heat = m_heat.clone();
    }
    double maxVal = 0;
    cv::minMaxLoc(heat, nullptr, &maxVal);
    if (maxVal <= 0)
        return QImage();
    cv::Mat blurred, norm8, color;
    cv::GaussianBlur(heat, blurred, cv::Size(0, 0), 1.6);
    cv::minMaxLoc(blurred, nullptr, &maxVal);
    blurred.convertTo(norm8, CV_8U, 255.0 / std::max(maxVal, 1e-6));
    cv::applyColorMap(norm8, color, cv::COLORMAP_INFERNO);

    QImage img(kHeatW, kHeatH, QImage::Format_ARGB32);
    for (int y = 0; y < kHeatH; ++y) {
        const uchar* v = norm8.ptr<uchar>(y);
        const cv::Vec3b* c = color.ptr<cv::Vec3b>(y);
        QRgb* dst = reinterpret_cast<QRgb*>(img.scanLine(y));
        for (int x = 0; x < kHeatW; ++x) {
            const int a = v[x] < 8 ? 0 : qMin(200, 40 + v[x] * 3 / 4);
            dst[x] = qRgba(c[x][2], c[x][1], c[x][0], a);
        }
    }
    return img;
}

void AnalysisEngine::checkDensity(const QString& key, const QString& zoneName, ZoneType type, int count,
                                  int capacity, const AnalysisParams& p, qint64 nowMs, AnalysisResult& r,
                                  DensityLevel& levelOut)
{
    levelOut = densityLevelFor(count, capacity, p.densityWarnPct, p.densityCritPct);
    if (!p.fnDensity || capacity <= 0)
        return;
    ZoneAlertState& st = m_zoneAlerts[key];
    if (levelOut >= DensityLevel::High) {
        st.lastHighMs = nowMs;
        const bool escalated = levelOut > st.alertedLevel;
        const bool reminder = levelOut == st.alertedLevel && nowMs - st.lastAlertMs > qint64(p.alertCooldownSec) * 4000;
        if (escalated || reminder) {
            st.alertedLevel = levelOut;
            st.lastAlertMs = nowMs;
            const int pct = int(std::lround(100.0 * count / capacity));
            EventType et = levelOut == DensityLevel::Critical ? EventType::Overcrowding : EventType::HighDensity;
            QString msg = QObject::tr("%1 at %2% of capacity (%3/%4 people)").arg(zoneName).arg(pct).arg(count).arg(capacity);
            if (type == ZoneType::Queue) {
                et = EventType::QueueAlert;
                msg = QObject::tr("Queue in %1 is building up: %2 people waiting (limit %3)")
                          .arg(zoneName).arg(count).arg(capacity);
            }
            addEvent(r, et, levelOut == DensityLevel::Critical ? Severity::Critical : Severity::Warning, zoneName, msg);
        }
    } else if (st.alertedLevel >= DensityLevel::High && nowMs - st.lastHighMs > 5000) {
        st.alertedLevel = DensityLevel::Low;   // re-arm once the area has calmed down
    }
}

void AnalysisEngine::checkTamper(const cv::Mat& frame, const AnalysisParams& p, qint64 nowMs, AnalysisResult& r)
{
    if (!p.fnTamper || nowMs - m_lastTamperCheckMs < 1000)
        return;
    m_lastTamperCheckMs = nowMs;

    cv::Mat small, gray, lap;
    cv::resize(frame, small, cv::Size(160, 90), 0, 0, cv::INTER_AREA);
    cv::cvtColor(small, gray, cv::COLOR_BGR2GRAY);
    cv::Scalar mean, stddev, lapMean, lapStd;
    cv::meanStdDev(gray, mean, stddev);
    cv::Laplacian(gray, lap, CV_32F);
    cv::meanStdDev(lap, lapMean, lapStd);
    const double brightness = mean[0];
    const double contrast = stddev[0];
    const double sharpness = lapStd[0] * lapStd[0];

    QString reason;
    if (brightness < 14)
        reason = QObject::tr("view is too dark or the lens is covered");
    else if (brightness > 245)
        reason = QObject::tr("view is blinded by strong light");
    else if (contrast < 7)
        reason = QObject::tr("view is blocked (no detail visible)");
    else if (m_baselineSamples >= 10 && sharpness < m_baselineSharpness * 0.15)
        reason = QObject::tr("image is out of focus or the lens is smeared");

    if (reason.isEmpty()) {
        m_tamperCount = 0;
        if (m_baselineSamples < 10) {
            m_baselineSharpness = (m_baselineSharpness * m_baselineSamples + sharpness) / (m_baselineSamples + 1);
            ++m_baselineSamples;
        } else {
            m_baselineSharpness = m_baselineSharpness * 0.98 + sharpness * 0.02;
        }
        return;
    }
    if (++m_tamperCount >= 3 && nowMs - m_lastTamperAlertMs > qint64(p.alertCooldownSec) * 2000) {
        m_lastTamperAlertMs = nowMs;
        addEvent(r, EventType::Tamper, Severity::Critical, QString(),
                 QObject::tr("Possible camera tampering: %1").arg(reason));
    }
}

void AnalysisEngine::saveSnapshot(const cv::Mat& frame, AnalysisResult& r, const ZoneSet& zs,
                                  const AnalysisParams& p)
{
    if (p.snapshotDir.isEmpty())
        return;
    const QDateTime now = QDateTime::currentDateTime();
    const QString dir = p.snapshotDir + QLatin1Char('/') + now.toString(QStringLiteral("yyyy-MM-dd"));
    QDir().mkpath(dir);
    const QString path = QStringLiteral("%1/cam%2_%3_alert.jpg")
                             .arg(dir)
                             .arg(m_cameraId, 2, 10, QLatin1Char('0'))
                             .arg(now.toString(QStringLiteral("HHmmss_zzz")));

    QImage img = matToImage(frame);
    if (img.isNull())
        return;
    {
        QPainter painter(&img);
        OverlayData od;
        od.zones = &zs.zones;
        od.lines = &zs.lines;
        od.objects = &r.objects;
        od.zoneStats = &r.zones;
        od.lineStats = &r.lines;
        OverlayOptions oo;
        Overlay::draw(painter, QRectF(0, 0, img.width(), img.height()), od, oo);

        // Caption bar with the event summary and timestamp.
        const EventRecord& e = r.events.first();
        const double s = qBound(0.8, img.height() / 540.0, 2.5);
        QFont f = painter.font();
        f.setPixelSize(int(14 * s));
        f.setBold(true);
        painter.setFont(f);
        const QRectF bar(0, img.height() - 30 * s, img.width(), 30 * s);
        painter.fillRect(bar, QColor(8, 14, 28, 200));
        painter.setPen(Overlay::alertColor());
        painter.drawText(bar.adjusted(10 * s, 0, -10 * s, 0), Qt::AlignVCenter | Qt::AlignLeft,
                         eventTypeName(e.type).toUpper() + QStringLiteral("  ") + e.message);
        painter.setPen(Qt::white);
        painter.drawText(bar.adjusted(10 * s, 0, -10 * s, 0), Qt::AlignVCenter | Qt::AlignRight,
                         e.cameraName + QStringLiteral("  ") + now.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
    }
    if (img.save(path, "JPG", 88)) {
        for (EventRecord& e : r.events)
            e.snapshotPath = path;
    }
}

AnalysisResult AnalysisEngine::process(const cv::Mat& frame, qint64 nowMs)
{
    AnalysisParams p;
    {
        QMutexLocker lock(&m_cfgMutex);
        p = m_params;
    }
    std::vector<Detection> dets;
    QElapsedTimer timer;
    timer.start();
    if (!frame.empty())
        Detector::instance().detect(frame, p.confThreshold, p.nmsThreshold, p.detectBags, dets);
    return analyze(frame, dets, nowMs, timer.nsecsElapsed() / 1e6);
}

AnalysisResult AnalysisEngine::analyze(const cv::Mat& frame, const std::vector<Detection>& dets, qint64 nowMs,
                                       double inferenceMs)
{
    AnalysisParams p;
    std::shared_ptr<const ZoneSet> zsPtr;
    QString camName;
    int camCapacity = 0;
    {
        QMutexLocker lock(&m_cfgMutex);
        p = m_params;
        zsPtr = m_zones;
        camName = m_cameraName;
        camCapacity = m_cameraCapacity;
    }
    const ZoneSet& zs = *zsPtr;
    if (m_resetRequested.exchange(false)) {
        m_counters.clear();
        m_zoneAlerts.clear();
    }

    AnalysisResult r;
    r.cameraId = m_cameraId;
    r.timestampMs = nowMs;
    if (frame.empty())
        return r;

    r.inferenceMs = inferenceMs;

    const float W = float(frame.cols);
    const float H = float(frame.rows);
    const float aspect = W / H;
    std::vector<TrackerDetection> personDets, bagDets;
    for (const Detection& d : dets) {
        TrackerDetection td;
        td.box = cv::Rect2f(d.box.x / W, d.box.y / H, d.box.width / W, d.box.height / H);
        td.score = d.score;
        if (d.cls == ClassPerson)
            td.appearance = Tracker::appearanceOf(frame, d.box);
        (d.cls == ClassBag ? bagDets : personDets).push_back(td);
    }

    // ---- tracking ---------------------------------------------------------
    // Keep tracks alive for a few analysis steps even at low analysis rates.
    m_persons.setMaxAge(qMax<qint64>(1500, 3LL * p.analysisIntervalMs));
    m_bags.setMaxAge(qMax<qint64>(6000, 4LL * p.analysisIntervalMs));
    m_persons.setTrailLength(p.trailLength);
    for (int id : m_persons.update(personDets, nowMs, aspect))
        m_personState.erase(id);
    for (int id : m_bags.update(bagDets, nowMs, aspect))
        m_bagState.erase(id);

    QHash<QString, FlowDelta> deltas;
    auto delta = [&](const QString& id, bool isLine, bool inTotals) -> FlowDelta& {
        FlowDelta& fd = deltas[id];
        fd.targetId = id;
        fd.isLine = isLine;
        fd.inTotals = inTotals;
        return fd;
    };

    QHash<QString, int> zoneCount;
    // A camera that has a counting line counts its site In/Out there; its
    // zones then report occupancy and their own in/out only, so the same
    // people are not added to the site totals twice.
    bool lineInTotals = false;
    for (const CountLine& l : zs.lines)
        lineInTotals = lineInTotals || (l.enabled && l.includeInTotals);
    auto zoneInTotals = [lineInTotals](const Zone& z) { return z.includeInTotals && !lineInTotals; };
    // People who newly triggered a per-person rule in this step, grouped per
    // zone so a crowd produces one alert instead of one per person.
    QHash<QString, QVector<int>> newIntruders, newLoiterers;
    QHash<QString, int> loiterSeconds;
    for (const Track& tr : m_persons.tracks()) {
        if (!tr.confirmed || tr.misses > 1)
            continue;
        PersonState& st = m_personState[tr.id];
        const cv::Point2f foot = tr.foot();
        const bool fresh = tr.misses == 0;
        quint32 flags = 0;

        for (const Zone& z : zs.zones) {
            if (!z.enabled || z.polygon.size() < 3)
                continue;
            ZoneMember& m = st.zones[z.id];
            if (fresh) {
                const bool inside = z.polygon.containsPoint(QPointF(foot.x, foot.y), Qt::OddEvenFill);
                if (!m.initialized) {
                    m.initialized = true;
                    m.inside = inside;
                    m.enterMs = nowMs;
                } else if (inside != m.inside) {
                    if (++m.pending >= 2) {   // debounce jitter on the boundary
                        m.pending = 0;
                        m.inside = inside;
                        if (inside) {
                            m.enterMs = nowMs;
                            m.loiterAlerted = false;
                            if (p.fnCounting) {
                                m_counters[z.id].first++;
                                delta(z.id, false, zoneInTotals(z)).in++;
                                m.entryObserved = true;
                            }
                        } else {
                            m.intrusionAlerted = false;
                            if (p.fnCounting) {
                                // Exits are always counted (this also keeps IN/OUT balanced
                                // when a track breaks inside the zone), but dwell time is
                                // only known when the entry itself was observed.
                                m_counters[z.id].second++;
                                delta(z.id, false, zoneInTotals(z)).out++;
                                if (m.entryObserved)
                                    r.dwell.push_back({ z.id, (nowMs - m.enterMs) / 1000.0 });
                            }
                            m.entryObserved = false;
                        }
                    }
                } else {
                    m.pending = 0;
                }
            }
            if (!m.inside)
                continue;
            zoneCount[z.id]++;
            const double dwell = (nowMs - m.enterMs) / 1000.0;
            if (z.type == ZoneType::Restricted && p.fnIntrusion) {
                flags |= FlagIntrusion;
                if (!m.intrusionAlerted) {
                    m.intrusionAlerted = true;
                    newIntruders[z.id].push_back(tr.id);
                }
            }
            if (z.loiterSec > 0 && p.fnLoitering && dwell >= z.loiterSec) {
                flags |= FlagLoitering;
                if (!m.loiterAlerted) {
                    m.loiterAlerted = true;
                    newLoiterers[z.id].push_back(tr.id);
                    loiterSeconds[z.id] = z.loiterSec;
                }
            }
        }

        if (fresh) {
            // Counting lines with hysteresis: a person must be clearly on one
            // side, then clearly on the other, so detection jitter while
            // someone stands on the line never counts.
            if (p.fnCounting) {
                const double margin = std::max(0.01, 0.15 * double(tr.box.height));
                for (const CountLine& l : zs.lines) {
                    if (!l.enabled)
                        continue;
                    double t = 0.0;
                    const double d = signedDistance(l, foot, aspect, t);
                    const int sideNow = d > margin ? 1 : (d < -margin ? -1 : 0);
                    if (sideNow == 0)
                        continue;
                    int& committed = st.lineSide[l.id];   // 0 = not known yet
                    const bool onSegment = t >= -0.05 && t <= 1.05;
                    if (committed != 0 && sideNow != committed && onSegment) {
                        const bool forward = committed < 0 && sideNow > 0;   // towards the line normal
                        const bool isIn = forward != l.invert;
                        FlowDelta& fd = delta(l.id, true, l.includeInTotals);
                        if (isIn) {
                            m_counters[l.id].first++;
                            fd.in++;
                        } else {
                            m_counters[l.id].second++;
                            fd.out++;
                            if (l.alertOnOut)
                                addEvent(r, EventType::WrongWay, Severity::Warning, l.name,
                                         QObject::tr("Person P-%1 crossed %2 in the wrong direction").arg(tr.id).arg(l.name));
                        }
                    }
                    // Walking around the end of a line changes side without counting.
                    committed = sideNow;
                }
            }

            // Running: speed measured in body heights per second.
            if (p.fnRunning && st.hasLastFoot) {
                const float dt = (nowMs - st.lastFootMs) / 1000.f;
                if (dt > 0.02f && dt < 1.5f && tr.box.height > 0.04f) {
                    const float dx = (foot.x - st.lastFoot.x) * aspect;
                    const float dy = foot.y - st.lastFoot.y;
                    const float speed = std::sqrt(dx * dx + dy * dy) / tr.box.height / dt;
                    st.speedEma = st.speedEma * 0.5f + speed * 0.5f;
                    st.fastCount = st.speedEma > p.runSpeed ? st.fastCount + 1 : 0;
                }
            }
            if (p.fnRunning && st.fastCount >= 3) {
                flags |= FlagRunning;
                if (nowMs - st.lastRunAlertMs > 20000) {
                    st.lastRunAlertMs = nowMs;
                    addEvent(r, EventType::Running, Severity::Warning, QString(),
                             QObject::tr("Person P-%1 is running").arg(tr.id));
                }
            }

            st.lastFoot = foot;
            st.lastFootMs = nowMs;
            st.hasLastFoot = true;
            addHeat(foot);
            st.flags = flags;
        } else {
            flags = st.flags;
        }
        r.personCount++;
    }

    // ---- grouped per-person alerts -----------------------------------------
    auto idList = [](const QVector<int>& ids) {
        QStringList parts;
        for (int i = 0; i < ids.size() && i < 6; ++i)
            parts << QStringLiteral("P-%1").arg(ids[i]);
        if (ids.size() > 6)
            parts << QStringLiteral("...");
        return parts.join(QStringLiteral(", "));
    };
    for (const Zone& z : zs.zones) {
        const QVector<int> intr = newIntruders.value(z.id);
        if (intr.size() == 1)
            addEvent(r, EventType::Intrusion, Severity::Critical, z.name,
                     QObject::tr("Person P-%1 entered restricted area %2").arg(intr[0]).arg(z.name));
        else if (intr.size() > 1)
            addEvent(r, EventType::Intrusion, Severity::Critical, z.name,
                     QObject::tr("%1 people entered restricted area %2 (%3)").arg(intr.size()).arg(z.name, idList(intr)));
        const QVector<int> loit = newLoiterers.value(z.id);
        if (loit.size() == 1)
            addEvent(r, EventType::Loitering, Severity::Warning, z.name,
                     QObject::tr("Person P-%1 has stayed in %2 for %3 s").arg(loit[0]).arg(z.name).arg(loiterSeconds.value(z.id)));
        else if (loit.size() > 1)
            addEvent(r, EventType::Loitering, Severity::Warning, z.name,
                     QObject::tr("%1 people have stayed in %2 for over %3 s (%4)")
                         .arg(loit.size()).arg(z.name).arg(loiterSeconds.value(z.id)).arg(idList(loit)));
    }

    // ---- zone statistics & density ---------------------------------------
    QHash<QString, DensityLevel> zoneLevels;
    for (const Zone& z : zs.zones) {
        if (!z.enabled || z.polygon.size() < 3)
            continue;
        ZoneStat s;
        s.zoneId = z.id;
        s.name = z.name;
        s.type = z.type;
        s.count = zoneCount.value(z.id);
        s.capacity = z.capacity;
        s.in = m_counters.value(z.id).first;
        s.out = m_counters.value(z.id).second;
        if (z.areaSqm > 0)
            s.densityPerSqm = s.count / z.areaSqm;
        s.inTotals = zoneInTotals(z);
        if (z.type != ZoneType::Restricted)
            checkDensity(z.id, z.name, z.type, s.count, z.capacity, p, nowMs, r, s.level);
        zoneLevels.insert(z.id, s.level);
        r.zones.push_back(s);
    }
    for (const CountLine& l : zs.lines) {
        if (!l.enabled)
            continue;
        LineStat s;
        s.lineId = l.id;
        s.inTotals = l.includeInTotals;
        s.name = l.name;
        s.in = m_counters.value(l.id).first;
        s.out = m_counters.value(l.id).second;
        r.lines.push_back(s);
    }
    checkDensity(QStringLiteral("__view__"), camName.isEmpty() ? QObject::tr("Camera view") : camName,
                 ZoneType::Counting, r.personCount, camCapacity, p, nowMs, r, r.viewLevel);

    // ---- objects for display ---------------------------------------------
    for (const Track& tr : m_persons.tracks()) {
        if (!tr.confirmed || tr.misses > 2)
            continue;
        TrackedObject o;
        o.id = tr.id;
        o.cls = ClassPerson;
        o.box = QRectF(tr.box.x, tr.box.y, tr.box.width, tr.box.height);
        o.score = tr.score;
        auto it = m_personState.find(tr.id);
        if (it != m_personState.end()) {
            o.flags = it->second.flags;
            for (auto zm = it->second.zones.cbegin(); zm != it->second.zones.cend(); ++zm) {
                if (!zm.value().inside)
                    continue;
                o.dwellSec = qMax(o.dwellSec, float((nowMs - zm.value().enterMs) / 1000.0));
                if (zoneLevels.value(zm.key(), DensityLevel::Low) >= DensityLevel::High)
                    o.flags |= FlagInCrowd;
            }
        }
        if (r.viewLevel >= DensityLevel::High)
            o.flags |= FlagInCrowd;
        for (const cv::Point2f& pt : tr.trail)
            o.trail.push_back(QPointF(pt.x, pt.y));
        r.objects.push_back(o);
    }

    // ---- abandoned objects -------------------------------------------------
    for (const Track& bag : m_bags.tracks()) {
        if (!bag.confirmed)
            continue;
        BagState& bs = m_bagState[bag.id];
        const cv::Point2f c = bag.center();
        if (!bs.initialized) {
            bs.initialized = true;
            bs.origin = c;
            bs.stillSinceMs = nowMs;
            bs.lastAttendedMs = nowMs;
        } else {
            const float dx = (c.x - bs.origin.x) * aspect;
            const float dy = c.y - bs.origin.y;
            if (std::sqrt(dx * dx + dy * dy) > 0.5f * std::max(bag.box.width * aspect, bag.box.height)) {
                bs.origin = c;
                bs.stillSinceMs = nowMs;
                bs.alerted = false;
            }
        }
        const float reach = std::max(0.08f, bag.box.height * 3.f);
        for (const Track& person : m_persons.tracks()) {
            if (!person.confirmed || person.misses > 1)
                continue;
            const cv::Point2f pc = person.center();
            const float dx = (pc.x - c.x) * aspect;
            const float dy = pc.y - c.y;
            if (std::sqrt(dx * dx + dy * dy) < reach + person.box.height * 0.5f) {
                bs.lastAttendedMs = nowMs;
                break;
            }
        }
        const double stillSec = (nowMs - bs.stillSinceMs) / 1000.0;
        const double aloneSec = (nowMs - bs.lastAttendedMs) / 1000.0;
        TrackedObject o;
        o.id = bag.id;
        o.cls = ClassBag;
        o.box = QRectF(bag.box.x, bag.box.y, bag.box.width, bag.box.height);
        o.score = bag.score;
        o.dwellSec = float(stillSec);
        if (p.fnAbandoned && stillSec >= p.abandonSec && aloneSec >= p.abandonSec * 0.5) {
            o.flags |= FlagAbandoned;
            if (!bs.alerted) {
                bs.alerted = true;
                addEvent(r, EventType::AbandonedObject, Severity::Critical, QString(),
                         QObject::tr("Unattended object detected (stationary for %1 s)").arg(int(stillSec)));
            }
        }
        r.objects.push_back(o);
    }

    checkTamper(frame, p, nowMs, r);

    for (auto it = deltas.cbegin(); it != deltas.cend(); ++it)
        if (it.value().in || it.value().out)
            r.deltas.push_back(it.value());

    if (!r.events.isEmpty() && p.autoSnapshot && nowMs - m_lastSnapshotMs > 2000) {
        m_lastSnapshotMs = nowMs;
        saveSnapshot(frame, r, zs, p);
    }
    return r;
}
