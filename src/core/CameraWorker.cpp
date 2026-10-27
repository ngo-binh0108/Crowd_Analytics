#include "CameraWorker.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QMutexLocker>
#include <QThreadPool>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

namespace {

cv::Mat readImageFile(const QString& path)
{
    // cv::imread cannot open non-ASCII paths on Windows; decode from memory.
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return cv::Mat();
    const QByteArray bytes = f.readAll();
    if (bytes.isEmpty())
        return cv::Mat();
    const cv::Mat buf(1, bytes.size(), CV_8U, const_cast<char*>(bytes.constData()));
    return cv::imdecode(buf, cv::IMREAD_COLOR);
}

QImage toQImage(const cv::Mat& bgr)
{
    if (bgr.empty())
        return QImage();
    const QImage view(bgr.data, bgr.cols, bgr.rows, int(bgr.step), QImage::Format_BGR888);
    return view.convertToFormat(QImage::Format_RGB32);
}

} // namespace

CameraWorker::CameraWorker(const CameraConfig& cfg, QThreadPool* analysisPool, QObject* resultReceiver,
                           QObject* parent)
    : QThread(parent),
      m_id(cfg.id),
      m_sourceCfg(cfg),
      m_pool(analysisPool),
      m_resultReceiver(resultReceiver),
      m_job(std::make_shared<AnalysisJobState>(cfg.id))
{
    updateConfig(cfg);
}

CameraWorker::~CameraWorker()
{
    stopAndWait();
}

void CameraWorker::updateConfig(const CameraConfig& cfg)
{
    m_job->engine.setCameraInfo(cfg.name, cfg.capacity);
    m_job->engine.setZones(cfg.zones, cfg.lines);
}

void CameraWorker::setParams(const AnalysisParams& params)
{
    m_job->engine.setParams(params);
}

void CameraWorker::setAnalysisEnabled(bool on)
{
    m_job->enabled = on;
    if (!on) {
        QMutexLocker lock(&m_job->boxMutex);
        m_job->personBoxes.clear();
    }
}

void CameraWorker::setDisplay(bool enabled, int maxWidth, int maxFps)
{
    m_displayEnabled = enabled;
    m_displayMaxWidth = qMax(160, maxWidth);
    m_displayMaxFps = qBound(1, maxFps, 60);
}

void CameraWorker::startRecording(const QString& path, int seconds)
{
    QMutexLocker lock(&m_recMutex);
    if (m_recording || m_recStartRequested) {
        if (m_recAuto && seconds > 0) {
            // Another alert during an automatic clip: extend it.
            m_recExtendMs = qMax<qint64>(m_recExtendMs, qint64(seconds) * 1000);
        } else if (m_recAuto && seconds == 0) {
            // Operator pressed Record during an automatic clip: keep recording until stopped.
            m_recAuto = false;
            m_recMakeManual = true;
            m_recExtendMs = 0;
        }
        // A manual recording is never shortened by automatic clips.
        return;
    }
    m_recPath = path;
    m_recStartRequested = true;
    m_recStopRequested = false;
    m_recAuto = seconds > 0;
    m_recMakeManual = false;
    m_recExtendMs = qint64(seconds) * 1000;
}

void CameraWorker::stopRecording()
{
    QMutexLocker lock(&m_recMutex);
    m_recStopRequested = true;
    m_recStartRequested = false;
}

QString CameraWorker::recordingPath() const
{
    QMutexLocker lock(&m_recMutex);
    return m_recPath;
}

cv::Mat CameraWorker::grabFrame() const
{
    QMutexLocker lock(&m_frameMutex);
    return m_lastFrame.clone();
}

QImage CameraWorker::latestImage(int maxWidth) const
{
    cv::Mat f;
    {
        QMutexLocker lock(&m_frameMutex);
        f = m_lastFrame;   // frames are never modified after capture
    }
    if (maxWidth > 0 && f.cols > maxWidth) {
        cv::Mat small;
        cv::resize(f, small, cv::Size(maxWidth, qMax(1, f.rows * maxWidth / f.cols)), 0, 0, cv::INTER_AREA);
        return toQImage(small);
    }
    return toQImage(f);
}

QSize CameraWorker::frameSize() const
{
    QMutexLocker lock(&m_frameMutex);
    return QSize(m_lastFrame.cols, m_lastFrame.rows);
}

void CameraWorker::retire()
{
    // Queued jobs see alive == false and skip their work; a running job keeps
    // the engine alive through its shared_ptr and its result is dropped.
    m_job->alive = false;
    m_job->enabled = false;
    requestInterruption();
}

void CameraWorker::stopAndWait()
{
    retire();
    wait();
}

void CameraWorker::setStatus(CameraStatus s, const QString& msg)
{
    m_status = s;
    emit statusChanged(m_id, s, msg);
}

bool CameraWorker::sleepInterruptible(int ms)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) {
        if (isInterruptionRequested())
            return false;
        QThread::msleep(quint32(qMin<qint64>(50, ms - t.elapsed() + 1)));
    }
    return !isInterruptionRequested();
}

bool CameraWorker::openSource(const CameraConfig& cfg, cv::VideoCapture& cap, cv::Mat& still, double& fps,
                              QString& error)
{
    const QString& source = cfg.source;
    switch (cfg.type) {
    case SourceType::Image:
        still = readImageFile(source);
        if (still.empty()) {
            error = QFileInfo::exists(source) ? tr("Unsupported image format") : tr("Image file not found");
            return false;
        }
        fps = 5.0;
        return true;

    case SourceType::Video:
        if (!QFileInfo::exists(source)) {
            error = tr("Video file not found");
            return false;
        }
        if (!cap.open(source.toUtf8().toStdString(), cv::CAP_FFMPEG) &&
            !cap.open(source.toLocal8Bit().toStdString(), cv::CAP_ANY)) {
            error = tr("Cannot decode video file");
            return false;
        }
        fps = cap.get(cv::CAP_PROP_FPS);
        if (fps < 1.0 || fps > 240.0)
            fps = 25.0;
        return true;

    case SourceType::UsbCamera:
        if (!cap.open(cfg.usbIndex, cv::CAP_DSHOW) && !cap.open(cfg.usbIndex, cv::CAP_MSMF)) {
            error = tr("USB camera #%1 is not available").arg(cfg.usbIndex);
            return false;
        }
        if (cfg.reqWidth > 0 && cfg.reqHeight > 0) {
            cap.set(cv::CAP_PROP_FRAME_WIDTH, cfg.reqWidth);
            cap.set(cv::CAP_PROP_FRAME_HEIGHT, cfg.reqHeight);
        }
        fps = 0.0;   // paced by the device
        return true;

    case SourceType::IpCamera: {
        const std::string url = source.trimmed().toStdString();
        const std::vector<int> params{ cv::CAP_PROP_OPEN_TIMEOUT_MSEC, 8000, cv::CAP_PROP_READ_TIMEOUT_MSEC, 8000 };
        if (!cap.open(url, cv::CAP_FFMPEG, params) && !cap.open(url, cv::CAP_ANY, params)) {
            error = tr("Cannot connect to stream");
            return false;
        }
        fps = 0.0;   // paced by the network stream
        return true;
    }
    }
    return false;
}

void CameraWorker::handleRecording(const cv::Mat& frame, double fps, qint64 nowMs)
{
    // m_writer & co. are only touched from the capture thread.
    QString startPath;
    bool stop = false;
    bool makeManual = false;
    qint64 extend = 0;
    {
        QMutexLocker lock(&m_recMutex);
        if (m_recStartRequested) {
            startPath = m_recPath;
            m_recStartRequested = false;
        }
        stop = m_recStopRequested;
        m_recStopRequested = false;
        extend = m_recExtendMs;
        m_recExtendMs = 0;
        makeManual = m_recMakeManual;
        m_recMakeManual = false;
    }

    if (!startPath.isEmpty() && !m_writer.isOpened()) {
        const bool avi = startPath.endsWith(QLatin1String(".avi"), Qt::CaseInsensitive);
        const int fourcc = avi ? cv::VideoWriter::fourcc('M', 'J', 'P', 'G') : cv::VideoWriter::fourcc('m', 'p', '4', 'v');
        const double outFps = fps > 1.0 ? fps : (m_fps > 1.0 ? double(m_fps) : 25.0);
        m_writerSize = cv::Size(frame.cols, frame.rows);
        if (m_writer.open(startPath.toUtf8().toStdString(), cv::CAP_FFMPEG, fourcc, outFps, m_writerSize, true) ||
            m_writer.open(startPath.toLocal8Bit().toStdString(), fourcc, outFps, m_writerSize, true)) {
            m_recDeadlineMs = extend > 0 ? nowMs + extend : 0;
            extend = 0;
            m_recording = true;
            emit recordingChanged(m_id, true, startPath);
        } else {
            m_recording = false;
            emit recordingChanged(m_id, false, tr("Could not create %1").arg(startPath));
        }
    }

    if (!m_writer.isOpened())
        return;

    if (makeManual)
        m_recDeadlineMs = 0;
    else if (extend > 0 && m_recDeadlineMs > 0)
        m_recDeadlineMs = qMax(m_recDeadlineMs, nowMs + extend);

    if (stop || (m_recDeadlineMs > 0 && nowMs >= m_recDeadlineMs)) {
        m_writer.release();
        m_recording = false;
        m_recDeadlineMs = 0;
        {
            QMutexLocker lock(&m_recMutex);
            m_recAuto = false;
        }
        emit recordingChanged(m_id, false, recordingPath());
        return;
    }

    if (frame.cols == m_writerSize.width && frame.rows == m_writerSize.height) {
        m_writer.write(frame);
    } else {
        cv::Mat resized;
        cv::resize(frame, resized, m_writerSize);
        m_writer.write(resized);
    }
}

void CameraWorker::submitAnalysis(const cv::Mat& frame)
{
    m_job->busy = true;
    const qint64 ts = QDateTime::currentMSecsSinceEpoch();
    // The job captures the shared state, never `this`: the worker may be
    // retired and deleted while the job is still queued. The frame buffer is
    // never written again by the capture loop, so it is shared without a copy.
    const std::shared_ptr<AnalysisJobState> st = m_job;
    QObject* receiver = m_resultReceiver;
    m_pool->start([st, receiver, frame, ts]() {
        if (st->alive && st->enabled) {
            const AnalysisResult r = st->engine.process(frame, ts);
            {
                QMutexLocker lock(&st->boxMutex);
                st->personBoxes.clear();
                for (const TrackedObject& o : r.objects)
                    if (o.cls == ClassPerson)
                        st->personBoxes.push_back(o.box);
            }
            // The receiver (CameraManager) owns the pool and waits for it
            // before it is destroyed, so it is always alive here.
            if (st->alive && st->enabled && receiver)
                QMetaObject::invokeMethod(receiver, "handleAnalysisResult", Qt::QueuedConnection,
                                          Q_ARG(AnalysisResult, r));
        }
        st->busy = false;
    });
}

QImage CameraWorker::makeDisplayImage(const cv::Mat& frame)
{
    cv::Mat disp = frame;
    const int maxW = m_displayMaxWidth;
    if (frame.cols > maxW) {
        const double s = double(maxW) / frame.cols;
        cv::resize(frame, disp, cv::Size(maxW, qMax(1, int(frame.rows * s))), 0, 0, cv::INTER_AREA);
    }
    if (m_privacy && m_job->enabled) {
        QVector<QRectF> boxes;
        {
            QMutexLocker lock(&m_job->boxMutex);
            boxes = m_job->personBoxes;
        }
        if (!boxes.isEmpty()) {
            if (disp.data == frame.data)
                disp = frame.clone();
            const cv::Rect bounds(0, 0, disp.cols, disp.rows);
            for (const QRectF& b : boxes) {
                // Boxes come from the latest analysis step; pad them so a moving
                // person stays covered until the next result arrives.
                const QRectF pb = b.adjusted(-b.width() * 0.12, -b.height() * 0.06, b.width() * 0.12, b.height() * 0.06);
                cv::Rect r(int(pb.x() * disp.cols), int(pb.y() * disp.rows), int(pb.width() * disp.cols),
                           int(pb.height() * disp.rows));
                r &= bounds;
                if (r.width < 4 || r.height < 4)
                    continue;
                cv::Mat roi = disp(r);
                cv::Mat tiny;
                cv::resize(roi, tiny, cv::Size(qMax(1, r.width / 10), qMax(1, r.height / 10)), 0, 0, cv::INTER_AREA);
                cv::resize(tiny, roi, roi.size(), 0, 0, cv::INTER_NEAREST);
            }
        }
    }
    return toQImage(disp);
}

void CameraWorker::run()
{
    cv::VideoCapture cap;
    cv::Mat still;
    bool opened = false;
    int backoffMs = 1000;
    int readFails = 0;
    double srcFps = 0.0;

    QElapsedTimer clock;
    clock.start();
    qint64 nextDueMs = 0;
    qint64 lastDisplayMs = -100000;
    qint64 lastAnalysisMs = -100000;
    qint64 fpsWindowStart = 0;
    int fpsFrames = 0;
    const SourceType type = m_sourceCfg.type;

    while (!isInterruptionRequested()) {
        if (!opened) {
            setStatus(CameraStatus::Connecting);
            QString err;
            opened = openSource(m_sourceCfg, cap, still, srcFps, err);
            if (!opened) {
                setStatus(CameraStatus::Offline, err);
                if (!sleepInterruptible(backoffMs))
                    break;
                backoffMs = qMin(backoffMs * 2, 15000);
                continue;
            }
            backoffMs = 1000;
            readFails = 0;
            setStatus(type == SourceType::Image   ? CameraStatus::Still
                      : type == SourceType::Video ? CameraStatus::Playing
                                                  : CameraStatus::Live);
            nextDueMs = clock.elapsed();
            fpsWindowStart = clock.elapsed();
            fpsFrames = 0;
        }

        cv::Mat frame;   // fresh buffer every iteration (shared with other threads afterwards)
        if (type == SourceType::Image) {
            frame = still;
        } else if (!cap.read(frame) || frame.empty()) {
            if (type == SourceType::Video) {
                if (m_sourceCfg.loop) {
                    cap.set(cv::CAP_PROP_POS_FRAMES, 0);
                    if (!cap.read(frame) || frame.empty()) {
                        cap.release();
                        opened = false;
                        continue;
                    }
                } else {
                    setStatus(CameraStatus::Ended);
                    while (!isInterruptionRequested())
                        QThread::msleep(100);
                    break;
                }
            } else {
                if (++readFails >= 3) {
                    cap.release();
                    opened = false;
                    setStatus(CameraStatus::Reconnecting, tr("Stream interrupted"));
                    if (!sleepInterruptible(1000))
                        break;
                }
                continue;
            }
        }
        readFails = 0;
        const qint64 now = clock.elapsed();

        {
            QMutexLocker lock(&m_frameMutex);
            m_lastFrame = frame;
        }

        if (type != SourceType::Image)
            handleRecording(frame, srcFps, now);

        if (m_job->enabled && !m_job->busy && m_pool && now - lastAnalysisMs >= m_analysisIntervalMs) {
            lastAnalysisMs = now;
            submitAnalysis(frame);
        }

        // Back-pressure: at most one display frame in flight, so a busy GUI
        // thread never accumulates a backlog of images.
        if (m_displayEnabled && !m_framePending && now - lastDisplayMs >= 1000 / m_displayMaxFps) {
            lastDisplayMs = now;
            m_framePending = true;
            emit frameReady(m_id, makeDisplayImage(frame));
        }

        ++fpsFrames;
        if (now - fpsWindowStart >= 1000) {
            m_fps = fpsFrames * 1000.0 / double(now - fpsWindowStart);
            fpsFrames = 0;
            fpsWindowStart = now;
        }

        // Pace files and still images; live devices pace themselves.
        const int intervalMs = type == SourceType::Image   ? 200
                               : type == SourceType::Video ? int(1000.0 / srcFps)
                                                           : 0;
        if (intervalMs > 0) {
            nextDueMs += intervalMs;
            const qint64 wait = nextDueMs - clock.elapsed();
            if (wait > 0) {
                if (!sleepInterruptible(int(wait)))
                    break;
            } else if (wait < -500) {
                nextDueMs = clock.elapsed();   // fell behind (slow decode) - resync
            }
        }
    }

    {
        QMutexLocker lock(&m_recMutex);
        m_recStartRequested = false;
        m_recStopRequested = true;
    }
    if (!m_lastFrame.empty())
        handleRecording(m_lastFrame, srcFps, clock.elapsed());
    cap.release();
    setStatus(CameraStatus::Stopped);
}

bool CameraWorker::probe(const CameraConfig& cfg, QImage* preview, QString* info)
{
    cv::VideoCapture cap;
    cv::Mat still;
    double fps = 0;
    QString err;
    if (!openSource(cfg, cap, still, fps, err)) {
        if (info)
            *info = err;
        return false;
    }
    cv::Mat frame = still;
    if (frame.empty()) {
        for (int i = 0; i < 5 && frame.empty(); ++i)
            cap.read(frame);
    }
    if (frame.empty()) {
        if (info)
            *info = tr("Connected but no frames were received");
        return false;
    }
    if (preview)
        *preview = toQImage(frame);
    if (info) {
        const double reported = cap.isOpened() ? cap.get(cv::CAP_PROP_FPS) : fps;
        *info = tr("%1 x %2").arg(frame.cols).arg(frame.rows);
        if (reported > 0 && reported < 240)
            *info += tr(" @ %1 fps").arg(reported, 0, 'f', 1);
    }
    return true;
}
