// Deterministic scenario test for AnalysisEngine::analyze (tracking, counting
// lines, zones, dwell and security rules) with scripted detections.
#include "core/AnalysisEngine.h"

#include <QCoreApplication>
#include <opencv2/imgproc.hpp>
#include <cmath>
#include <cstdio>
#include <random>

static int failures = 0;
void check(bool ok, const char* what)
{
    std::printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok)
        ++failures;
}

static Detection person(double cx, double footY, double w = 0.06, double h = 0.35)
{
    Detection d;
    d.box = cv::Rect2f(float((cx - w / 2) * 1280), float((footY - h) * 720), float(w * 1280), float(h * 720));
    d.score = 0.9f;
    d.cls = ClassPerson;
    return d;
}

static Zone zone(const char* id, const char* name, double x0, double y0, double x1, double y1, ZoneType type, int cap,
                 int loiter = 0)
{
    Zone z;
    z.id = QString::fromLatin1(id);
    z.name = QString::fromLatin1(name);
    z.type = type;
    z.polygon << QPointF(x0, y0) << QPointF(x1, y0) << QPointF(x1, y1) << QPointF(x0, y1);
    z.capacity = cap;
    z.loiterSec = loiter;
    return z;
}

// Scenario 2: a person in blue walks across the counting line and passes in
// front of a person in red standing just left of it. Like a real detector,
// the boxes jitter, analysis intervals are irregular (100-500 ms) and while
// the two overlap the detector reports either only the front person or one
// merged box. Run with 20 random seeds: every run must count exactly one
// crossing and the person in red must keep the same ID (no ID swap).
static bool crossingRun(unsigned seed, int& inOut, int& outOut)
{
    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> jit(-0.004, 0.004);
    std::uniform_int_distribution<int> gap(1, 5);     // x 100 ms
    std::bernoulli_distribution merged(0.5);

    AnalysisEngine engine(2);
    AnalysisParams p;
    p.autoSnapshot = false;
    p.fnTamper = false;
    p.analysisIntervalMs = 300;
    engine.setParams(p);
    CountLine line;
    line.id = QStringLiteral("line");
    line.name = QStringLiteral("Gate");
    line.a = QPointF(0.5, 0.2);
    line.b = QPointF(0.5, 1.0);
    line.invert = true;   // IN = moving right
    engine.setZones({}, { line });

    const double redX = 0.46;
    auto idNear = [](const AnalysisResult& res, double x) {
        for (const TrackedObject& o : res.objects)
            if (o.cls == ClassPerson && std::abs(o.box.center().x() - x) < 0.03)
                return o.id;
        return -1;
    };
    qint64 now = 5000000;
    double blueX = 0.15;
    AnalysisResult r;
    int redIdStart = -1;
    for (int step = 0; blueX < 0.85; ++step) {
        const Detection red = person(redX + jit(rng), 0.92 + jit(rng));
        const Detection blue = person(blueX + jit(rng), 0.94 + jit(rng));
        cv::Mat frame(720, 1280, CV_8UC3, cv::Scalar(90, 90, 90));
        cv::rectangle(frame, cv::Rect(red.box), cv::Scalar(40, 40, 220), cv::FILLED);    // behind
        cv::rectangle(frame, cv::Rect(blue.box), cv::Scalar(220, 90, 30), cv::FILLED);   // in front
        std::vector<Detection> dets;
        if (std::abs(blueX - redX) < 0.06) {
            if (merged(rng)) {
                Detection m = blue;
                m.box = red.box | blue.box;   // one box around both people
                dets.push_back(m);
            } else {
                dets.push_back(blue);         // red hidden behind blue
            }
        } else {
            dets = { red, blue };
        }
        r = engine.analyze(frame, dets, now);
        if (step == 2)
            redIdStart = idNear(r, redX);
        const int g = gap(rng);
        now += 100 * g;
        blueX += 0.02 * g;                    // ~1 body height per second
    }
    const int redIdEnd = idNear(r, redX);
    inOut = int(r.lines.at(0).in);
    outOut = int(r.lines.at(0).out);
    return inOut == 1 && outOut == 0 && redIdStart > 0 && redIdStart == redIdEnd;
}

static void crossingScenario()
{
    int ok = 0;
    for (unsigned seed = 1; seed <= 20; ++seed) {
        int in = 0, out = 0;
        if (crossingRun(seed, in, out))
            ++ok;
        else
            std::printf("  seed %2u: IN=%d OUT=%d (expected IN=1 OUT=0, same ID)\n", seed, in, out);
    }
    std::printf("crossing: %d / 20 randomized runs exact\n", ok);
    check(ok == 20, "crossing: person passing in front of another counted exactly once, no ID swap (20 runs)");
}

void userTests();   // user_test.cpp

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    registerMetaTypes();

    AnalysisEngine engine(1);
    AnalysisParams p;
    p.autoSnapshot = false;
    p.fnTamper = false;
    p.analysisIntervalMs = 125;
    engine.setParams(p);

    CountLine line;
    line.id = QStringLiteral("line");
    line.name = QStringLiteral("Door");
    line.a = QPointF(0.5, 0.2);
    line.b = QPointF(0.5, 1.0);   // normal points left: IN = right->left, OUT = left->right
    engine.setZones({ zone("hall", "Hall", 0.55, 0.3, 0.95, 0.97, ZoneType::Counting, 2),
                      zone("restr", "Restricted", 0.02, 0.3, 0.20, 0.97, ZoneType::Restricted, 0),
                      zone("bench", "Bench", 0.28, 0.5, 0.45, 0.97, ZoneType::Counting, 10, 3) },
                    { line });

    const cv::Mat frame(720, 1280, CV_8UC3, cv::Scalar(90, 90, 90));
    std::mt19937 rng(7);
    std::uniform_real_distribution<double> jitter(-0.006, 0.006);

    // Person A path: 0-4 s walk 0.25 -> 0.75, 4-6 s wait, 6-10 s walk back, 10-12 s wait.
    auto posA = [](double t) {
        if (t < 4) return 0.25 + 0.5 * t / 4;
        if (t < 6) return 0.75;
        if (t < 10) return 0.75 - 0.5 * (t - 6) / 4;
        return 0.25;
    };

    qint64 now = 1000000;
    AnalysisResult r;
    QVector<EventRecord> events;
    int maxHall = 0;
    for (int step = 0; step <= 12 * 8; ++step) {
        const double t = step / 8.0;
        std::vector<Detection> dets{
            person(posA(t), 0.90),                 // A
            person(0.5 + jitter(rng), 0.92),       // B: standing on the line, jittering
            person(0.10, 0.90),                    // C: inside the restricted zone
            person(0.37, 0.90),                    // D: loitering on the bench
            person(0.85, 0.90),                    // E: already in the hall
        };
        r = engine.analyze(frame, dets, now);
        for (const EventRecord& e : r.events) {
            events.push_back(e);
            std::printf("  t=%5.2fs  %-16s %s\n", t, eventTypeName(e.type).toLatin1().constData(),
                        e.message.toLatin1().constData());
        }
        for (const ZoneStat& z : r.zones)
            if (z.zoneId == QLatin1String("hall"))
                maxHall = qMax(maxHall, z.count);
        now += 125;
    }

    const LineStat& ls = r.lines.at(0);
    ZoneStat hall;
    for (const ZoneStat& z : r.zones)
        if (z.zoneId == QLatin1String("hall"))
            hall = z;
    auto count = [&](EventType t) {
        int n = 0;
        for (const EventRecord& e : events)
            n += e.type == t ? 1 : 0;
        return n;
    };
    std::printf("line IN=%lld OUT=%lld | hall IN=%lld OUT=%lld now=%d max=%d | people=%d\n", ls.in, ls.out, hall.in,
                hall.out, hall.count, maxHall, r.personCount);

    check(ls.out == 1, "line: A walking left->right counted once as OUT");
    check(ls.in == 1, "line: A walking back right->left counted once as IN");
    check(ls.in + ls.out == 2, "line: jittering person B on the line never counted");
    check(hall.in == 1 && hall.out == 1, "zone: A entered and left the hall once (E, already inside, not counted)");
    check(maxHall == 2 && hall.count == 1, "zone: occupancy peaked at 2 and returned to 1");
    check(count(EventType::Intrusion) == 1, "rule: exactly one intrusion alert for C");
    check(count(EventType::Loitering) == 1, "rule: exactly one loitering alert for D (3 s limit)");
    check(count(EventType::Overcrowding) == 1, "rule: overcrowding alert when the hall reached capacity");
    check(r.personCount == 5, "tracking: 5 people tracked at the end");
    check(!hall.inTotals && ls.inTotals,
          "totals: with a counting line on the camera, its zones are left out of site In/Out (no double counting)");

    crossingScenario();
    userTests();
    std::printf("%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
