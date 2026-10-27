#include "Tracker.h"

#include <algorithm>
#include <cmath>

#include <opencv2/imgproc.hpp>

namespace {

float iou(const cv::Rect2f& a, const cv::Rect2f& b)
{
    const float inter = (a & b).area();
    const float uni = a.area() + b.area() - inter;
    return uni > 0.f ? inter / uni : 0.f;
}

struct Candidate {
    float score;
    int track;
    int det;
};

// Appearance gate: an established track never takes a detection whose
// clothing colours are clearly different (typical when people cross).
constexpr float kMinSimilarity = 0.55f;
constexpr int kAppearanceMinHits = 3;

} // namespace

cv::Mat Tracker::appearanceOf(const cv::Mat& bgr, const cv::Rect2f& box)
{
    // Central 60% of the width and 10%..90% of the height: mostly clothing,
    // little background.
    cv::Rect r(int(box.x + box.width * 0.2f), int(box.y + box.height * 0.1f), int(box.width * 0.6f),
               int(box.height * 0.8f));
    r &= cv::Rect(0, 0, bgr.cols, bgr.rows);
    if (r.width < 4 || r.height < 8)
        return cv::Mat();
    cv::Mat small, hsv;
    cv::resize(bgr(r), small, cv::Size(16, 32), 0, 0, cv::INTER_AREA);
    cv::cvtColor(small, hsv, cv::COLOR_BGR2HSV);
    // 8 hue x 4 saturation x 4 value bins: value bins keep dark clothing apart.
    const int channels[] = { 0, 1, 2 };
    const int bins[] = { 8, 4, 4 };
    const float hr[] = { 0, 180 }, sr[] = { 0, 256 }, vr[] = { 0, 256 };
    const float* ranges[] = { hr, sr, vr };
    cv::Mat hist;
    cv::calcHist(&hsv, 1, channels, cv::Mat(), hist, 3, bins, ranges);
    hist = hist.reshape(1, 1);
    const double sum = cv::sum(hist)[0];
    if (sum <= 0)
        return cv::Mat();
    hist.convertTo(hist, CV_32F, 1.0 / sum);
    return hist;
}

float Tracker::similarity(const cv::Mat& a, const cv::Mat& b)
{
    if (a.empty() || b.empty() || a.total() != b.total())
        return 1.f;
    const float* pa = a.ptr<float>();
    const float* pb = b.ptr<float>();
    double bc = 0;
    for (size_t i = 0; i < a.total(); ++i)
        bc += std::sqrt(double(pa[i]) * pb[i]);
    return float(std::min(1.0, bc));
}

Tracker::Tracker(qint64 maxAgeMs, int minHits, int trailLength)
    : m_maxAgeMs(maxAgeMs), m_minHits(minHits), m_trailLength(trailLength)
{
}

void Tracker::reset()
{
    m_tracks.clear();
    m_nextId = 1;
    m_lastMs = -1;
}

std::vector<int> Tracker::update(const std::vector<TrackerDetection>& dets, qint64 nowMs, float aspect)
{
    const float dt = m_lastMs < 0 ? 0.f : std::clamp(float(nowMs - m_lastMs) / 1000.f, 0.f, 5.f);
    m_lastMs = nowMs;

    // 1. Predict every track forward.
    std::vector<cv::Rect2f> predicted(m_tracks.size());
    for (size_t i = 0; i < m_tracks.size(); ++i) {
        cv::Rect2f p = m_tracks[i].box;
        p.x += m_tracks[i].velocity.x * dt;
        p.y += m_tracks[i].velocity.y * dt;
        predicted[i] = p;
    }

    // 2. Candidate pairs: IoU first, centre distance (in body heights) as a
    //    fallback for fast movers at low analysis rates. Appearance similarity
    //    breaks ties and vetoes matches between clearly different people.
    std::vector<Candidate> cands;
    cands.reserve(m_tracks.size() * 4);
    for (size_t t = 0; t < m_tracks.size(); ++t) {
        const cv::Rect2f& p = predicted[t];
        const cv::Point2f pc(p.x + p.width / 2, p.y + p.height / 2);
        const bool established = m_tracks[t].hits >= kAppearanceMinHits;
        for (size_t d = 0; d < dets.size(); ++d) {
            const float sim = similarity(m_tracks[t].appearance, dets[d].appearance);
            if (established && sim < kMinSimilarity)
                continue;
            const cv::Rect2f& b = dets[d].box;
            const float ov = iou(p, b);
            if (ov > 0.15f) {
                cands.push_back({ 1.f + 0.6f * ov + 0.4f * sim, int(t), int(d) });
                continue;
            }
            const cv::Point2f bc(b.x + b.width / 2, b.y + b.height / 2);
            const float h = std::max(1e-4f, std::max(p.height, b.height));
            const float dx = (pc.x - bc.x) * aspect;
            const float dy = pc.y - bc.y;
            const float dist = std::sqrt(dx * dx + dy * dy) / h;
            const float sizeRatio = std::min(p.height, b.height) / h;
            if (dist < 0.6f && sizeRatio > 0.6f)
                cands.push_back({ 0.6f * (1.f - dist / 0.6f) + 0.4f * sim, int(t), int(d) });
        }
    }
    std::sort(cands.begin(), cands.end(), [](const Candidate& a, const Candidate& b) { return a.score > b.score; });

    std::vector<int> trackMatch(m_tracks.size(), -1);
    std::vector<int> detMatch(dets.size(), -1);
    for (const Candidate& c : cands) {
        if (trackMatch[c.track] >= 0 || detMatch[c.det] >= 0)
            continue;
        trackMatch[c.track] = c.det;
        detMatch[c.det] = c.track;
    }

    // 3. Update matched tracks, age unmatched ones.
    for (size_t t = 0; t < m_tracks.size(); ++t) {
        Track& tr = m_tracks[t];
        if (trackMatch[t] >= 0) {
            const TrackerDetection& d = dets[trackMatch[t]];
            const cv::Point2f oldC = tr.center();
            // Light smoothing against the prediction keeps boxes steady.
            const cv::Rect2f& p = predicted[t];
            const float a = 0.75f;
            tr.box = cv::Rect2f(a * d.box.x + (1 - a) * p.x, a * d.box.y + (1 - a) * p.y,
                                a * d.box.width + (1 - a) * p.width, a * d.box.height + (1 - a) * p.height);
            if (dt > 0.f && dt < 3.f) {
                const cv::Point2f v = (tr.center() - oldC) / dt;
                tr.velocity = tr.velocity * 0.6f + v * 0.4f;
            } else {
                tr.velocity = cv::Point2f(0, 0);
            }
            tr.score = d.score;
            if (!d.appearance.empty()) {
                if (tr.appearance.empty())
                    tr.appearance = d.appearance.clone();
                else
                    cv::addWeighted(tr.appearance, 0.8, d.appearance, 0.2, 0.0, tr.appearance);
            }
            tr.hits++;
            tr.misses = 0;
            tr.lastSeenMs = nowMs;
            if (tr.hits >= m_minHits)
                tr.confirmed = true;
            tr.trail.push_back(tr.foot());
            while (int(tr.trail.size()) > m_trailLength)
                tr.trail.pop_front();
        } else {
            tr.box = predicted[t];
            tr.velocity *= 0.5f;
            tr.misses++;
        }
    }

    // 4. Drop stale tracks.
    std::vector<int> removed;
    m_tracks.erase(std::remove_if(m_tracks.begin(), m_tracks.end(),
                                  [&](const Track& tr) {
                                      const bool stale = (nowMs - tr.lastSeenMs) > m_maxAgeMs ||
                                                         (!tr.confirmed && tr.misses > 1);
                                      if (stale)
                                          removed.push_back(tr.id);
                                      return stale;
                                  }),
                   m_tracks.end());

    // 5. New tracks for unmatched detections.
    for (size_t d = 0; d < dets.size(); ++d) {
        if (detMatch[d] >= 0)
            continue;
        Track tr;
        tr.id = m_nextId++;
        tr.box = dets[d].box;
        tr.score = dets[d].score;
        tr.appearance = dets[d].appearance.clone();
        tr.hits = 1;
        tr.confirmed = m_minHits <= 1;
        tr.firstSeenMs = nowMs;
        tr.lastSeenMs = nowMs;
        tr.trail.push_back(tr.foot());
        m_tracks.push_back(tr);
    }
    if (m_nextId > 999999)
        m_nextId = 1;
    return removed;
}
