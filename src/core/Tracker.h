#pragma once

#include <QtGlobal>

#include <deque>
#include <vector>

#include <opencv2/core.hpp>

// Lightweight multi-object tracker: greedy association on IoU / centre
// distance with constant-velocity prediction, plus an appearance check (colour
// histogram of the clothing) that stops IDs from jumping between people who
// pass each other. Works on normalized [0,1] boxes and is tuned for the low
// analysis rates (1-15 fps) used to keep CPU load down.
struct Track {
    int id = 0;
    cv::Rect2f box;              // normalized
    cv::Point2f velocity;        // centre velocity, normalized units per second
    float score = 0.f;
    int hits = 0;
    int misses = 0;              // consecutive updates without a detection
    bool confirmed = false;
    qint64 firstSeenMs = 0;
    qint64 lastSeenMs = 0;
    std::deque<cv::Point2f> trail;   // foot points, oldest first
    cv::Mat appearance;              // L1-normalised colour histogram (empty = unknown)

    cv::Point2f foot() const { return { box.x + box.width * 0.5f, box.y + box.height }; }
    cv::Point2f center() const { return { box.x + box.width * 0.5f, box.y + box.height * 0.5f }; }
};

struct TrackerDetection {
    cv::Rect2f box;              // normalized
    float score = 0.f;
    cv::Mat appearance;          // optional, see Tracker::appearanceOf()
};

class Tracker {
public:
    Tracker(qint64 maxAgeMs = 1500, int minHits = 2, int trailLength = 24);

    // Updates the tracks and returns the ids of tracks that were deleted.
    std::vector<int> update(const std::vector<TrackerDetection>& dets, qint64 nowMs, float aspect);

    const std::vector<Track>& tracks() const { return m_tracks; }
    void setTrailLength(int n) { m_trailLength = qMax(2, n); }
    void setMaxAge(qint64 ms) { m_maxAgeMs = qMax<qint64>(200, ms); }
    void reset();

    // Colour histogram of the central part of a box (pixel coordinates).
    static cv::Mat appearanceOf(const cv::Mat& bgr, const cv::Rect2f& box);
    // Bhattacharyya coefficient: 1 = identical, 0 = disjoint; 1 when unknown.
    static float similarity(const cv::Mat& a, const cv::Mat& b);

private:
    std::vector<Track> m_tracks;
    int m_nextId = 1;
    qint64 m_lastMs = -1;
    qint64 m_maxAgeMs;
    int m_minHits;
    int m_trailLength;
};
