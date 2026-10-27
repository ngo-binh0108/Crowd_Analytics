#include "CameraManager.h"
#include "CameraWorker.h"
#include "Detector.h"
#include "Overlay.h"
#include "Settings.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QPointer>
#include <QRegularExpression>
#include <QUuid>
#include <QtConcurrent/QtConcurrentRun>

static bool isOnline(CameraStatus s)
{
    return s == CameraStatus::Live || s == CameraStatus::Playing || s == CameraStatus::Still;
}

CameraManager::CameraManager(QObject* parent) : QObject(parent)
{
    m_pool.setMaxThreadCount(AppSettings::instance().inferenceThreads);
    m_pool.setExpiryTimeout(30000);
    connect(&m_loadWatcher, &QFutureWatcher<QString>::finished, this, &CameraManager::onModelLoaded);
}

CameraManager::~CameraManager()
{
    shutdown();
}

QString CameraManager::newObjectId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces).remove(QLatin1Char('-')).left(10);
}

QString CameraManager::configFile() const
{
    return AppSettings::instance().dataDir + QStringLiteral("/cameras.json");
}

void CameraManager::load()
{
    QFile f(configFile());
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    m_nextId = qMax(1, root["nextId"].toInt(1));
    for (const QJsonValue& v : root["cameras"].toArray()) {
        if (m_cams.size() >= kMaxCameras)
            break;
        CameraConfig cfg = CameraConfig::fromJson(v.toObject());
        if (cfg.id <= 0)
            cfg.id = m_nextId++;
        m_nextId = qMax(m_nextId, cfg.id + 1);
        m_cams.push_back(cfg);
        startWorker(cfg);
    }
    emit camerasChanged();
}

void CameraManager::save() const
{
    QJsonArray arr;
    for (const CameraConfig& c : m_cams)
        arr.append(c.toJson());
    QJsonObject root;
    root["version"] = 1;
    root["nextId"] = m_nextId;
    root["cameras"] = arr;
    QFile f(configFile());
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

void CameraManager::shutdown()
{
    if (m_loadWatcher.isRunning())
        m_loadWatcher.waitForFinished();
    m_state = AnalysisState::Stopped;
    m_restartPending = false;
    // Interrupt every capture thread first so they wind down in parallel.
    for (CameraWorker* w : qAsConst(m_workers))
        w->retire();
    for (CameraWorker* w : qAsConst(m_retiring))
        w->retire();
    for (CameraWorker* w : qAsConst(m_workers)) {
        w->stopAndWait();
        delete w;
    }
    for (CameraWorker* w : qAsConst(m_retiring)) {
        w->stopAndWait();
        delete w;
    }
    m_workers.clear();
    m_retiring.clear();
    m_pool.waitForDone();
    Detector::instance().unload();
}

const CameraConfig* CameraManager::camera(int id) const
{
    for (const CameraConfig& c : m_cams)
        if (c.id == id)
            return &c;
    return nullptr;
}

QStringList CameraManager::locations() const
{
    QStringList out;
    for (const CameraConfig& c : m_cams)
        if (!c.location.trimmed().isEmpty() && !out.contains(c.location.trimmed()))
            out << c.location.trimmed();
    out.sort(Qt::CaseInsensitive);
    return out;
}

int CameraManager::onlineCount() const
{
    int n = 0;
    for (const CameraConfig& c : m_cams)
        n += isOnline(status(c.id)) ? 1 : 0;
    return n;
}

void CameraManager::configureWorker(CameraWorker* w, const CameraConfig& cfg)
{
    const AppSettings& s = AppSettings::instance();
    w->updateConfig(cfg);
    w->setParams(s.analysisParams());
    w->setAnalysisIntervalMs(int(1000.0 / s.analysisFps));
    w->setPrivacyMask(s.fnPrivacy);
    w->setDisplay(!m_hidden.contains(cfg.id), m_displayWidth.value(cfg.id, 960), s.maxDisplayFps);
    w->setAnalysisEnabled(m_state == AnalysisState::Running && cfg.analysisEnabled);
}

void CameraManager::startWorker(const CameraConfig& cfg)
{
    auto* w = new CameraWorker(cfg, &m_pool, this);
    const QPointer<CameraWorker> wp(w);
    // Frames and status from a retired worker (camera removed / reconnected)
    // may still be queued; only the current worker for the id is honoured.
    connect(
        w, &CameraWorker::frameReady, this,
        [this, wp](int id, const QImage& img) {
            if (!wp)
                return;
            wp->frameConsumed();
            if (m_workers.value(id) == wp)
                emit frameReady(id, img);
        },
        Qt::QueuedConnection);
    connect(
        w, &CameraWorker::statusChanged, this,
        [this, wp](int id, CameraStatus st, const QString& msg) {
            if (wp && m_workers.value(id) == wp)
                onWorkerStatus(id, st, msg);
        },
        Qt::QueuedConnection);
    connect(
        w, &CameraWorker::recordingChanged, this,
        [this](int id, bool recording, const QString& info) {
            emit recordingChanged(id, recording, info);
            if (recording)
                return;
            if (QFileInfo::exists(info))
                raise(id, EventType::Recording, Severity::Info,
                      tr("Recording saved: %1").arg(QDir::toNativeSeparators(info)));
            else if (!info.isEmpty())
                raise(id, EventType::Recording, Severity::Warning, info);
        },
        Qt::QueuedConnection);
    configureWorker(w, cfg);
    m_workers.insert(cfg.id, w);
    m_status.insert(cfg.id, CameraStatus::Connecting);
    w->start();
}

void CameraManager::stopWorker(int id)
{
    // Non-blocking: a capture thread may be stuck for seconds in a network
    // open/read. The worker is deleted once its thread has finished.
    CameraWorker* w = m_workers.take(id);
    if (w) {
        w->retire();
        // Normal sources stop within milliseconds (and must release e.g. a
        // USB device before a replacement worker opens it). Only a thread
        // stuck in a network call is left to finish in the background.
        if (w->wait(1500)) {
            w->deleteLater();
        } else {
            m_retiring.insert(w);
            connect(w, &QThread::finished, this, [this, w] {
                if (m_retiring.remove(w))
                    w->deleteLater();
            });
            if (w->isFinished() && m_retiring.remove(w))
                w->deleteLater();
        }
    }
    m_lastResults.remove(id);
    m_status.insert(id, CameraStatus::Stopped);
    m_wasOnline.remove(id);
}

int CameraManager::addCamera(CameraConfig cfg)
{
    if (m_cams.size() >= kMaxCameras)
        return -1;
    cfg.id = m_nextId++;
    if (cfg.name.trimmed().isEmpty())
        cfg.name = tr("Camera %1").arg(cfg.id);
    m_cams.push_back(cfg);
    startWorker(cfg);
    save();
    emit camerasChanged();
    return cfg.id;
}

void CameraManager::updateCamera(const CameraConfig& cfg)
{
    for (CameraConfig& c : m_cams) {
        if (c.id != cfg.id)
            continue;
        const bool sourceChanged = c.type != cfg.type || c.source != cfg.source || c.usbIndex != cfg.usbIndex ||
                                   c.reqWidth != cfg.reqWidth || c.reqHeight != cfg.reqHeight || c.loop != cfg.loop;
        const bool analysisChanged = c.analysisEnabled != cfg.analysisEnabled;
        c = cfg;
        if (sourceChanged) {
            stopWorker(cfg.id);
            startWorker(cfg);
        } else if (CameraWorker* w = worker(cfg.id)) {
            configureWorker(w, cfg);
        }
        if (analysisChanged)
            emit cameraAnalysisChanged(cfg.id, isCameraAnalyzing(cfg.id));
        save();
        emit camerasChanged();
        return;
    }
}

void CameraManager::removeCamera(int id)
{
    stopWorker(id);
    for (int i = 0; i < m_cams.size(); ++i) {
        if (m_cams[i].id == id) {
            m_cams.removeAt(i);
            break;
        }
    }
    m_status.remove(id);
    m_statusMsg.remove(id);
    m_displayWidth.remove(id);
    m_hidden.remove(id);
    m_everOffline.remove(id);
    m_reportedFailure.remove(id);
    save();
    emit cameraRemoved(id);
    emit camerasChanged();
}

void CameraManager::moveCamera(int id, int delta)
{
    for (int i = 0; i < m_cams.size(); ++i) {
        if (m_cams[i].id != id)
            continue;
        const int j = i + delta;
        if (j < 0 || j >= m_cams.size())
            return;
        std::swap(m_cams[i], m_cams[j]);
        save();
        emit camerasChanged();
        return;
    }
}

void CameraManager::reconnect(int id)
{
    const CameraConfig* c = camera(id);
    if (!c)
        return;
    const CameraConfig cfg = *c;
    stopWorker(id);
    m_reportedFailure.remove(id);
    startWorker(cfg);
}

void CameraManager::onWorkerStatus(int id, CameraStatus st, const QString& message)
{
    const CameraConfig* cfg = camera(id);
    if (!cfg)
        return;
    m_status.insert(id, st);
    m_statusMsg.insert(id, message);

    const bool live = cfg->type == SourceType::IpCamera || cfg->type == SourceType::UsbCamera;
    if (isOnline(st)) {
        if (!m_wasOnline.contains(id) && m_everOffline.contains(id))
            raise(id, EventType::CameraOnline, Severity::Info, tr("Video signal restored"));
        m_wasOnline.insert(id);
        m_reportedFailure.remove(id);
    } else if (st == CameraStatus::Offline || st == CameraStatus::Reconnecting) {
        if (m_wasOnline.contains(id)) {
            m_wasOnline.remove(id);
            m_everOffline.insert(id);
            raise(id, EventType::CameraOffline, live ? Severity::Critical : Severity::Warning,
                  tr("Video signal lost%1").arg(message.isEmpty() ? QString() : QStringLiteral(": ") + message));
        } else if (st == CameraStatus::Offline && !m_reportedFailure.contains(id)) {
            m_reportedFailure.insert(id);
            m_everOffline.insert(id);
            raise(id, EventType::CameraOffline, Severity::Warning,
                  tr("Cannot open source: %1").arg(message.isEmpty() ? tr("unknown error") : message));
        }
    }
    emit cameraStatusChanged(id, st, message);
}

void CameraManager::handleAnalysisResult(const AnalysisResult& r)
{
    if (m_state != AnalysisState::Running || !camera(r.cameraId) || !isCameraAnalyzing(r.cameraId))
        return;
    m_lastResults.insert(r.cameraId, r);
    emit analysisResult(r);

    const AppSettings& s = AppSettings::instance();
    for (const EventRecord& e : r.events) {
        emit eventRaised(e);
        if (s.autoRecordSec > 0 && e.severity == Severity::Critical)
            startRecording(e.cameraId, s.autoRecordSec);
    }
}

// ---------------------------------------------------------------------------
// Analysis control
// ---------------------------------------------------------------------------

void CameraManager::startAnalysis()
{
    if (m_state != AnalysisState::Stopped)
        return;
    m_state = AnalysisState::Starting;
    emit analysisStateChanged(m_state, tr("Loading detection model..."));
    if (m_loadWatcher.isRunning()) {
        // A cancelled load is still finishing; load again (with the current
        // settings) as soon as it completes.
        m_restartPending = true;
        return;
    }
    beginModelLoad();
}

void CameraManager::beginModelLoad()
{
    const AppSettings& s = AppSettings::instance();
    DetectorConfig dc;
    dc.modelPath = s.resolvedModelPath();
    dc.useCuda = s.device == 1;
    dc.cudaDeviceId = s.cudaDeviceId;
    dc.inputSize = s.inputSize;
    dc.modelFormat = s.modelFormat;
    dc.intraOpThreads = s.intraOpThreads;
    m_pool.setMaxThreadCount(s.inferenceThreads);

    m_loadWatcher.setFuture(QtConcurrent::run([dc]() {
        QString msg;
        try {
            Detector::instance().load(dc, &msg);
        } catch (const std::exception& e) {
            msg = tr("Detector error: %1").arg(QString::fromUtf8(e.what()));
        } catch (...) {
            msg = tr("Detector error");
        }
        return msg;
    }));
}

void CameraManager::onModelLoaded()
{
    const QString msg = m_loadWatcher.result();
    if (m_state != AnalysisState::Starting) {
        // Stop was pressed while the model was loading.
        m_restartPending = false;
        if (AppSettings::instance().unloadOnStop)
            Detector::instance().unload();
        return;
    }
    if (m_restartPending) {
        // Stop + Start happened during the load: settings may have changed.
        m_restartPending = false;
        beginModelLoad();
        return;
    }
    m_state = AnalysisState::Running;
    m_detectorInfo = Detector::instance().description();
    for (const CameraConfig& c : m_cams)
        if (CameraWorker* w = worker(c.id))
            configureWorker(w, c);
    emit analysisStateChanged(m_state, msg);
    raise(-1, EventType::System, Severity::Info, tr("Analysis started (%1)").arg(m_detectorInfo));
}

void CameraManager::stopAnalysis()
{
    if (m_state == AnalysisState::Stopped)
        return;
    if (m_state == AnalysisState::Starting) {
        m_state = AnalysisState::Stopped;
        m_restartPending = false;
        emit analysisStateChanged(m_state, tr("Analysis cancelled"));
        return;
    }
    m_state = AnalysisState::Stopped;
    // Queued jobs skip their work once analysis is disabled; unload() below
    // waits only for inferences that are already running.
    for (CameraWorker* w : qAsConst(m_workers))
        w->setAnalysisEnabled(false);
    m_lastResults.clear();
    QString msg = tr("Analysis stopped.");
    if (AppSettings::instance().unloadOnStop) {
        Detector::instance().unload();
        msg += QLatin1Char(' ') + tr("Detection model released from memory.");
    }
    emit analysisStateChanged(m_state, msg);
    raise(-1, EventType::System, Severity::Info, msg);
}

bool CameraManager::isCameraAnalyzing(int id) const
{
    const CameraConfig* c = camera(id);
    return m_state == AnalysisState::Running && c && c->analysisEnabled && m_workers.contains(id);
}

void CameraManager::setCameraAnalysis(int id, bool on)
{
    const CameraConfig* c = camera(id);
    if (!c || c->analysisEnabled == on)
        return;
    CameraConfig cfg = *c;
    cfg.analysisEnabled = on;
    if (!on)
        m_lastResults.remove(id);
    updateCamera(cfg);
}

void CameraManager::applySettings()
{
    m_pool.setMaxThreadCount(AppSettings::instance().inferenceThreads);
    for (const CameraConfig& c : m_cams)
        if (CameraWorker* w = worker(c.id))
            configureWorker(w, c);
}

void CameraManager::setDisplayHint(int id, bool visible, int maxWidth)
{
    if (visible)
        m_hidden.remove(id);
    else
        m_hidden.insert(id);
    if (maxWidth > 0)
        m_displayWidth.insert(id, maxWidth);
    if (CameraWorker* w = worker(id))
        w->setDisplay(visible, m_displayWidth.value(id, 960), AppSettings::instance().maxDisplayFps);
}

// ---------------------------------------------------------------------------
// Media
// ---------------------------------------------------------------------------

static QString safeFileName(const QString& s)
{
    QString out = s;
    out.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_-]+")), QStringLiteral("_"));
    return out.left(40);
}

QString CameraManager::takeSnapshot(int id)
{
    CameraWorker* w = worker(id);
    const CameraConfig* cfg = camera(id);
    if (!w || !cfg)
        return QString();
    QImage img = w->latestImage();
    if (img.isNull())
        return QString();

    const AppSettings& s = AppSettings::instance();
    if (isCameraAnalyzing(id) && m_lastResults.contains(id)) {
        const AnalysisResult& r = m_lastResults[id];
        QPainter p(&img);
        OverlayData od;
        od.zones = &cfg->zones;
        od.lines = &cfg->lines;
        od.objects = &r.objects;
        od.zoneStats = &r.zones;
        od.lineStats = &r.lines;
        OverlayOptions oo;
        oo.showIds = s.showIds;
        oo.showTrails = s.fnTracks;
        Overlay::draw(p, QRectF(0, 0, img.width(), img.height()), od, oo);
    }

    const QDateTime now = QDateTime::currentDateTime();
    const QString dir = s.snapshotDir + QLatin1Char('/') + now.toString(QStringLiteral("yyyy-MM-dd"));
    QDir().mkpath(dir);
    const QString path = QStringLiteral("%1/cam%2_%3_manual.jpg")
                             .arg(dir)
                             .arg(id, 2, 10, QLatin1Char('0'))
                             .arg(now.toString(QStringLiteral("HHmmss_zzz")));
    if (!img.save(path, "JPG", 92))
        return QString();
    raise(id, EventType::Snapshot, Severity::Info, tr("Snapshot saved"), path);
    return path;
}

void CameraManager::startRecording(int id, int seconds)
{
    CameraWorker* w = worker(id);
    const CameraConfig* cfg = camera(id);
    if (!w || !cfg || cfg->type == SourceType::Image)
        return;
    const AppSettings& s = AppSettings::instance();
    const QDateTime now = QDateTime::currentDateTime();
    const QString dir = s.recordDir + QLatin1Char('/') + now.toString(QStringLiteral("yyyy-MM-dd"));
    QDir().mkpath(dir);
    const QString path = QStringLiteral("%1/cam%2_%3_%4.%5")
                             .arg(dir)
                             .arg(id, 2, 10, QLatin1Char('0'))
                             .arg(safeFileName(cfg->name))
                             .arg(now.toString(QStringLiteral("HHmmss")))
                             .arg(s.recordFormat == 1 ? QStringLiteral("avi") : QStringLiteral("mp4"));
    w->startRecording(path, seconds);
}

void CameraManager::stopRecording(int id)
{
    if (CameraWorker* w = worker(id))
        w->stopRecording();
}

bool CameraManager::isRecording(int id) const
{
    CameraWorker* w = worker(id);
    return w && w->isRecording();
}

void CameraManager::resetCounters()
{
    for (CameraWorker* w : qAsConst(m_workers))
        w->requestCounterReset();
}

void CameraManager::resetHeatmaps()
{
    for (CameraWorker* w : qAsConst(m_workers))
        w->resetHeatmap();
}

void CameraManager::raise(int cameraId, EventType type, Severity sev, const QString& message, const QString& snapshot)
{
    EventRecord e;
    e.time = QDateTime::currentDateTime();
    e.cameraId = cameraId;
    const CameraConfig* cfg = camera(cameraId);
    e.cameraName = cfg ? cfg->name : tr("System");
    e.type = type;
    e.severity = sev;
    e.message = message;
    e.snapshotPath = snapshot;
    emit eventRaised(e);
}
