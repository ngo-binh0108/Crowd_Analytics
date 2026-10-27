#include "LiveViewPage.h"
#include "core/AnalyticsStore.h"
#include "core/CameraManager.h"
#include "core/CameraWorker.h"
#include "core/Database.h"
#include "core/EventManager.h"
#include "core/Session.h"
#include "core/Settings.h"
#include "ui/Theme.h"
#include "ui/widgets/CameraGrid.h"
#include "ui/widgets/Charts.h"
#include "ui/widgets/Common.h"

#include <QButtonGroup>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QImageReader>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>

namespace {

QToolButton* navButton(const QString& icon, const QString& tip, QWidget* parent)
{
    auto* b = new QToolButton(parent);
    b->setIcon(Theme::icon(icon, Theme::text2()));
    b->setIconSize(QSize(16, 16));
    b->setToolTip(tip);
    b->setProperty("tool", true);
    b->setCursor(Qt::PointingHandCursor);
    return b;
}

QToolButton* tileButton(const QString& icon, const QString& text, QWidget* parent, bool checkable = false)
{
    auto* b = new QToolButton(parent);
    b->setIcon(Theme::icon(icon, Theme::text2(), Qt::white));
    b->setIconSize(QSize(20, 20));
    b->setText(text);
    b->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    b->setProperty("tile", true);
    b->setCheckable(checkable);
    b->setCursor(Qt::PointingHandCursor);
    b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    b->setMinimumHeight(58);
    return b;
}

} // namespace

LiveViewPage::LiveViewPage(const AppCore& core, QWidget* parent) : QWidget(parent), m_core(core)
{
    setObjectName(QStringLiteral("Page"));
    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(16, 14, 16, 14);
    root->setSpacing(14);

    auto* center = new QVBoxLayout;
    center->setSpacing(10);

    // Header row
    m_headerRow = new QWidget(this);
    auto* hl = new QHBoxLayout(m_headerRow);
    hl->setContentsMargins(0, 0, 0, 0);
    hl->setSpacing(8);
    auto* titleCol = new QVBoxLayout;
    titleCol->setSpacing(0);
    titleCol->addWidget(makeLabel(tr("Live View"), "pageTitle", this));
    m_subtitle = makeLabel(QString(), "secondary", this);
    titleCol->addWidget(m_subtitle);
    hl->addLayout(titleCol);
    hl->addStretch(1);
    m_prevBtn = navButton(QStringLiteral("chevron-left"), tr("Previous page"), this);
    m_pageLabel = makeLabel(QString(), "secondary", this);
    m_nextBtn = navButton(QStringLiteral("chevron-right"), tr("Next page"), this);
    hl->addWidget(m_prevBtn);
    hl->addWidget(m_pageLabel);
    hl->addWidget(m_nextBtn);
    auto* stripBtn = navButton(QStringLiteral("layers"), tr("Show / hide the bottom analytics panel"), this);
    stripBtn->setCheckable(true);
    stripBtn->setChecked(AppSettings::instance().bottomPanelVisible);
    hl->addSpacing(8);
    hl->addWidget(stripBtn);
    center->addWidget(m_headerRow);

    m_grid = new CameraGrid(core.cameras, core.analytics, this);
    center->addWidget(m_grid, 1);

    m_bottomStrip = buildBottomStrip();
    m_bottomStrip->setVisible(AppSettings::instance().bottomPanelVisible);
    center->addWidget(m_bottomStrip);
    root->addLayout(center, 1);

    m_rightPanel = buildRightPanel();
    root->addWidget(m_rightPanel);

    // Wiring
    connect(m_prevBtn, &QToolButton::clicked, m_grid, &CameraGrid::prevPage);
    connect(m_nextBtn, &QToolButton::clicked, m_grid, &CameraGrid::nextPage);
    connect(stripBtn, &QToolButton::toggled, this, [this](bool on) {
        AppSettings::instance().bottomPanelVisible = on;
        AppSettings::instance().save();
        m_bottomStrip->setVisible(on);
    });
    connect(m_grid, &CameraGrid::pageChanged, this, [this](int page, int count) {
        m_pageLabel->setText(tr("Page %1 / %2").arg(page + 1).arg(count));
        m_prevBtn->setEnabled(page > 0);
        m_nextBtn->setEnabled(page + 1 < count);
    });
    connect(m_grid, &CameraGrid::layoutChanged, this, [this](int n) {
        if (QAbstractButton* b = m_layoutGroup->button(n))
            b->setChecked(true);
        AppSettings::instance().save();
    });
    connect(m_grid, &CameraGrid::singleViewChanged, this, [this](bool) { updateQuickControls(); });
    connect(m_grid, &CameraGrid::selectionChanged, this, [this](int) {
        updateQuickControls();
        updateHeatmap();
    });
    connect(m_grid, &CameraGrid::addCameraRequested, this, &LiveViewPage::addCameraRequested);
    connect(m_grid, &CameraGrid::fileDropped, this, &LiveViewPage::fileDropped);
    connect(m_grid, &CameraGrid::contextMenuRequested, this, &LiveViewPage::showCameraMenu);

    connect(core.cameras, &CameraManager::camerasChanged, this, &LiveViewPage::refreshCameras);
    connect(core.cameras, &CameraManager::cameraStatusChanged, this, [this] { updateStats(); });
    connect(core.cameras, &CameraManager::recordingChanged, this, [this] { updateQuickControls(); });
    connect(core.cameras, &CameraManager::analysisStateChanged, this, [this] {
        updateStats();
        updateZoneBars();
    });
    connect(core.analytics, &AnalyticsStore::updated, this, [this] {
        updateStats();
        updateZoneBars();
    });
    connect(core.events, &EventManager::eventAdded, this, [this](const EventRecord& e) {
        addEventRow(e);
        if (!e.snapshotPath.isEmpty())
            addSnapshot(e.snapshotPath, e.time.toString(QStringLiteral("HH:mm:ss")) + QStringLiteral("  ") + e.cameraName);
    });
    connect(core.events, &EventManager::eventsChanged, this, [this] { updateStats(); });

    m_slowTimer.setInterval(5000);
    connect(&m_slowTimer, &QTimer::timeout, this, &LiveViewPage::updateFlowChart);
    m_slowTimer.start();
    m_heatTimer.setInterval(2000);
    connect(&m_heatTimer, &QTimer::timeout, this, &LiveViewPage::updateHeatmap);
    m_heatTimer.start();

    // Initial content
    EventFilter f;
    f.from = QDateTime(QDate::currentDate().addDays(-1), QTime(0, 0));
    QVector<EventRecord> recent = Database::instance().queryEvents(f, 6);
    std::reverse(recent.begin(), recent.end());
    for (const EventRecord& e : recent)
        addEventRow(e);
    loadRecentSnapshots();
    refreshCameras();
    updateFlowChart();
    updateStats();
}

// ---------------------------------------------------------------------------
// Layout builders
// ---------------------------------------------------------------------------

QWidget* LiveViewPage::buildBottomStrip()
{
    auto* strip = new QWidget(this);
    strip->setFixedHeight(214);
    auto* lay = new QHBoxLayout(strip);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(10);

    auto* flow = new Card(tr("People Flow  (last 60 min)"), QStringLiteral("analytics"), strip);
    m_flowChart = new LineChart(flow);
    m_flowChart->setEmptyText(tr("No entries or exits counted yet"));
    flow->body()->addWidget(m_flowChart);
    lay->addWidget(flow, 3);

    auto* zones = new Card(tr("Zone Occupancy"), QStringLiteral("density"), strip);
    auto* zscroll = new QScrollArea(zones);
    zscroll->setWidgetResizable(true);
    zscroll->setFrameShape(QFrame::NoFrame);
    zscroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_zoneBars = new ZoneBars(zscroll);
    m_zoneBars->setEmptyText(tr("Draw zones in Zones & Rules to measure occupancy"));
    zscroll->setWidget(m_zoneBars);
    zscroll->viewport()->setStyleSheet(QStringLiteral("background:transparent;"));
    m_zoneBars->setStyleSheet(QStringLiteral("background:transparent;"));
    zones->body()->addWidget(zscroll);
    lay->addWidget(zones, 2);

    auto* heat = new Card(tr("Crowd Heatmap"), QStringLiteral("flame"), strip);
    m_heatmap = new HeatmapView(heat);
    heat->body()->addWidget(m_heatmap);
    lay->addWidget(heat, 2);
    m_heatCard = heat;

    auto* snaps = new Card(tr("Recent Snapshots"), QStringLiteral("snapshot"), strip);
    m_snapshots = new QListWidget(snaps);
    m_snapshots->setViewMode(QListView::IconMode);
    m_snapshots->setFlow(QListView::LeftToRight);
    m_snapshots->setWrapping(false);
    m_snapshots->setIconSize(QSize(144, 81));
    m_snapshots->setSpacing(4);
    m_snapshots->setMovement(QListView::Static);
    m_snapshots->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_snapshots->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_snapshots->setToolTip(tr("Double-click to open"));
    m_snapshots->setStyleSheet(QStringLiteral("QListWidget{background:transparent;} QListWidget::item{color:#9db0cc;}"));
    connect(m_snapshots, &QListWidget::itemDoubleClicked, this, [](QListWidgetItem* it) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(it->data(Qt::UserRole).toString()));
    });
    snaps->body()->addWidget(m_snapshots);
    lay->addWidget(snaps, 2);
    return strip;
}

QWidget* LiveViewPage::buildRightPanel()
{
    auto* scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("PageScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setFixedWidth(318);

    auto* content = new QWidget(scroll);
    auto* col = new QVBoxLayout(content);
    col->setContentsMargins(0, 0, 8, 0);
    col->setSpacing(12);

    // Live statistics
    auto* stats = new Card(tr("Live Statistics"), QStringLiteral("people"), content);
    auto* sg = new QGridLayout;
    sg->setSpacing(8);
    m_statPeople = new StatTile(tr("People in view"), QStringLiteral("people"), Theme::seriesPeople(), stats);
    m_statIn = new StatTile(tr("Entered today"), QStringLiteral("in"), Theme::seriesIn(), stats);
    m_statOut = new StatTile(tr("Exited today"), QStringLiteral("out"), Theme::seriesOut(), stats);
    m_statAlerts = new StatTile(tr("Open alerts"), QStringLiteral("alert"), Theme::critical(), stats);
    sg->addWidget(m_statPeople, 0, 0);
    sg->addWidget(m_statAlerts, 0, 1);
    sg->addWidget(m_statIn, 1, 0);
    sg->addWidget(m_statOut, 1, 1);
    stats->body()->addLayout(sg);
    m_occLabel = makeLabel(QString(), "secondary", stats);
    stats->body()->addWidget(m_occLabel);
    m_occBar = new QProgressBar(stats);
    m_occBar->setRange(0, 100);
    m_occBar->setTextVisible(false);
    m_occBar->setFixedHeight(6);
    stats->body()->addWidget(m_occBar);
    col->addWidget(stats);

    // Quick controls
    auto* quick = new Card(tr("Quick Controls"), QStringLiteral("power"), content);
    auto* qg = new QGridLayout;
    qg->setSpacing(8);
    auto* snapBtn = tileButton(QStringLiteral("snapshot"), tr("Snapshot"), quick);
    m_recordBtn = tileButton(QStringLiteral("record"), tr("Record"), quick, true);
    m_tourBtn = tileButton(QStringLiteral("tour"), tr("Tour"), quick, true);
    auto* fullBtn = tileButton(QStringLiteral("fullscreen"), tr("Video Wall"), quick);
    auto* resetBtn = tileButton(QStringLiteral("refresh"), tr("Reset Count"), quick);
    auto* reconnectBtn = tileButton(QStringLiteral("power"), tr("Reconnect"), quick);
    m_snapBtn = snapBtn;
    m_resetBtn = resetBtn;
    m_reconnectBtn = reconnectBtn;
    snapBtn->setToolTip(tr("Save a snapshot of the selected camera (F2)"));
    m_recordBtn->setToolTip(tr("Start / stop recording the selected camera"));
    m_tourBtn->setToolTip(tr("Cycle through all cameras in single view"));
    fullBtn->setToolTip(tr("Full-screen video wall (F11, Esc to exit)"));
    resetBtn->setToolTip(tr("Reset today's entry / exit counters"));
    reconnectBtn->setToolTip(tr("Reconnect the selected camera"));
    qg->addWidget(snapBtn, 0, 0);
    qg->addWidget(m_recordBtn, 0, 1);
    qg->addWidget(m_tourBtn, 0, 2);
    qg->addWidget(fullBtn, 1, 0);
    qg->addWidget(resetBtn, 1, 1);
    qg->addWidget(reconnectBtn, 1, 2);
    quick->body()->addLayout(qg);
    col->addWidget(quick);

    connect(snapBtn, &QToolButton::clicked, this, [this] {
        const int id = selectedCamera();
        if (id < 0) {
            emit statusMessage(tr("Select a camera first."));
            return;
        }
        const QString path = m_core.cameras->takeSnapshot(id);
        emit statusMessage(path.isEmpty() ? tr("Snapshot failed: no picture from this camera yet.")
                                          : tr("Snapshot saved: %1").arg(QDir::toNativeSeparators(path)));
    });
    connect(m_recordBtn, &QToolButton::clicked, this, [this] {
        const int id = selectedCamera();
        if (id < 0) {
            emit statusMessage(tr("Select a camera first."));
            updateQuickControls();
            return;
        }
        if (m_core.cameras->isRecording(id))
            m_core.cameras->stopRecording(id);
        else
            m_core.cameras->startRecording(id);
        emit statusMessage(m_core.cameras->isRecording(id) ? tr("Stopping recording...") : tr("Recording started."));
    });
    connect(m_tourBtn, &QToolButton::toggled, this, [this](bool on) {
        if (on != m_grid->isTourRunning())
            m_grid->setTour(on);
    });
    connect(fullBtn, &QToolButton::clicked, this, &LiveViewPage::fullScreenRequested);
    connect(resetBtn, &QToolButton::clicked, this, [this] {
        if (QMessageBox::question(this, tr("Reset counters"),
                                  tr("Reset today's entry / exit counters for all cameras?\n"
                                     "History already saved stays available in Analytics.")) != QMessageBox::Yes)
            return;
        m_core.analytics->resetCounters();
        m_core.cameras->resetCounters();
        emit statusMessage(tr("Counters reset."));
    });
    connect(reconnectBtn, &QToolButton::clicked, this, [this] {
        const int id = selectedCamera();
        if (id >= 0)
            m_core.cameras->reconnect(id);
        else
            emit statusMessage(tr("Select a camera first."));
    });

    // Display layout
    auto* layoutCard = new Card(tr("Display Layout"), QStringLiteral("grid"), content);
    auto* lg = new QGridLayout;
    lg->setSpacing(6);
    m_layoutGroup = new QButtonGroup(layoutCard);
    m_layoutGroup->setExclusive(true);
    int pos = 0;
    for (int n : CameraGrid::supportedLayouts()) {
        int cols = 1, rows = 1;
        switch (n) {
        case 1: cols = rows = 1; break;
        case 4: cols = rows = 2; break;
        case 6: cols = 3; rows = 2; break;
        case 9: cols = rows = 3; break;
        case 16: cols = rows = 4; break;
        case 25: cols = rows = 5; break;
        default: cols = 8; rows = 4; break;
        }
        auto* b = new QToolButton(layoutCard);
        b->setIcon(Theme::gridIcon(cols, rows, Theme::text2(), Qt::white));
        b->setIconSize(QSize(18, 18));
        b->setText(n == 1 ? tr("1 View") : QString::number(n));
        b->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        b->setProperty("tile", true);
        b->setCheckable(true);
        b->setCursor(Qt::PointingHandCursor);
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        b->setToolTip(tr("%1-camera layout").arg(n));
        m_layoutGroup->addButton(b, n);
        lg->addWidget(b, pos / 4, pos % 4);
        ++pos;
    }
    if (QAbstractButton* b = m_layoutGroup->button(m_grid->layoutCount()))
        b->setChecked(true);
    connect(m_layoutGroup, &QButtonGroup::idClicked, this,
            [this](int n) { m_grid->setLayoutCount(n); });
    layoutCard->body()->addLayout(lg);
    col->addWidget(layoutCard);

    // AI functions
    auto* fn = new Card(tr("AI Functions"), QStringLiteral("shield"), content);
    auto* fl = new QVBoxLayout;
    fl->setSpacing(4);
    AppSettings& s = AppSettings::instance();
    addFunctionRow(fl, QStringLiteral("people"), tr("People Counting"),
                   tr("Count entries and exits through zones and counting lines"), &s.fnCounting);
    addFunctionRow(fl, QStringLiteral("density"), tr("Crowd Density"),
                   tr("Measure occupancy against capacity and raise crowding alerts"), &s.fnDensity);
    addFunctionRow(fl, QStringLiteral("intrusion"), tr("Intrusion Detection"),
                   tr("Alert when a person enters a Restricted zone"), &s.fnIntrusion);
    addFunctionRow(fl, QStringLiteral("loiter"), tr("Loitering"),
                   tr("Alert when a person stays in a zone longer than its loitering time"), &s.fnLoitering);
    addFunctionRow(fl, QStringLiteral("bag"), tr("Abandoned Objects"),
                   tr("Detect bags and suitcases left unattended (needs a COCO YOLO model)"), &s.fnAbandoned);
    addFunctionRow(fl, QStringLiteral("run"), tr("Running Detection"),
                   tr("Alert when a person runs (possible panic or pursuit)"), &s.fnRunning);
    addFunctionRow(fl, QStringLiteral("eye"), tr("Camera Tamper"),
                   tr("Alert when a lens is covered, blinded or out of focus"), &s.fnTamper);
    addFunctionRow(fl, QStringLiteral("flame"), tr("Heatmap Overlay"),
                   tr("Show where people spend time, on top of the video"), &s.fnHeatmap);
    addFunctionRow(fl, QStringLiteral("privacy"), tr("Privacy Masking"),
                   tr("Pixelate detected people on screen (recordings keep the original)"), &s.fnPrivacy);
    addFunctionRow(fl, QStringLiteral("route"), tr("Movement Trails"),
                   tr("Draw each person's recent path"), &s.fnTracks);
    fn->body()->addLayout(fl);
    col->addWidget(fn);

    // Alerts & events
    auto* ev = new Card(tr("Alerts & Events"), QStringLiteral("events"), content);
    auto* viewAll = new QPushButton(tr("View all"), ev);
    viewAll->setProperty("variant", "ghost");
    viewAll->setCursor(Qt::PointingHandCursor);
    connect(viewAll, &QPushButton::clicked, this, &LiveViewPage::viewAllEventsRequested);
    ev->addHeaderWidget(viewAll);
    m_eventsLayout = new QVBoxLayout;
    m_eventsLayout->setSpacing(2);
    ev->body()->addLayout(m_eventsLayout);
    col->addWidget(ev);

    col->addStretch(1);
    scroll->setWidget(content);
    return scroll;
}

void LiveViewPage::addFunctionRow(QVBoxLayout* lay, const QString& icon, const QString& label, const QString& tip,
                                  bool* setting)
{
    auto* row = new QWidget(this);
    row->setToolTip(tip);
    auto* h = new QHBoxLayout(row);
    h->setContentsMargins(0, 3, 0, 3);
    h->setSpacing(10);
    auto* ic = new QLabel(row);
    ic->setPixmap(Theme::pixmap(icon, Theme::text2(), 16));
    h->addWidget(ic);
    h->addWidget(makeLabel(label, nullptr, row), 1);
    auto* sw = new ToggleSwitch(row);
    m_fnSwitches.push_back(sw);
    sw->setChecked(*setting);
    h->addWidget(sw);
    lay->addWidget(row);
    connect(sw, &ToggleSwitch::toggled, this, [this, setting](bool on) {
        *setting = on;
        AppSettings::instance().save();
        m_core.cameras->applySettings();
        m_grid->applyDisplayOptions();
    });
}

// ---------------------------------------------------------------------------
// Updates
// ---------------------------------------------------------------------------

int LiveViewPage::selectedCamera() const
{
    return m_grid->selectedCameraId();
}

void LiveViewPage::setLocationFilter(const QString& location)
{
    m_location = location;
    refreshCameras();
}

void LiveViewPage::refreshCameras()
{
    QVector<int> ids;
    for (const CameraConfig& c : m_core.cameras->cameras())
        if (m_location.isEmpty() || c.location.trimmed() == m_location)
            ids.push_back(c.id);
    m_grid->setCameraIds(ids);
    if (m_grid->selectedCameraId() < 0 && !ids.isEmpty())
        m_grid->selectCamera(ids.first());
    updateStats();
    updateZoneBars();
    updateHeatmap();
}

void LiveViewPage::setVideoWallMode(bool on)
{
    m_rightPanel->setVisible(!on);
    m_headerRow->setVisible(!on);
    m_bottomStrip->setVisible(!on && AppSettings::instance().bottomPanelVisible);
    layout()->setContentsMargins(on ? 4 : 16, on ? 4 : 14, on ? 4 : 16, on ? 4 : 14);
}

void LiveViewPage::updateStats()
{
    const AppSettings& s = AppSettings::instance();
    AnalyticsStore* st = m_core.analytics;
    const bool running = m_core.cameras->isAnalysisRunning();
    const int total = m_core.cameras->count();
    const int online = m_core.cameras->onlineCount();

    m_subtitle->setText(tr("%1 cameras  |  %2 online%3")
                            .arg(total)
                            .arg(online)
                            .arg(m_location.isEmpty() ? QString() : QStringLiteral("  |  ") + m_location));

    m_statPeople->setValue(QString::number(st->peopleInView()));
    m_statPeople->setCaption(running ? tr("%1 cameras analysed").arg(st->activeCameras()) : tr("Analysis stopped"),
                             running ? QColor() : Theme::text3());
    m_statIn->setValue(QString::number(st->totalIn()));
    m_statIn->setCaption(tr("since %1").arg(st->countingSince().toString(QStringLiteral("HH:mm"))));
    m_statOut->setValue(QString::number(st->totalOut()));
    m_statOut->setCaption(tr("net flow %1%2").arg(st->totalIn() - st->totalOut() >= 0 ? "+" : "").arg(st->totalIn() - st->totalOut()));
    const int open = m_core.events->unacknowledgedToday();
    m_statAlerts->setValue(QString::number(open));
    m_statAlerts->setCaption(open > 0 ? tr("need acknowledgement") : tr("all clear"),
                             open > 0 ? Theme::warning() : Theme::good());

    const int occ = st->occupancy();
    const int cap = s.siteCapacity;
    const int pct = cap > 0 ? qRound(100.0 * occ / cap) : 0;
    const DensityLevel lvl = densityLevelFor(occ, cap, s.densityWarnPct, s.densityCritPct);
    m_occLabel->setText(tr("Site occupancy  %1 / %2  (%3%)  -  %4").arg(occ).arg(cap).arg(pct).arg(densityLevelName(lvl)));
    m_occBar->setValue(qBound(0, pct, 100));
    m_occBar->setStyleSheet(QStringLiteral("QProgressBar::chunk{background:%1;border-radius:3px;}")
                                .arg(Theme::levelColor(lvl).name()));
}

void LiveViewPage::updateFlowChart()
{
    if (!m_bottomStrip->isVisible())
        return;
    const QDateTime now = QDateTime::currentDateTime();
    QDateTime from = now.addSecs(-59 * 60);
    from.setTime(QTime(from.time().hour(), from.time().minute()));
    const QVector<MinuteRow> rows = m_core.analytics->series(from, now, 1, -1);
    QStringList labels;
    ChartSeries in{ tr("In"), Theme::seriesIn(), {}, true };
    ChartSeries out{ tr("Out"), Theme::seriesOut(), {}, true };
    for (const MinuteRow& r : rows) {
        labels << Database::timeOfMinute(r.minute).toString(QStringLiteral("HH:mm"));
        in.values << double(r.in);
        out.values << double(r.out);
    }
    m_flowChart->setData(labels, { in, out });
}

void LiveViewPage::updateZoneBars()
{
    const bool running = m_core.cameras->isAnalysisRunning();
    QVector<ZoneBarItem> items;
    for (const CameraConfig& c : m_core.cameras->cameras()) {
        if (!m_location.isEmpty() && c.location.trimmed() != m_location)
            continue;
        const bool active = running && m_core.cameras->isCameraAnalyzing(c.id);
        for (const Zone& z : c.zones) {
            if (!z.enabled || z.type == ZoneType::Restricted)
                continue;
            const TargetLive t = m_core.analytics->target(c.id, z.id);
            ZoneBarItem it;
            it.name = z.name;
            it.subtitle = c.name + (active ? QString() : tr("  (idle)"));
            it.count = active ? t.current : 0;
            it.capacity = z.capacity;
            it.level = active ? t.level : DensityLevel::Low;
            items.push_back(it);
        }
    }
    std::stable_sort(items.begin(), items.end(), [](const ZoneBarItem& a, const ZoneBarItem& b) {
        const double fa = a.capacity > 0 ? double(a.count) / a.capacity : 0;
        const double fb = b.capacity > 0 ? double(b.count) / b.capacity : 0;
        return fa > fb;
    });
    m_zoneBars->setItems(items);
    m_zoneBars->setMinimumHeight(qMax(60, items.size() * 42 + 4));
}

void LiveViewPage::updateHeatmap()
{
    if (!m_bottomStrip->isVisible() || !isVisible())
        return;
    int id = selectedCamera();
    if (id < 0 && m_core.cameras->count() > 0)
        id = m_core.cameras->cameras().first().id;
    const CameraConfig* cfg = m_core.cameras->camera(id);
    CameraWorker* w = m_core.cameras->worker(id);
    if (!cfg || !w) {
        m_heatmap->setImages(QImage(), QImage());
        m_heatCard->setTitle(tr("Crowd Heatmap"));
        return;
    }
    m_heatCard->setTitle(tr("Crowd Heatmap  |  %1").arg(cfg->name));
    m_heatmap->setImages(w->latestImage(480), m_core.cameras->isCameraAnalyzing(id) ? w->heatmapImage() : QImage());
}

void LiveViewPage::updateQuickControls()
{
    const int id = selectedCamera();
    m_recordBtn->setChecked(id >= 0 && m_core.cameras->isRecording(id));
    m_tourBtn->blockSignals(true);
    m_tourBtn->setChecked(m_grid->isTourRunning());
    m_tourBtn->blockSignals(false);
}

void LiveViewPage::addEventRow(const EventRecord& e)
{
    auto* row = new QWidget(this);
    row->setToolTip(e.message);
    auto* h = new QHBoxLayout(row);
    h->setContentsMargins(2, 4, 2, 4);
    h->setSpacing(10);
    auto* ic = new QLabel(row);
    const QString icon = e.severity == Severity::Critical ? QStringLiteral("alert")
                         : e.severity == Severity::Warning ? QStringLiteral("alert")
                                                           : QStringLiteral("info");
    ic->setPixmap(Theme::pixmap(icon, Theme::severityColor(e.severity), 18));
    ic->setAlignment(Qt::AlignTop);
    h->addWidget(ic, 0, Qt::AlignTop);
    auto* col = new QVBoxLayout;
    col->setSpacing(0);
    auto* title = makeLabel(eventTypeName(e.type), nullptr, row);
    title->setStyleSheet(QStringLiteral("font-weight:600;"));
    col->addWidget(title);
    QString sub = e.cameraName;
    if (!e.zoneName.isEmpty())
        sub += QStringLiteral("  |  ") + e.zoneName;
    auto* subL = makeLabel(sub, "muted", row);
    col->addWidget(subL);
    h->addLayout(col, 1);
    h->addWidget(makeLabel(e.time.toString(QStringLiteral("HH:mm")), "muted", row), 0, Qt::AlignTop);

    m_eventsLayout->insertWidget(0, row);
    while (m_eventsLayout->count() > 6) {
        QLayoutItem* it = m_eventsLayout->takeAt(m_eventsLayout->count() - 1);
        if (it->widget())
            it->widget()->deleteLater();
        delete it;
    }
}

void LiveViewPage::addSnapshot(const QString& path, const QString& caption)
{
    for (int i = 0; i < m_snapshots->count(); ++i)
        if (m_snapshots->item(i)->data(Qt::UserRole).toString() == path)
            return;
    QImageReader reader(path);
    const QSize sz = reader.size();
    if (sz.isValid())
        reader.setScaledSize(sz.scaled(QSize(288, 162), Qt::KeepAspectRatio));
    const QImage thumb = reader.read();
    if (thumb.isNull())
        return;
    auto* it = new QListWidgetItem(QIcon(QPixmap::fromImage(thumb)), caption);
    it->setData(Qt::UserRole, path);
    it->setToolTip(QDir::toNativeSeparators(path));
    m_snapshots->insertItem(0, it);
    while (m_snapshots->count() > 20)
        delete m_snapshots->takeItem(m_snapshots->count() - 1);
}

void LiveViewPage::loadRecentSnapshots()
{
    QFileInfoList files;
    QDirIterator it(AppSettings::instance().snapshotDir, { QStringLiteral("*.jpg"), QStringLiteral("*.png") }, QDir::Files,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        files << it.fileInfo();
    }
    std::sort(files.begin(), files.end(),
              [](const QFileInfo& a, const QFileInfo& b) { return a.lastModified() < b.lastModified(); });
    const int start = qMax(0, files.size() - 10);
    for (int i = start; i < files.size(); ++i)
        addSnapshot(files[i].absoluteFilePath(), files[i].lastModified().toString(QStringLiteral("MM-dd HH:mm")));
}

void LiveViewPage::showCameraMenu(int cameraId, const QPoint& globalPos)
{
    const CameraConfig* cfg = m_core.cameras->camera(cameraId);
    if (!cfg)
        return;
    QMenu menu(this);
    const bool running = m_core.cameras->isAnalysisRunning();
    QAction* aiAct = menu.addAction(Theme::icon(QStringLiteral("shield")),
                                    cfg->analysisEnabled ? tr("Exclude from analysis") : tr("Include in analysis"));
    aiAct->setToolTip(running ? QString() : tr("Takes effect when analysis is running"));
    QAction* snapAct = menu.addAction(Theme::icon(QStringLiteral("snapshot")), tr("Take snapshot"));
    QAction* recAct = nullptr;
    if (cfg->type != SourceType::Image)
        recAct = menu.addAction(Theme::icon(QStringLiteral("record")),
                                m_core.cameras->isRecording(cameraId) ? tr("Stop recording") : tr("Start recording"));
    menu.addSeparator();
    QAction* singleAct = menu.addAction(Theme::icon(QStringLiteral("fullscreen")),
                                        m_grid->isSingleView() ? tr("Back to grid") : tr("Single view"));
    QAction* zonesAct = menu.addAction(Theme::icon(QStringLiteral("zones")), tr("Edit zones && rules..."));
    QAction* propsAct = menu.addAction(Theme::icon(QStringLiteral("edit")), tr("Camera properties..."));
    QAction* reconAct = menu.addAction(Theme::icon(QStringLiteral("refresh")), tr("Reconnect"));
    QAction* heatAct = menu.addAction(Theme::icon(QStringLiteral("flame")), tr("Reset heatmap"));
    menu.addSeparator();
    QAction* removeAct = menu.addAction(Theme::icon(QStringLiteral("trash"), Theme::critical()), tr("Remove camera"));

    const Session& ses = Session::instance();
    aiAct->setEnabled(ses.can(Permission::ToggleFunctions));
    snapAct->setEnabled(ses.can(Permission::CaptureMedia));
    if (recAct)
        recAct->setEnabled(ses.can(Permission::CaptureMedia));
    zonesAct->setVisible(ses.can(Permission::EditZones));
    propsAct->setVisible(ses.can(Permission::ManageCameras));
    reconAct->setEnabled(ses.can(Permission::ControlAnalysis));
    heatAct->setEnabled(ses.can(Permission::ResetCounters));
    removeAct->setVisible(ses.can(Permission::ManageCameras));
    QAction* chosen = menu.exec(globalPos);
    if (!chosen)
        return;
    if (chosen == aiAct) {
        m_core.cameras->setCameraAnalysis(cameraId, !cfg->analysisEnabled);
        if (!m_core.cameras->isCameraAnalyzing(cameraId))
            m_core.analytics->setCameraInactive(cameraId);
    } else if (chosen == snapAct) {
        const QString path = m_core.cameras->takeSnapshot(cameraId);
        emit statusMessage(path.isEmpty() ? tr("Snapshot failed.") : tr("Snapshot saved: %1").arg(QDir::toNativeSeparators(path)));
    } else if (recAct && chosen == recAct) {
        if (m_core.cameras->isRecording(cameraId))
            m_core.cameras->stopRecording(cameraId);
        else
            m_core.cameras->startRecording(cameraId);
    } else if (chosen == singleAct) {
        m_grid->setTour(false);
        m_grid->toggleSingleView(cameraId);
    } else if (chosen == zonesAct) {
        emit editZonesRequested(cameraId);
    } else if (chosen == propsAct) {
        emit editCameraRequested(cameraId);
    } else if (chosen == reconAct) {
        m_core.cameras->reconnect(cameraId);
    } else if (chosen == heatAct) {
        if (CameraWorker* w = m_core.cameras->worker(cameraId))
            w->resetHeatmap();
    } else if (chosen == removeAct) {
        if (QMessageBox::question(this, tr("Remove camera"), tr("Remove \"%1\" and its zones?").arg(cfg->name)) ==
            QMessageBox::Yes)
            m_core.cameras->removeCamera(cameraId);
    }
}

void LiveViewPage::applyPermissions()
{
    const Session& ses = Session::instance();
    m_snapBtn->setEnabled(ses.can(Permission::CaptureMedia));
    m_recordBtn->setEnabled(ses.can(Permission::CaptureMedia));
    m_resetBtn->setEnabled(ses.can(Permission::ResetCounters));
    m_reconnectBtn->setEnabled(ses.can(Permission::ControlAnalysis));
    for (ToggleSwitch* sw : qAsConst(m_fnSwitches))
        sw->setEnabled(ses.can(Permission::ToggleFunctions));
    m_grid->setCanAddCameras(ses.can(Permission::ManageCameras));
}
