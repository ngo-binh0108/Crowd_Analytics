#pragma once

#include <QReadWriteLock>
#include <QString>

#include <memory>
#include <string>
#include <vector>

#include <opencv2/core.hpp>

namespace Ort {
struct Env;
struct Session;
}

struct Detection {
    cv::Rect2f box;   // pixels in the source frame
    float score = 0.f;
    int cls = 0;      // ObjectClass
};

struct DetectorConfig {
    QString modelPath;
    bool useCuda = false;
    int cudaDeviceId = 0;
    int inputSize = 640;
    int modelFormat = 0;      // 0 auto, 1 YOLOv5, 2 YOLOv8/11, 3 end-to-end
    int intraOpThreads = 2;
};

// Shared people/object detector. The ONNX session is created lazily when the
// operator starts analysis and released again when analysis stops, so an idle
// system only pays for video decoding. detect() may be called concurrently
// from several analysis threads.
class Detector {
public:
    static Detector& instance();

    bool load(const DetectorConfig& cfg, QString* message);
    void unload();
    bool isLoaded() const;
    QString description() const;

    bool detect(const cv::Mat& bgr, float confThreshold, float nmsThreshold, bool wantBags,
                std::vector<Detection>& out);

    static QString libraryVersions();   // "OpenCV x.y.z, ONNX Runtime a.b.c"

private:
    Detector();
    ~Detector();
    Detector(const Detector&) = delete;
    Detector& operator=(const Detector&) = delete;

    enum class Layout { ChannelsFirst, Rows, RowsWithObjectness, EndToEnd };

    bool runOnnx(const cv::Mat& bgr, float conf, float nms, bool wantBags, std::vector<Detection>& out);
    bool runHog(const cv::Mat& bgr, float conf, std::vector<Detection>& out);
    static int mapClass(int modelClass, int numClasses, bool wantBags);

    mutable QReadWriteLock m_lock;
    std::unique_ptr<Ort::Env> m_env;
    std::unique_ptr<Ort::Session> m_session;
    std::string m_inputName;
    std::string m_outputName;
    int m_inW = 640;
    int m_inH = 640;
    int m_format = 0;
    bool m_loaded = false;
    bool m_useHog = false;
    QString m_description;
};
