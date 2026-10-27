#pragma once

#include <QString>
#include <QStringList>

// Parameters consumed by the per-camera analysis engine (copied into worker
// threads, so it must stay a plain value type).
struct AnalysisParams {
    float confThreshold = 0.35f;
    float nmsThreshold = 0.45f;
    bool detectBags = true;

    bool fnCounting = true;
    bool fnDensity = true;
    bool fnIntrusion = true;
    bool fnLoitering = true;
    bool fnAbandoned = false;
    bool fnRunning = false;
    bool fnTamper = true;

    int densityWarnPct = 80;
    int densityCritPct = 100;
    int abandonSec = 30;
    double runSpeed = 1.8;         // body heights per second
    int alertCooldownSec = 30;
    int trailLength = 24;
    int analysisIntervalMs = 125;  // time between analysed frames of one camera

    bool autoSnapshot = true;
    QString snapshotDir;
};

// Application-wide settings. Owned by the UI thread; persisted to
// <data>/settings.ini.
struct AppSettings {
    // General
    QString siteName = QStringLiteral("Central Station");
    int siteType = 0;              // index into siteTypes()
    int siteCapacity = 500;
    QString operatorName = QStringLiteral("Operator");
    bool autoStartAnalysis = false;
    bool rtspOverTcp = true;
    bool minimizeToTray = false;

    // Detection
    QString modelPath = QStringLiteral("models/yolo26n.onnx");
    int device = 0;                // 0 = CPU, 1 = CUDA
    int cudaDeviceId = 0;
    float confThreshold = 0.35f;
    float nmsThreshold = 0.45f;
    int inputSize = 640;
    int modelFormat = 0;           // 0 auto, 1 YOLOv5, 2 YOLOv8/11, 3 end-to-end (NMS inside)
    double analysisFps = 8.0;      // per camera
    int inferenceThreads = 2;      // concurrent inference jobs
    int intraOpThreads = 2;        // ONNX Runtime threads per inference
    bool unloadOnStop = true;
    bool detectBags = true;

    // AI functions
    bool fnCounting = true;
    bool fnDensity = true;
    bool fnIntrusion = true;
    bool fnLoitering = true;
    bool fnAbandoned = false;
    bool fnRunning = false;
    bool fnTamper = true;
    bool fnHeatmap = false;        // heatmap overlay on video tiles
    bool fnPrivacy = false;        // pixelate people on screen
    bool fnTracks = true;          // draw movement trails

    // Alerts
    int densityWarnPct = 80;
    int densityCritPct = 100;
    int abandonSec = 30;
    double runSpeed = 1.8;
    int alertCooldownSec = 30;
    bool alertSound = true;
    bool trayNotify = true;
    bool autoSnapshot = true;
    int autoRecordSec = 0;         // 0 = off, record a clip on critical alerts

    // Storage
    QString dataDir;
    QString snapshotDir;
    QString recordDir;
    int recordFormat = 0;          // 0 MP4, 1 AVI (MJPG)
    int retentionDays = 30;

    // Display
    int maxDisplayFps = 25;
    bool showIds = true;
    bool showZones = true;
    bool showConfidence = false;
    int gridLayout = 4;
    bool bottomPanelVisible = true;
    qint64 countersResetEpoch = 0; // seconds since epoch; counts before this are ignored today

    static AppSettings& instance();
    static QStringList siteTypes();

    void load();
    void save() const;
    QString settingsFile() const;
    QString resolvedModelPath() const;          // falls back to a bundled model if the file is missing
    static QStringList bundledModels();         // models/*.onnx next to the executable (relative paths)
    AnalysisParams analysisParams() const;
    void ensureDirectories() const;
};
