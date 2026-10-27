#pragma once

#include "Types.h"

#include <QFutureWatcher>
#include <QHash>
#include <QImage>
#include <QObject>
#include <QSet>
#include <QThreadPool>

class CameraWorker;

enum class AnalysisState { Stopped = 0, Starting, Running };

// Owns the camera configuration list, one capture worker per camera, the
// shared analysis thread pool and the global Start/Stop Analysis state.
class CameraManager : public QObject {
    Q_OBJECT
public:
    static constexpr int kMaxCameras = 32;

    explicit CameraManager(QObject* parent = nullptr);
    ~CameraManager() override;

    void load();
    void save() const;
    void shutdown();

    const QVector<CameraConfig>& cameras() const { return m_cams; }
    const CameraConfig* camera(int id) const;
    int count() const { return m_cams.size(); }
    QStringList locations() const;

    int addCamera(CameraConfig cfg);              // returns id or -1 when full
    void updateCamera(const CameraConfig& cfg);
    void removeCamera(int id);
    void moveCamera(int id, int delta);
    void reconnect(int id);

    CameraWorker* worker(int id) const { return m_workers.value(id, nullptr); }
    CameraStatus status(int id) const { return m_status.value(id, CameraStatus::Stopped); }
    QString statusMessage(int id) const { return m_statusMsg.value(id); }
    int onlineCount() const;

    // Analysis control
    AnalysisState analysisState() const { return m_state; }
    bool isAnalysisRunning() const { return m_state == AnalysisState::Running; }
    void startAnalysis();
    void stopAnalysis();
    void setCameraAnalysis(int id, bool on);
    bool isCameraAnalyzing(int id) const;
    QString detectorInfo() const { return m_detectorInfo; }
    AnalysisResult lastResult(int id) const { return m_lastResults.value(id); }

    void applySettings();
    void setDisplayHint(int id, bool visible, int maxWidth);

    // Media
    QString takeSnapshot(int id);
    void startRecording(int id, int seconds = 0);
    void stopRecording(int id);
    bool isRecording(int id) const;
    void resetCounters();
    void resetHeatmaps();

    static QString newObjectId();

    // Called (queued) by analysis jobs running on the thread pool.
    Q_INVOKABLE void handleAnalysisResult(const AnalysisResult& r);

signals:
    void camerasChanged();
    void cameraStatusChanged(int id, CameraStatus status, const QString& message);
    void frameReady(int id, const QImage& image);
    void analysisResult(const AnalysisResult& result);
    void analysisStateChanged(AnalysisState state, const QString& message);
    void recordingChanged(int id, bool recording, const QString& info);
    void cameraAnalysisChanged(int id, bool on);
    void cameraRemoved(int id);
    void eventRaised(const EventRecord& e);

private:
    void startWorker(const CameraConfig& cfg);
    void stopWorker(int id);
    void configureWorker(CameraWorker* w, const CameraConfig& cfg);
    void onWorkerStatus(int id, CameraStatus status, const QString& message);
    void onModelLoaded();
    void beginModelLoad();
    void raise(int cameraId, EventType type, Severity sev, const QString& message, const QString& snapshot = QString());
    QString configFile() const;

    QVector<CameraConfig> m_cams;
    QHash<int, CameraWorker*> m_workers;
    QSet<CameraWorker*> m_retiring;   // stopped workers whose thread is still finishing
    QHash<int, CameraStatus> m_status;
    QHash<int, QString> m_statusMsg;
    QHash<int, AnalysisResult> m_lastResults;
    QHash<int, int> m_displayWidth;
    QSet<int> m_hidden;
    QSet<int> m_wasOnline;
    QSet<int> m_everOffline;
    QSet<int> m_reportedFailure;
    int m_nextId = 1;

    QThreadPool m_pool;
    AnalysisState m_state = AnalysisState::Stopped;
    QString m_detectorInfo;
    QFutureWatcher<QString> m_loadWatcher;
    bool m_restartPending = false;    // Start pressed while a cancelled load was still running
};
