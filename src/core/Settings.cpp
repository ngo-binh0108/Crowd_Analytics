#include "Settings.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QSettings>
#include <QStandardPaths>

static QString defaultDataDir()
{
    // Portable layout first (<exe>/data); fall back to the user's AppData
    // folder when the install directory is not writable (e.g. Program Files).
    const QString portable = QCoreApplication::applicationDirPath() + QStringLiteral("/data");
    QDir().mkpath(portable);
    QFile probe(portable + QStringLiteral("/.write_test"));
    if (probe.open(QIODevice::WriteOnly)) {
        probe.close();
        probe.remove();
        return portable;
    }
    const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(appData);
    return appData;
}

AppSettings& AppSettings::instance()
{
    static AppSettings s;
    return s;
}

QStringList AppSettings::siteTypes()
{
    return { QObject::tr("Railway Station"), QObject::tr("Subway / Metro"), QObject::tr("Airport"),
             QObject::tr("Cinema / Theater"), QObject::tr("Shopping Mall"), QObject::tr("Stadium / Arena"),
             QObject::tr("Bus Terminal"), QObject::tr("Other") };
}

QString AppSettings::settingsFile() const
{
    return dataDir + QStringLiteral("/settings.ini");
}

void AppSettings::load()
{
    dataDir = defaultDataDir();
    QSettings s(settingsFile(), QSettings::IniFormat);

    s.beginGroup("General");
    siteName = s.value("siteName", siteName).toString();
    siteType = s.value("siteType", siteType).toInt();
    siteCapacity = s.value("siteCapacity", siteCapacity).toInt();
    operatorName = s.value("operatorName", operatorName).toString();
    autoStartAnalysis = s.value("autoStartAnalysis", autoStartAnalysis).toBool();
    rtspOverTcp = s.value("rtspOverTcp", rtspOverTcp).toBool();
    minimizeToTray = s.value("minimizeToTray", minimizeToTray).toBool();
    s.endGroup();

    s.beginGroup("Detection");
    modelPath = s.value("modelPath", modelPath).toString();
    device = s.value("device", device).toInt();
    cudaDeviceId = s.value("cudaDeviceId", cudaDeviceId).toInt();
    confThreshold = s.value("confThreshold", confThreshold).toFloat();
    nmsThreshold = s.value("nmsThreshold", nmsThreshold).toFloat();
    inputSize = s.value("inputSize", inputSize).toInt();
    modelFormat = s.value("modelFormat", modelFormat).toInt();
    analysisFps = s.value("analysisFps", analysisFps).toDouble();
    inferenceThreads = s.value("inferenceThreads", inferenceThreads).toInt();
    intraOpThreads = s.value("intraOpThreads", intraOpThreads).toInt();
    unloadOnStop = s.value("unloadOnStop", unloadOnStop).toBool();
    detectBags = s.value("detectBags", detectBags).toBool();
    s.endGroup();

    s.beginGroup("Functions");
    fnCounting = s.value("counting", fnCounting).toBool();
    fnDensity = s.value("density", fnDensity).toBool();
    fnIntrusion = s.value("intrusion", fnIntrusion).toBool();
    fnLoitering = s.value("loitering", fnLoitering).toBool();
    fnAbandoned = s.value("abandoned", fnAbandoned).toBool();
    fnRunning = s.value("running", fnRunning).toBool();
    fnTamper = s.value("tamper", fnTamper).toBool();
    fnHeatmap = s.value("heatmap", fnHeatmap).toBool();
    fnPrivacy = s.value("privacy", fnPrivacy).toBool();
    fnTracks = s.value("tracks", fnTracks).toBool();
    s.endGroup();

    s.beginGroup("Alerts");
    densityWarnPct = s.value("densityWarnPct", densityWarnPct).toInt();
    densityCritPct = s.value("densityCritPct", densityCritPct).toInt();
    abandonSec = s.value("abandonSec", abandonSec).toInt();
    runSpeed = s.value("runSpeed", runSpeed).toDouble();
    alertCooldownSec = s.value("alertCooldownSec", alertCooldownSec).toInt();
    alertSound = s.value("alertSound", alertSound).toBool();
    trayNotify = s.value("trayNotify", trayNotify).toBool();
    autoSnapshot = s.value("autoSnapshot", autoSnapshot).toBool();
    autoRecordSec = s.value("autoRecordSec", autoRecordSec).toInt();
    s.endGroup();

    s.beginGroup("Storage");
    snapshotDir = s.value("snapshotDir", dataDir + "/snapshots").toString();
    recordDir = s.value("recordDir", dataDir + "/recordings").toString();
    recordFormat = s.value("recordFormat", recordFormat).toInt();
    retentionDays = s.value("retentionDays", retentionDays).toInt();
    s.endGroup();

    s.beginGroup("Display");
    maxDisplayFps = s.value("maxDisplayFps", maxDisplayFps).toInt();
    showIds = s.value("showIds", showIds).toBool();
    showZones = s.value("showZones", showZones).toBool();
    showConfidence = s.value("showConfidence", showConfidence).toBool();
    gridLayout = s.value("gridLayout", gridLayout).toInt();
    bottomPanelVisible = s.value("bottomPanelVisible", bottomPanelVisible).toBool();
    countersResetEpoch = s.value("countersResetEpoch", countersResetEpoch).toLongLong();
    s.endGroup();

    // Sanitize values that could come from a hand-edited file.
    analysisFps = qBound(0.5, analysisFps, 30.0);
    inferenceThreads = qBound(1, inferenceThreads, 16);
    intraOpThreads = qBound(1, intraOpThreads, 32);
    inputSize = qBound(160, inputSize, 1920);
    maxDisplayFps = qBound(1, maxDisplayFps, 60);
    ensureDirectories();
}

void AppSettings::save() const
{
    QSettings s(settingsFile(), QSettings::IniFormat);

    s.beginGroup("General");
    s.setValue("siteName", siteName);
    s.setValue("siteType", siteType);
    s.setValue("siteCapacity", siteCapacity);
    s.setValue("operatorName", operatorName);
    s.setValue("autoStartAnalysis", autoStartAnalysis);
    s.setValue("rtspOverTcp", rtspOverTcp);
    s.setValue("minimizeToTray", minimizeToTray);
    s.endGroup();

    s.beginGroup("Detection");
    s.setValue("modelPath", modelPath);
    s.setValue("device", device);
    s.setValue("cudaDeviceId", cudaDeviceId);
    s.setValue("confThreshold", double(confThreshold));   // plain number in the INI
    s.setValue("nmsThreshold", double(nmsThreshold));
    s.setValue("inputSize", inputSize);
    s.setValue("modelFormat", modelFormat);
    s.setValue("analysisFps", analysisFps);
    s.setValue("inferenceThreads", inferenceThreads);
    s.setValue("intraOpThreads", intraOpThreads);
    s.setValue("unloadOnStop", unloadOnStop);
    s.setValue("detectBags", detectBags);
    s.endGroup();

    s.beginGroup("Functions");
    s.setValue("counting", fnCounting);
    s.setValue("density", fnDensity);
    s.setValue("intrusion", fnIntrusion);
    s.setValue("loitering", fnLoitering);
    s.setValue("abandoned", fnAbandoned);
    s.setValue("running", fnRunning);
    s.setValue("tamper", fnTamper);
    s.setValue("heatmap", fnHeatmap);
    s.setValue("privacy", fnPrivacy);
    s.setValue("tracks", fnTracks);
    s.endGroup();

    s.beginGroup("Alerts");
    s.setValue("densityWarnPct", densityWarnPct);
    s.setValue("densityCritPct", densityCritPct);
    s.setValue("abandonSec", abandonSec);
    s.setValue("runSpeed", runSpeed);
    s.setValue("alertCooldownSec", alertCooldownSec);
    s.setValue("alertSound", alertSound);
    s.setValue("trayNotify", trayNotify);
    s.setValue("autoSnapshot", autoSnapshot);
    s.setValue("autoRecordSec", autoRecordSec);
    s.endGroup();

    s.beginGroup("Storage");
    s.setValue("snapshotDir", snapshotDir);
    s.setValue("recordDir", recordDir);
    s.setValue("recordFormat", recordFormat);
    s.setValue("retentionDays", retentionDays);
    s.endGroup();

    s.beginGroup("Display");
    s.setValue("maxDisplayFps", maxDisplayFps);
    s.setValue("showIds", showIds);
    s.setValue("showZones", showZones);
    s.setValue("showConfidence", showConfidence);
    s.setValue("gridLayout", gridLayout);
    s.setValue("bottomPanelVisible", bottomPanelVisible);
    s.setValue("countersResetEpoch", countersResetEpoch);
    s.endGroup();
    s.sync();
}

QStringList AppSettings::bundledModels()
{
    const QDir dir(QCoreApplication::applicationDirPath() + QStringLiteral("/models"));
    QStringList out;
    for (const QString& f : dir.entryList({ QStringLiteral("*.onnx") }, QDir::Files, QDir::Name))
        out << QStringLiteral("models/") + f;
    return out;
}

QString AppSettings::resolvedModelPath() const
{
    const QDir appDir(QCoreApplication::applicationDirPath());
    if (!modelPath.isEmpty()) {
        const QFileInfo fi(modelPath);
        const QString path = fi.isAbsolute() ? fi.absoluteFilePath() : appDir.absoluteFilePath(modelPath);
        if (QFileInfo::exists(path))
            return path;
    }
    // Configured file missing: use a bundled model (preferred order), if any.
    const QStringList bundled = bundledModels();
    for (const QString& preferred : { QStringLiteral("models/yolo26n.onnx"), QStringLiteral("models/yolo11n.onnx") })
        if (bundled.contains(preferred))
            return appDir.absoluteFilePath(preferred);
    if (!bundled.isEmpty())
        return appDir.absoluteFilePath(bundled.first());
    return modelPath.isEmpty() ? QString() : appDir.absoluteFilePath(modelPath);
}

AnalysisParams AppSettings::analysisParams() const
{
    AnalysisParams p;
    p.confThreshold = confThreshold;
    p.nmsThreshold = nmsThreshold;
    p.detectBags = detectBags && fnAbandoned;
    p.fnCounting = fnCounting;
    p.fnDensity = fnDensity;
    p.fnIntrusion = fnIntrusion;
    p.fnLoitering = fnLoitering;
    p.fnAbandoned = fnAbandoned;
    p.fnRunning = fnRunning;
    p.fnTamper = fnTamper;
    p.densityWarnPct = densityWarnPct;
    p.densityCritPct = qMax(densityCritPct, densityWarnPct + 1);
    p.abandonSec = abandonSec;
    p.runSpeed = runSpeed;
    p.alertCooldownSec = alertCooldownSec;
    p.analysisIntervalMs = int(1000.0 / analysisFps);
    p.autoSnapshot = autoSnapshot;
    p.snapshotDir = snapshotDir;
    return p;
}

void AppSettings::ensureDirectories() const
{
    QDir().mkpath(dataDir);
    QDir().mkpath(snapshotDir);
    QDir().mkpath(recordDir);
}
