#include "Detector.h"
#include "Types.h"

#include <QDir>
#include <QFileInfo>
#include <QObject>

#include <algorithm>
#include <array>
#include <cmath>

// ONNX Runtime is initialised on first use rather than at program start: if
// an incompatible onnxruntime.dll gets loaded (Windows ships an older one in
// System32), the application still starts and falls back to HOG.
#define ORT_API_MANUAL_INIT
#include <onnxruntime_cxx_api.h>
#undef ORT_API_MANUAL_INIT
#include <opencv2/dnn.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/objdetect.hpp>

namespace {

// COCO ids of the classes the system cares about.
constexpr int kCocoPerson = 0;
constexpr int kCocoBackpack = 24;
constexpr int kCocoHandbag = 26;
constexpr int kCocoSuitcase = 28;

struct Letterbox {
    float scale = 1.f;
    int padX = 0;
    int padY = 0;
};

cv::Mat letterbox(const cv::Mat& src, int dstW, int dstH, Letterbox& lb)
{
    lb.scale = std::min(dstW / float(src.cols), dstH / float(src.rows));
    const int nw = std::max(1, int(std::round(src.cols * lb.scale)));
    const int nh = std::max(1, int(std::round(src.rows * lb.scale)));
    lb.padX = (dstW - nw) / 2;
    lb.padY = (dstH - nh) / 2;
    cv::Mat canvas(dstH, dstW, CV_8UC3, cv::Scalar(114, 114, 114));
    cv::Mat resized;
    cv::resize(src, resized, cv::Size(nw, nh), 0, 0, cv::INTER_LINEAR);
    resized.copyTo(canvas(cv::Rect(lb.padX, lb.padY, nw, nh)));
    return canvas;
}

cv::Rect2f unletterbox(float x1, float y1, float x2, float y2, const Letterbox& lb, int w, int h)
{
    x1 = (x1 - lb.padX) / lb.scale;
    x2 = (x2 - lb.padX) / lb.scale;
    y1 = (y1 - lb.padY) / lb.scale;
    y2 = (y2 - lb.padY) / lb.scale;
    x1 = std::clamp(x1, 0.f, float(w - 1));
    x2 = std::clamp(x2, 0.f, float(w - 1));
    y1 = std::clamp(y1, 0.f, float(h - 1));
    y2 = std::clamp(y2, 0.f, float(h - 1));
    return cv::Rect2f(x1, y1, std::max(0.f, x2 - x1), std::max(0.f, y2 - y1));
}

// Returns true when the loaded onnxruntime.dll provides the API version these
// headers were built for; `found` receives the DLL's version string.
bool ensureOrtApi(QString* found)
{
    static const OrtApi* api = nullptr;
    static bool tried = false;
    static QString version;
    if (!tried) {
        tried = true;
        const OrtApiBase* base = OrtGetApiBase();
        version = base ? QString::fromLatin1(base->GetVersionString()) : QStringLiteral("?");
        api = base ? base->GetApi(ORT_API_VERSION) : nullptr;
        if (api)
            Ort::InitApi(api);
    }
    if (found)
        *found = version;
    return api != nullptr;
}

} // namespace

Detector& Detector::instance()
{
    static Detector d;
    return d;
}

Detector::Detector() = default;
Detector::~Detector() = default;

QString Detector::libraryVersions()
{
    QString ort;
    const bool ok = ensureOrtApi(&ort);
    return QStringLiteral("OpenCV %1, ONNX Runtime %2%3")
        .arg(QString::fromLatin1(CV_VERSION), ort, ok ? QString() : QObject::tr(" (incompatible)"));
}

bool Detector::isLoaded() const
{
    QReadLocker lock(&m_lock);
    return m_loaded;
}

QString Detector::description() const
{
    QReadLocker lock(&m_lock);
    return m_loaded ? m_description : QObject::tr("Not loaded");
}

bool Detector::load(const DetectorConfig& cfg, QString* message)
{
    QWriteLocker lock(&m_lock);
    m_session.reset();
    m_loaded = false;
    m_useHog = false;
    m_format = cfg.modelFormat;
    QString info;

    const QFileInfo fi(cfg.modelPath);
    if (cfg.modelPath.isEmpty() || !fi.exists()) {
        m_useHog = true;
        m_loaded = true;
        m_description = QObject::tr("OpenCV HOG (fallback)");
        if (message)
            *message = QObject::tr("Model file not found: %1. Using the built-in OpenCV HOG people detector "
                                   "(lower accuracy). Place a YOLO ONNX model there for best results.")
                           .arg(QDir::toNativeSeparators(cfg.modelPath));
        return true;
    }

    QString ortVersion;
    if (!ensureOrtApi(&ortVersion)) {
        m_useHog = true;
        m_loaded = true;
        m_description = QObject::tr("OpenCV HOG (fallback)");
        if (message)
            *message = QObject::tr("The onnxruntime.dll that was loaded (version %1) is too old; version 1.%2 or newer "
                                   "must be next to SurveillanceSystem.exe. Using the OpenCV HOG people detector.")
                           .arg(ortVersion)
                           .arg(ORT_API_VERSION);
        return true;
    }

    bool ok = false;
    try {
        if (!m_env)
            m_env = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "SurveillanceSystem");

        Ort::SessionOptions so;
        so.SetIntraOpNumThreads(std::max(1, cfg.intraOpThreads));
        so.SetInterOpNumThreads(1);
        so.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

        bool cuda = false;
        if (cfg.useCuda) {
            try {
                OrtCUDAProviderOptions cudaOpts;
                cudaOpts.device_id = cfg.cudaDeviceId;
                so.AppendExecutionProvider_CUDA(cudaOpts);
                cuda = true;
            } catch (const Ort::Exception& e) {
                info = QObject::tr("CUDA is not available (%1). Falling back to CPU. ").arg(QString::fromUtf8(e.what()));
            }
        }

        const std::wstring path = QDir::toNativeSeparators(fi.absoluteFilePath()).toStdWString();
        m_session = std::make_unique<Ort::Session>(*m_env, path.c_str(), so);

        if (m_session->GetInputCount() < 1 || m_session->GetOutputCount() < 1)
            throw std::runtime_error("model must have at least one input and one output");

        Ort::AllocatorWithDefaultOptions alloc;
        m_inputName = m_session->GetInputNameAllocated(0, alloc).get();
        m_outputName = m_session->GetOutputNameAllocated(0, alloc).get();
        Ort::TypeInfo outType = m_session->GetOutputTypeInfo(0);
        if (outType.GetTensorTypeAndShapeInfo().GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT)
            throw std::runtime_error("the first model output must be a float32 detection tensor");

        Ort::TypeInfo inType = m_session->GetInputTypeInfo(0);
        auto inInfo = inType.GetTensorTypeAndShapeInfo();
        if (inInfo.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT)
            throw std::runtime_error("only float32 input models are supported (export with half=False)");
        const std::vector<int64_t> inShape = inInfo.GetShape();
        if (inShape.size() != 4)
            throw std::runtime_error("expected an NCHW image input");
        m_inH = inShape[2] > 0 ? int(inShape[2]) : cfg.inputSize;
        m_inW = inShape[3] > 0 ? int(inShape[3]) : cfg.inputSize;

        m_loaded = true;
        m_description = QStringLiteral("%1 (%2, %3x%4)")
                            .arg(fi.completeBaseName(), cuda ? QStringLiteral("CUDA") : QStringLiteral("CPU"))
                            .arg(m_inW)
                            .arg(m_inH);
        ok = true;
    } catch (const std::exception& e) {
        m_session.reset();
        m_useHog = true;
        m_loaded = true;
        m_description = QObject::tr("OpenCV HOG (fallback)");
        if (message)
            *message = info + QObject::tr("Failed to load %1: %2. Using the OpenCV HOG people detector instead.")
                                  .arg(fi.fileName(), QString::fromUtf8(e.what()));
    }
    lock.unlock();

    if (ok) {
        // Warm-up so the first real frame does not stall (CUDA kernels, arena).
        std::vector<Detection> dummy;
        const cv::Mat blank(m_inH, m_inW, CV_8UC3, cv::Scalar(0, 0, 0));
        detect(blank, 0.9f, 0.5f, false, dummy);
        if (message)
            *message = info + QObject::tr("Model loaded: %1").arg(description());
    }
    return true;
}

void Detector::unload()
{
    QWriteLocker lock(&m_lock);
    m_session.reset();
    m_loaded = false;
    m_useHog = false;
}

int Detector::mapClass(int modelClass, int numClasses, bool wantBags)
{
    if (numClasses < 80)                // custom/person-only model: class 0 is a person
        return modelClass == 0 ? ClassPerson : -1;
    if (modelClass == kCocoPerson)
        return ClassPerson;
    if (wantBags && (modelClass == kCocoBackpack || modelClass == kCocoHandbag || modelClass == kCocoSuitcase))
        return ClassBag;
    return -1;
}

bool Detector::detect(const cv::Mat& bgr, float conf, float nms, bool wantBags, std::vector<Detection>& out)
{
    out.clear();
    if (bgr.empty())
        return false;
    QReadLocker lock(&m_lock);
    if (!m_loaded)
        return false;
    try {
        if (m_useHog)
            return runHog(bgr, conf, out);
        return runOnnx(bgr, conf, nms, wantBags, out);
    } catch (const std::exception&) {
        // e.g. Ort::Exception or cv::Exception on an unexpected model/frame
        out.clear();
        return false;
    }
}

bool Detector::runOnnx(const cv::Mat& bgr, float conf, float nms, bool wantBags, std::vector<Detection>& out)
{
    Letterbox lb;
    const cv::Mat input = letterbox(bgr, m_inW, m_inH, lb);
    const cv::Mat blob = cv::dnn::blobFromImage(input, 1.0 / 255.0, cv::Size(), cv::Scalar(), true, false, CV_32F);

    const std::array<int64_t, 4> shape{ 1, 3, m_inH, m_inW };
    Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    Ort::Value tensor = Ort::Value::CreateTensor<float>(mem, const_cast<float*>(blob.ptr<float>()),
                                                        size_t(blob.total()), shape.data(), shape.size());

    const char* inNames[] = { m_inputName.c_str() };
    const char* outNames[] = { m_outputName.c_str() };
    std::vector<Ort::Value> outputs = m_session->Run(Ort::RunOptions{ nullptr }, inNames, &tensor, 1, outNames, 1);
    if (outputs.empty() || !outputs[0].IsTensor())
        return false;

    const float* data = outputs[0].GetTensorData<float>();
    std::vector<int64_t> os = outputs[0].GetTensorTypeAndShapeInfo().GetShape();
    while (os.size() > 2 && os.front() == 1)
        os.erase(os.begin());
    if (os.size() != 2)
        return false;
    const int d0 = int(os[0]);
    const int d1 = int(os[1]);

    Layout layout;
    switch (m_format) {
    case 1: layout = Layout::RowsWithObjectness; break;
    case 2: layout = d0 < d1 ? Layout::ChannelsFirst : Layout::Rows; break;
    case 3: layout = Layout::EndToEnd; break;
    default:
        if (d1 == 6 && d0 <= 1000)
            layout = Layout::EndToEnd;
        else if (d0 < d1)
            layout = Layout::ChannelsFirst;             // YOLOv8/v11: [4+nc, N]
        else if (d1 == 85 || d1 == 6)
            layout = Layout::RowsWithObjectness;        // YOLOv5: [N, 5+nc]
        else
            layout = Layout::Rows;                      // YOLOv8 transposed: [N, 4+nc]
        break;
    }

    // Reject shapes that do not fit the chosen layout (e.g. a forced format).
    switch (layout) {
    case Layout::ChannelsFirst:      if (d0 < 5) return false; break;
    case Layout::Rows:               if (d1 < 5) return false; break;
    case Layout::RowsWithObjectness: if (d1 < 6) return false; break;
    case Layout::EndToEnd:           if (d1 < 6) return false; break;
    }

    std::vector<cv::Rect2d> boxes[2];
    std::vector<float> scores[2];
    const int W = bgr.cols;
    const int H = bgr.rows;

    auto push = [&](int cls, float score, float x1, float y1, float x2, float y2) {
        const cv::Rect2f r = unletterbox(x1, y1, x2, y2, lb, W, H);
        if (r.width < 2 || r.height < 2)
            return;
        boxes[cls].emplace_back(r.x, r.y, r.width, r.height);
        scores[cls].push_back(score);
    };

    if (layout == Layout::ChannelsFirst) {
        const int n = d1;
        const int numClasses = d0 - 4;
        for (int i = 0; i < n; ++i) {
            int bestCls = -1;
            float best = conf;
            for (int c = 0; c < numClasses; ++c) {
                const float s = data[(4 + c) * n + i];
                if (s > best && mapClass(c, numClasses, wantBags) >= 0) {
                    best = s;
                    bestCls = c;
                }
            }
            if (bestCls < 0)
                continue;
            const float cx = data[i], cy = data[n + i], w = data[2 * n + i], h = data[3 * n + i];
            push(mapClass(bestCls, numClasses, wantBags), best, cx - w / 2, cy - h / 2, cx + w / 2, cy + h / 2);
        }
    } else if (layout == Layout::Rows || layout == Layout::RowsWithObjectness) {
        const bool obj = layout == Layout::RowsWithObjectness;
        const int cols = d1;
        const int first = obj ? 5 : 4;
        const int numClasses = cols - first;
        for (int i = 0; i < d0; ++i) {
            const float* row = data + size_t(i) * cols;
            const float objectness = obj ? row[4] : 1.f;
            if (objectness < conf)
                continue;
            int bestCls = -1;
            float best = conf;
            for (int c = 0; c < numClasses; ++c) {
                const float s = row[first + c] * objectness;
                if (s > best && mapClass(c, numClasses, wantBags) >= 0) {
                    best = s;
                    bestCls = c;
                }
            }
            if (bestCls < 0)
                continue;
            push(mapClass(bestCls, numClasses, wantBags), best, row[0] - row[2] / 2, row[1] - row[3] / 2, row[0] + row[2] / 2,
                 row[1] + row[3] / 2);
        }
    } else { // EndToEnd: [N, 6] = x1 y1 x2 y2 score cls
        for (int i = 0; i < d0; ++i) {
            const float* row = data + size_t(i) * d1;
            if (row[4] < conf)
                continue;
            const int cls = mapClass(int(row[5]), 80, wantBags);
            if (cls < 0)
                continue;
            push(cls, row[4], row[0], row[1], row[2], row[3]);
        }
    }

    for (int cls = 0; cls < 2; ++cls) {
        if (boxes[cls].empty())
            continue;
        std::vector<int> keep;
        if (layout == Layout::EndToEnd) {
            keep.resize(boxes[cls].size());
            for (size_t i = 0; i < keep.size(); ++i)
                keep[i] = int(i);
        } else {
            cv::dnn::NMSBoxes(boxes[cls], scores[cls], conf, nms, keep);
        }
        for (int k : keep) {
            const cv::Rect2d& b = boxes[cls][k];
            out.push_back({ cv::Rect2f(float(b.x), float(b.y), float(b.width), float(b.height)), scores[cls][k], cls });
        }
    }
    return true;
}

bool Detector::runHog(const cv::Mat& bgr, float conf, std::vector<Detection>& out)
{
    // HOGDescriptor keeps no per-call state we rely on, but a thread-local
    // instance avoids any doubt when several cameras are analysed at once.
    thread_local cv::HOGDescriptor hog = [] {
        cv::HOGDescriptor h;
        h.setSVMDetector(cv::HOGDescriptor::getDefaultPeopleDetector());
        return h;
    }();

    const double scale = bgr.cols > 800 ? 800.0 / bgr.cols : 1.0;
    cv::Mat small;
    if (scale < 1.0)
        cv::resize(bgr, small, cv::Size(), scale, scale, cv::INTER_AREA);
    else
        small = bgr;

    std::vector<cv::Rect> found;
    std::vector<double> weights;
    hog.detectMultiScale(small, found, weights, 0.0, cv::Size(8, 8), cv::Size(16, 16), 1.05, 2.0, false);

    // SVM weights are unbounded; squash to a 0..1 confidence.
    const float minScore = std::min(conf, 0.5f);
    for (size_t i = 0; i < found.size(); ++i) {
        const float s = float(1.0 / (1.0 + std::exp(-(i < weights.size() ? weights[i] : 0.0))));
        if (s < minScore)
            continue;
        cv::Rect r = found[i];
        // HOG boxes are padded; tighten them a little.
        r.x += int(r.width * 0.1);
        r.width = int(r.width * 0.8);
        r.y += int(r.height * 0.05);
        r.height = int(r.height * 0.85);
        out.push_back({ cv::Rect2f(float(r.x / scale), float(r.y / scale), float(r.width / scale),
                                   float(r.height / scale)),
                        s, ClassPerson });
    }
    return true;
}
