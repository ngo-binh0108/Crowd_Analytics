#pragma once

#include "Detector.h"
#include "Settings.h"
#include "Tracker.h"
#include "Types.h"

#include <QHash>
#include <QImage>
#include <QMutex>

#include <atomic>
#include <memory>
#include <unordered_map>

#include <opencv2/core.hpp>

struct ZoneSet {
    QVector<Zone> zones;
    QVector<CountLine> lines;
};

// Per-camera analytics pipeline: detection -> tracking -> zone/line counting
// -> security rules -> result. process() is called from the analysis thread
// pool, never concurrently for the same camera. Configuration setters are
// thread-safe and may be called from the UI thread at any time.
class AnalysisEngine {
public:
    explicit AnalysisEngine(int cameraId);

    void setCameraInfo(const QString& name, int capacity);
    void setZones(const QVector<Zone>& zones, const QVector<CountLine>& lines);
    void setParams(const AnalysisParams& params);
    void requestCounterReset();
    void resetHeatmap();

    // Detects objects in `frame` and analyses them.
    AnalysisResult process(const cv::Mat& frame, qint64 nowMs);
    // Analyses detections supplied by the caller (pixel boxes in `frame`);
    // process() uses it, and tests can drive the tracking/counting/rules
    // deterministically through it.
    AnalysisResult analyze(const cv::Mat& frame, const std::vector<Detection>& dets, qint64 nowMs,
                           double inferenceMs = 0.0);

    // Colour heatmap (ARGB32, alpha follows intensity). Thread-safe.
    QImage heatmapImage() const;

private:
    struct ZoneMember {
        bool initialized = false;
        bool inside = false;
        int pending = 0;
        qint64 enterMs = 0;
        bool intrusionAlerted = false;
        bool loiterAlerted = false;
        bool entryObserved = false;   // the crossing into the zone was seen (dwell is valid)
    };
    struct PersonState {
        QHash<QString, ZoneMember> zones;
        QHash<QString, int> lineSide;   // committed side per counting line (-1 / +1, 0 = unknown)
        cv::Point2f lastFoot;
        qint64 lastFootMs = 0;
        bool hasLastFoot = false;
        float speedEma = 0.f;
        int fastCount = 0;
        qint64 lastRunAlertMs = -1000000;
        quint32 flags = 0;
    };
    struct BagState {
        bool initialized = false;
        cv::Point2f origin;
        qint64 stillSinceMs = 0;
        qint64 lastAttendedMs = 0;
        bool alerted = false;
    };
    struct ZoneAlertState {
        DensityLevel alertedLevel = DensityLevel::Low;
        qint64 lastAlertMs = 0;
        qint64 lastHighMs = 0;
    };

    void checkDensity(const QString& key, const QString& zoneName, ZoneType type, int count, int capacity,
                      const AnalysisParams& p, qint64 nowMs, AnalysisResult& r, DensityLevel& levelOut);
    void checkTamper(const cv::Mat& frame, const AnalysisParams& p, qint64 nowMs, AnalysisResult& r);
    void addHeat(const cv::Point2f& foot);
    void addEvent(AnalysisResult& r, EventType type, Severity sev, const QString& zone, const QString& msg) const;
    void saveSnapshot(const cv::Mat& frame, AnalysisResult& r, const ZoneSet& zs, const AnalysisParams& p);

    const int m_cameraId;

    mutable QMutex m_cfgMutex;
    QString m_cameraName;
    int m_cameraCapacity = 0;
    std::shared_ptr<const ZoneSet> m_zones;
    AnalysisParams m_params;
    std::atomic<bool> m_resetRequested{ false };

    Tracker m_persons;
    Tracker m_bags;
    std::unordered_map<int, PersonState> m_personState;
    std::unordered_map<int, BagState> m_bagState;
    QHash<QString, QPair<qint64, qint64>> m_counters;   // id -> (in, out)
    QHash<QString, ZoneAlertState> m_zoneAlerts;

    // Tamper detection state
    qint64 m_lastTamperCheckMs = 0;
    int m_tamperCount = 0;
    int m_baselineSamples = 0;
    double m_baselineSharpness = 0.0;
    qint64 m_lastTamperAlertMs = -1000000;
    qint64 m_lastSnapshotMs = -1000000;

    mutable QMutex m_heatMutex;
    cv::Mat m_heat;   // CV_32F accumulation grid
};
