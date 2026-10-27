#include "CameraGrid.h"
#include "VideoTile.h"
#include "core/AnalyticsStore.h"
#include "core/CameraManager.h"
#include "core/CameraWorker.h"
#include "core/Settings.h"

#include <QGridLayout>
#include <QResizeEvent>
#include <QWindow>

#include <cmath>

CameraGrid::CameraGrid(CameraManager* cameras, AnalyticsStore* store, QWidget* parent)
    : QWidget(parent), m_cams(cameras), m_store(store)
{
    m_grid = new QGridLayout(this);
    m_grid->setContentsMargins(0, 0, 0, 0);
    m_grid->setSpacing(6);

    for (int i = 0; i < CameraManager::kMaxCameras; ++i) {
        auto* t = new VideoTile(i, this);
        t->hide();
        connect(t, &VideoTile::clicked, this, [this](int idx) { selectCamera(m_tiles[idx]->cameraId()); });
        connect(t, &VideoTile::doubleClicked, this, [this](int idx) {
            setTour(false);
            toggleSingleView(m_tiles[idx]->cameraId());
        });
        connect(t, &VideoTile::contextMenuRequested, this,
                [this](int idx, const QPoint& pos) { emit contextMenuRequested(m_tiles[idx]->cameraId(), pos); });
        connect(t, &VideoTile::addRequested, this, &CameraGrid::addCameraRequested);
        connect(t, &VideoTile::fileDropped, this, [this](int, const QString& path) { emit fileDropped(path); });
        m_tiles.push_back(t);
    }

    connect(m_cams, &CameraManager::frameReady, this, [this](int id, const QImage& img) {
        if (VideoTile* t = tileFor(id))
            t->setFrame(img);
    });
    connect(m_cams, &CameraManager::cameraStatusChanged, this,
            [this](int id, CameraStatus s, const QString& msg) {
                if (VideoTile* t = tileFor(id))
                    t->setStatus(s, msg);
            });
    connect(m_cams, &CameraManager::analysisResult, this, [this](const AnalysisResult& r) {
        if (VideoTile* t = tileFor(r.cameraId)) {
            t->setFlowTotals(m_store->totalIn(r.cameraId), m_store->totalOut(r.cameraId));
            t->setResult(r);
        }
    });
    connect(m_cams, &CameraManager::recordingChanged, this, [this](int id, bool rec, const QString&) {
        if (VideoTile* t = tileFor(id))
            t->setRecording(rec);
    });
    connect(m_cams, &CameraManager::analysisStateChanged, this, [this](AnalysisState, const QString&) {
        for (auto it = m_tileFor.cbegin(); it != m_tileFor.cend(); ++it)
            it.value()->setAnalyzing(m_cams->isCameraAnalyzing(it.key()));
    });
    connect(m_cams, &CameraManager::cameraAnalysisChanged, this, [this](int id, bool on) {
        if (VideoTile* t = tileFor(id))
            t->setAnalyzing(on);
    });

    m_tour.setInterval(8000);
    connect(&m_tour, &QTimer::timeout, this, [this] {
        if (m_ids.isEmpty())
            return;
        m_tourPos = (m_tourPos + 1) % m_ids.size();
        m_singleId = m_ids[m_tourPos];
        selectCamera(m_singleId);
        rebuild();
    });

    m_heatTimer.setInterval(1000);
    connect(&m_heatTimer, &QTimer::timeout, this, &CameraGrid::updateHeatmaps);
    m_heatTimer.start();

    m_hintTimer.setSingleShot(true);
    m_hintTimer.setInterval(150);
    connect(&m_hintTimer, &QTimer::timeout, this, &CameraGrid::updateDisplayHints);

    m_layoutN = AppSettings::instance().gridLayout;
    if (!supportedLayouts().contains(m_layoutN))
        m_layoutN = 4;
    applyDisplayOptions();
}

void CameraGrid::gridDims(int n, int& cols, int& rows)
{
    switch (n) {
    case 1:  cols = 1; rows = 1; break;
    case 4:  cols = 2; rows = 2; break;
    case 6:  cols = 3; rows = 2; break;
    case 9:  cols = 3; rows = 3; break;
    case 16: cols = 4; rows = 4; break;
    case 25: cols = 5; rows = 5; break;
    default: cols = 8; rows = 4; break;   // 32
    }
}

int CameraGrid::pageCount() const
{
    if (m_singleId >= 0)
        return 1;
    return qMax(1, int(std::ceil(double(m_ids.size()) / m_layoutN)));
}

void CameraGrid::setCameraIds(const QVector<int>& ids)
{
    m_ids = ids;
    if (m_singleId >= 0 && !m_ids.contains(m_singleId))
        m_singleId = -1;
    if (m_selected >= 0 && !m_ids.contains(m_selected))
        m_selected = -1;
    m_page = qBound(0, m_page, pageCount() - 1);
    rebuild();
}

void CameraGrid::setLayoutCount(int n)
{
    if (!supportedLayouts().contains(n))
        return;
    m_layoutN = n;
    m_singleId = -1;
    setTour(false);
    // Keep the selected camera on screen after the layout change.
    const int sel = m_ids.indexOf(m_selected);
    m_page = sel >= 0 ? sel / n : 0;
    AppSettings::instance().gridLayout = n;
    rebuild();
    emit layoutChanged(n);
    emit singleViewChanged(false);
}

void CameraGrid::nextPage()
{
    if (m_page + 1 < pageCount()) {
        ++m_page;
        rebuild();
    }
}

void CameraGrid::prevPage()
{
    if (m_page > 0) {
        --m_page;
        rebuild();
    }
}

void CameraGrid::selectCamera(int id)
{
    m_selected = id;
    for (VideoTile* t : qAsConst(m_tiles))
        t->setSelected(t->isVisible() && t->cameraId() >= 0 && t->cameraId() == id);
    emit selectionChanged(id);
}

void CameraGrid::toggleSingleView(int cameraId)
{
    if (cameraId < 0)
        return;
    m_singleId = m_singleId == cameraId ? -1 : cameraId;
    if (m_singleId < 0) {
        const int idx = m_ids.indexOf(cameraId);
        m_page = idx >= 0 ? idx / m_layoutN : m_page;
    }
    selectCamera(cameraId);
    rebuild();
    emit singleViewChanged(m_singleId >= 0);
}

void CameraGrid::setTour(bool on, int seconds)
{
    if (!on) {
        if (m_tour.isActive()) {
            m_tour.stop();
            m_singleId = -1;
            rebuild();
            emit singleViewChanged(false);
        }
        return;
    }
    if (m_ids.isEmpty())
        return;
    m_tour.setInterval(qMax(3, seconds) * 1000);
    m_tourPos = qMax(0, m_ids.indexOf(m_selected));
    m_singleId = m_ids[m_tourPos];
    rebuild();
    m_tour.start();
    emit singleViewChanged(true);
}

void CameraGrid::rebuild()
{
    while (m_grid->count() > 0)
        delete m_grid->takeAt(0);   // deletes the layout item, not the tile
    m_tileFor.clear();

    QVector<int> visible;
    int n = m_layoutN;
    if (m_singleId >= 0) {
        visible = { m_singleId };
        n = 1;
    } else {
        for (int i = m_page * m_layoutN; i < m_ids.size() && i < (m_page + 1) * m_layoutN; ++i)
            visible.push_back(m_ids[i]);
    }
    int cols = 1, rows = 1;
    gridDims(n, cols, rows);
    for (int c = 0; c < 8; ++c)
        m_grid->setColumnStretch(c, c < cols ? 1 : 0);
    for (int r = 0; r < 8; ++r)
        m_grid->setRowStretch(r, r < rows ? 1 : 0);

    const OverlayOptions opts = [] {
        const AppSettings& s = AppSettings::instance();
        OverlayOptions o;
        o.showZones = s.showZones;
        o.showIds = s.showIds;
        o.showTrails = s.fnTracks;
        o.showConfidence = s.showConfidence;
        return o;
    }();

    for (int i = 0; i < m_tiles.size(); ++i) {
        VideoTile* t = m_tiles[i];
        if (i >= n) {
            if (t->cameraId() >= 0)
                t->setCamera(-1, nullptr);   // release the cached frame
            t->hide();
            continue;
        }
        const int id = i < visible.size() ? visible[i] : -1;
        const CameraConfig* cfg = id >= 0 ? m_cams->camera(id) : nullptr;
        t->setCamera(cfg ? id : -1, cfg);
        if (cfg) {
            m_tileFor.insert(id, t);
            t->setStatus(m_cams->status(id), m_cams->statusMessage(id));
            t->setAnalyzing(m_cams->isCameraAnalyzing(id));
            t->setRecording(m_cams->isRecording(id));
            t->setOverlayOptions(opts);
            if (t->frame().isNull())
                if (CameraWorker* w = m_cams->worker(id))
                    t->setFrame(w->latestImage(960));
        }
        t->setSelected(cfg && id == m_selected);
        m_grid->addWidget(t, i / cols, i % cols);
        t->show();
    }
    m_hintTimer.start();
    emit pageChanged(m_page, pageCount());
}

void CameraGrid::refreshConfig()
{
    rebuild();
}

void CameraGrid::applyDisplayOptions()
{
    const AppSettings& s = AppSettings::instance();
    OverlayOptions o;
    o.showZones = s.showZones;
    o.showIds = s.showIds;
    o.showTrails = s.fnTracks;
    o.showConfidence = s.showConfidence;
    for (VideoTile* t : qAsConst(m_tiles)) {
        t->setOverlayOptions(o);
        if (!s.fnHeatmap)
            t->setHeatmap(QImage());
    }
}

void CameraGrid::raiseAlert(int cameraId, const QString& text, Severity severity)
{
    if (VideoTile* t = tileFor(cameraId))
        t->raiseAlert(text, severity);
}

void CameraGrid::resizeEvent(QResizeEvent* e)
{
    QWidget::resizeEvent(e);
    m_hintTimer.start();
}

void CameraGrid::updateDisplayHints()
{
    // Tell each capture thread how big its picture is on screen, so frames are
    // down-scaled once in the worker instead of per paint, and hidden cameras
    // skip display conversion entirely (analysis keeps running for them).
    const qreal dpr = window() && window()->windowHandle() ? window()->windowHandle()->devicePixelRatio() : 1.0;
    for (int id : qAsConst(m_ids)) {
        VideoTile* t = tileFor(id);
        if (!t) {
            m_cams->setDisplayHint(id, false, 0);
            continue;
        }
        const int px = int(t->width() * dpr);
        const int w = px <= 400 ? 480 : px <= 720 ? 800 : px <= 1100 ? 1280 : 1920;
        m_cams->setDisplayHint(id, true, w);
    }
    for (const CameraConfig& c : m_cams->cameras())
        if (!m_ids.contains(c.id))
            m_cams->setDisplayHint(c.id, false, 0);
}

void CameraGrid::updateHeatmaps()
{
    if (!AppSettings::instance().fnHeatmap || !isVisible())
        return;
    for (auto it = m_tileFor.cbegin(); it != m_tileFor.cend(); ++it) {
        if (!m_cams->isCameraAnalyzing(it.key()))
            continue;
        if (CameraWorker* w = m_cams->worker(it.key()))
            it.value()->setHeatmap(w->heatmapImage());
    }
}

void CameraGrid::setCanAddCameras(bool on)
{
    for (VideoTile* t : qAsConst(m_tiles))
        t->setCanAdd(on);
}
