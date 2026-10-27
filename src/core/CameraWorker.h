#pragma once

#include "AnalysisEngine.h"
#include "Types.h"

#include <QImage>
#include <QMutex>
#include <QSize>
#include <QThread>

#include <atomic>
#include <memory>

#include <opencv2/core.hpp>
#include <opencv2/videoio.hpp>

class QThreadPool;

// State shared between a capture worker and its analysis jobs. Jobs hold a
// shared_ptr, so the engine outlives the worker if a job is still queued or
// running when the camera is removed or reconnected.
struct AnalysisJobState {
    explicit AnalysisJobState(int cameraId) : engine(cameraId) {}
    AnalysisEngine engine;
    QMutex boxMutex;
    QVector<QRectF> personBoxes;          // latest person boxes, for the privacy mask
    std::atomic<bool> busy{ false };      // one job per camera at a time
    std::atomic<bool> enabled{ false };   // analysis switched on for this camera
    std::atomic<bool> alive{ true };      // false once the worker is retired
};

// Capture thread for one video source. Frames are always decoded and shown;
// they are only handed to the analysis pool while analysis is enabled, which
// keeps an idle system cheap. All public setters are thread-safe.
//
// Analysis results are delivered by invoking
// `resultReceiver->handleAnalysisResult(AnalysisResult)` (queued), never via
// this object, so a retired worker can be deleted while jobs are pending.
class CameraWorker : public QThread {
    Q_OBJECT
public:
    CameraWorker(const CameraConfig& cfg, QThreadPool* analysisPool, QObject* resultReceiver,
                 QObject* parent = nullptr);
    ~CameraWorker() override;

    int cameraId() const { return m_id; }
    void updateConfig(const CameraConfig& cfg);   // name, zones, capacity (not the source)
    void setParams(const AnalysisParams& params);

    void setAnalysisEnabled(bool on);
    bool analysisEnabled() const { return m_job->enabled; }
    void setAnalysisIntervalMs(int ms) { m_analysisIntervalMs = qMax(30, ms); }
    void setDisplay(bool enabled, int maxWidth, int maxFps);
    void setPrivacyMask(bool on) { m_privacy = on; }
    void frameConsumed() { m_framePending = false; }

    // seconds > 0 records an automatic clip (extended by further calls);
    // seconds == 0 records until stopRecording().
    void startRecording(const QString& path, int seconds = 0);
    void stopRecording();
    bool isRecording() const { return m_recording; }
    QString recordingPath() const;

    void requestCounterReset() { m_job->engine.requestCounterReset(); }
    void resetHeatmap() { m_job->engine.resetHeatmap(); }
    QImage heatmapImage() const { return m_job->engine.heatmapImage(); }

    cv::Mat grabFrame() const;                   // latest full-resolution frame (deep copy)
    QImage latestImage(int maxWidth = 0) const;  // latest frame as QImage, optionally down-scaled
    CameraStatus status() const { return m_status; }
    double measuredFps() const { return m_fps; }
    QSize frameSize() const;

    void retire();          // non-blocking stop request (analysis off, thread interrupted)
    void stopAndWait();     // blocking stop

    // Opens a source briefly and returns one frame (used by "Test connection").
    static bool probe(const CameraConfig& cfg, QImage* preview, QString* info);

signals:
    void frameReady(int cameraId, const QImage& image);
    void statusChanged(int cameraId, CameraStatus status, const QString& message);
    void recordingChanged(int cameraId, bool recording, const QString& pathOrError);

protected:
    void run() override;

private:
    static bool openSource(const CameraConfig& cfg, cv::VideoCapture& cap, cv::Mat& still, double& fps,
                           QString& error);
    void setStatus(CameraStatus s, const QString& msg = QString());
    void handleRecording(const cv::Mat& frame, double fps, qint64 nowMs);
    void submitAnalysis(const cv::Mat& frame);
    QImage makeDisplayImage(const cv::Mat& frame);
    bool sleepInterruptible(int ms);

    const int m_id;
    const CameraConfig m_sourceCfg;    // source fields are fixed for the worker's lifetime

    QThreadPool* m_pool;
    QObject* m_resultReceiver;
    std::shared_ptr<AnalysisJobState> m_job;

    std::atomic<int> m_analysisIntervalMs{ 125 };
    std::atomic<bool> m_displayEnabled{ true };
    std::atomic<bool> m_framePending{ false };  // a display frame is queued and not yet shown
    std::atomic<int> m_displayMaxWidth{ 960 };
    std::atomic<int> m_displayMaxFps{ 25 };
    std::atomic<bool> m_privacy{ false };
    std::atomic<CameraStatus> m_status{ CameraStatus::Stopped };
    std::atomic<double> m_fps{ 0.0 };

    mutable QMutex m_frameMutex;
    cv::Mat m_lastFrame;

    // Recording requests (UI thread) -> capture thread
    mutable QMutex m_recMutex;
    QString m_recPath;
    bool m_recStartRequested = false;
    bool m_recStopRequested = false;
    bool m_recAuto = false;             // current / pending recording is an automatic clip
    bool m_recMakeManual = false;       // operator took over an automatic clip
    qint64 m_recExtendMs = 0;           // requested clip length / extension
    std::atomic<bool> m_recording{ false };

    // Capture thread only
    cv::VideoWriter m_writer;
    cv::Size m_writerSize;
    qint64 m_recDeadlineMs = 0;         // 0 = record until stopped
};
