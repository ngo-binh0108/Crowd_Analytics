#include "MainWindow.h"
#include "core/AnalyticsStore.h"
#include "core/Database.h"
#include "core/EventManager.h"
#include "core/Session.h"
#include "core/Settings.h"
#include "pages/AnalyticsPage.h"
#include "pages/CamerasPage.h"
#include "pages/EventsPage.h"
#include "pages/LiveViewPage.h"
#include "pages/MediaPage.h"
#include "pages/SettingsPage.h"
#include "pages/UsersPage.h"
#include "pages/ZonesPage.h"
#include "ui/AccountDialogs.h"
#include "ui/Theme.h"
#include "ui/widgets/CameraGrid.h"
#include "ui/widgets/Common.h"

#include <QApplication>
#include <QButtonGroup>
#include <QCloseEvent>
#include <QComboBox>
#include <QDate>
#include <QDir>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QShortcut>
#include <QStackedWidget>
#include <QStatusBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent)
{
    setWindowTitle(tr("Surveillance System"));
    setMinimumSize(1280, 760);

    m_core.cameras = new CameraManager(this);
    m_core.analytics = new AnalyticsStore(this);
    m_core.events = new EventManager(this);
    m_core.analytics->loadToday();

    // Pages
    m_pages = new QStackedWidget(this);
    m_live = new LiveViewPage(m_core, m_pages);
    m_analyticsPage = new AnalyticsPage(m_core, m_pages);
    m_zones = new ZonesPage(m_core, m_pages);
    m_eventsPage = new EventsPage(m_core, m_pages);
    m_media = new MediaPage(m_core, m_pages);
    m_camerasPage = new CamerasPage(m_core, m_pages);
    m_settingsPage = new SettingsPage(m_core, m_pages);
    m_usersPage = new UsersPage(m_core, m_pages);
    for (QWidget* p : { static_cast<QWidget*>(m_live), static_cast<QWidget*>(m_analyticsPage), static_cast<QWidget*>(m_zones),
                        static_cast<QWidget*>(m_eventsPage), static_cast<QWidget*>(m_media), static_cast<QWidget*>(m_camerasPage),
                        static_cast<QWidget*>(m_settingsPage), static_cast<QWidget*>(m_usersPage) })
        m_pages->addWidget(p);

    auto* central = new QWidget(this);
    auto* v = new QVBoxLayout(central);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);
    m_topBar = buildTopBar();
    v->addWidget(m_topBar);
    auto* h = new QHBoxLayout;
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(0);
    m_sideBar = buildSideBar();
    h->addWidget(m_sideBar);
    h->addWidget(m_pages, 1);
    v->addLayout(h, 1);
    setCentralWidget(central);
    statusBar()->showMessage(tr("Ready. Video is shown live; press Start Analysis to run people counting and security analytics."));

    // Core wiring
    CameraManager* cams = m_core.cameras;
    connect(cams, &CameraManager::eventRaised, m_core.events, &EventManager::addEvent);
    connect(cams, &CameraManager::analysisResult, m_core.analytics, &AnalyticsStore::onResult);
    connect(cams, &CameraManager::analysisStateChanged, this, &MainWindow::onAnalysisState);
    connect(cams, &CameraManager::cameraStatusChanged, this, [this](int id, CameraStatus s) {
        if (s != CameraStatus::Live && s != CameraStatus::Playing && s != CameraStatus::Still)
            m_core.analytics->setCameraInactive(id);
        updateSideStatus();
    });
    connect(cams, &CameraManager::cameraAnalysisChanged, this, [this](int id, bool on) {
        if (!on)
            m_core.analytics->setCameraInactive(id);
    });
    connect(cams, &CameraManager::camerasChanged, this, [this] {
        refreshLocations();
        updateSideStatus();
    });
    connect(m_core.events, &EventManager::eventAdded, this, &MainWindow::onEventAdded);
    connect(m_core.events, &EventManager::eventsChanged, this, &MainWindow::updateAlertBadge);
    connect(cams, &CameraManager::cameraRemoved, m_core.analytics, &AnalyticsStore::removeCamera);
    connect(m_core.analytics, &AnalyticsStore::dayChanged, cams, &CameraManager::resetCounters);

    // Page wiring
    connect(m_live, &LiveViewPage::addCameraRequested, this, [this] {
        if (Session::instance().can(Permission::ManageCameras))
            m_camerasPage->addCamera();
    });
    connect(m_live, &LiveViewPage::fileDropped, this, [this](const QString& p) {
        if (Session::instance().can(Permission::ManageCameras))
            m_camerasPage->addFileSources({ p });
    });
    connect(m_live, &LiveViewPage::editZonesRequested, this, [this](int id) {
        if (!canOpen(PageZones))
            return;
        navigate(PageZones);
        m_zones->editCamera(id);
    });
    connect(m_live, &LiveViewPage::editCameraRequested, this, [this](int id) {
        if (Session::instance().can(Permission::ManageCameras))
            m_camerasPage->editCamera(id);
    });
    connect(m_live, &LiveViewPage::viewAllEventsRequested, this, [this] { navigate(PageEvents); });
    connect(m_live, &LiveViewPage::fullScreenRequested, this, [this] { setVideoWall(!m_videoWall); });
    connect(m_media, &MediaPage::addVideoSourceRequested, this, [this](const QString& p) {
        if (m_camerasPage->addFileSources({ p }) > 0)
            navigate(PageLive);
    });
    connect(m_settingsPage, &SettingsPage::settingsSaved, this, [this] {
        m_live->grid()->applyDisplayOptions();
        m_siteLabel->setText(AppSettings::instance().siteName);
    });
    auto status = [this](const QString& text) { showStatus(text); };
    connect(m_live, &LiveViewPage::statusMessage, this, status);
    connect(m_analyticsPage, &AnalyticsPage::statusMessage, this, status);
    connect(m_zones, &ZonesPage::statusMessage, this, status);
    connect(m_eventsPage, &EventsPage::statusMessage, this, status);
    connect(m_media, &MediaPage::statusMessage, this, status);
    connect(m_camerasPage, &CamerasPage::statusMessage, this, status);
    connect(m_settingsPage, &SettingsPage::statusMessage, this, status);
    connect(m_usersPage, &UsersPage::statusMessage, this, status);
    connect(m_usersPage, &UsersPage::usersChanged, this, [this] {
        Session::instance().refresh();
        updateUserButton();
        updateUsersBadge();
    });

    setupTray();
    setupShortcuts();

    m_clock.setInterval(1000);
    connect(&m_clock, &QTimer::timeout, this, [this] {
        const QDateTime now = QDateTime::currentDateTime();
        m_timeLabel->setText(now.toString(QStringLiteral("HH:mm:ss")));
        m_dateLabel->setText(now.toString(QStringLiteral("yyyy-MM-dd  dddd")));
        if (now.time().second() % 2 == 0) {
            m_monitor.sample();
            m_cpuLabel->setText(tr("CPU %1%\nRAM %2 MB").arg(qRound(m_monitor.cpuPercent())).arg(qRound(m_monitor.processMemoryMB())));
            if (m_core.cameras->isAnalysisRunning())
                updateSideStatus();
        }
    });
    m_clock.start();
    m_monitor.sample();

    // Start cameras (video only - analysis waits for the operator).
    cams->load();
    refreshLocations();
    updateAlertBadge();
    updateSideStatus();
    onAnalysisState(AnalysisState::Stopped, QString());
    applyPermissions();
    navigate(PageLive);
    m_core.events->system(tr("Application started (%1 cameras)").arg(cams->count()));
    m_core.events->account(tr("%1 (%2) signed in").arg(Session::instance().user().username,
                                                       userRoleName(Session::instance().user().role)));
    {
        const AppSettings& st = AppSettings::instance();
        QtConcurrent::run(&MainWindow::cleanupOldMedia, st.snapshotDir, st.recordDir, st.retentionDays);
    }

    if (AppSettings::instance().autoStartAnalysis)
        QTimer::singleShot(1500, cams, &CameraManager::startAnalysis);
}

MainWindow::~MainWindow()
{
    m_core.cameras->shutdown();
    m_core.analytics->flush();
}

// ---------------------------------------------------------------------------
// Chrome
// ---------------------------------------------------------------------------

QWidget* MainWindow::buildTopBar()
{
    auto* bar = new QWidget(this);
    bar->setObjectName(QStringLiteral("TopBar"));
    bar->setAttribute(Qt::WA_StyledBackground);
    bar->setFixedHeight(66);
    auto* h = new QHBoxLayout(bar);
    h->setContentsMargins(18, 8, 18, 8);
    h->setSpacing(14);

    auto* logo = new QLabel(bar);
    logo->setPixmap(Theme::pixmap(QStringLiteral("logo"), QColor(), 38));
    h->addWidget(logo);
    auto* titles = new QVBoxLayout;
    titles->setSpacing(0);
    auto* title = new QLabel(tr("Surveillance System"), bar);
    title->setObjectName(QStringLiteral("AppTitle"));
    auto* sub = new QLabel(tr("Crowd Analytics & Security Platform"), bar);
    sub->setObjectName(QStringLiteral("AppSubtitle"));
    titles->addWidget(title);
    titles->addWidget(sub);
    h->addLayout(titles);
    h->addSpacing(24);

    // Site + location filter
    auto* site = new QFrame(bar);
    site->setObjectName(QStringLiteral("SitePill"));
    site->setStyleSheet(QStringLiteral("QFrame#SitePill{background:#14223a;border:1px solid #2a3e62;border-radius:10px;}"));
    auto* sh = new QHBoxLayout(site);
    sh->setContentsMargins(10, 4, 6, 4);
    sh->setSpacing(8);
    auto* pin = new QLabel(site);
    pin->setPixmap(Theme::pixmap(QStringLiteral("pin"), Theme::accent2(), 16));
    sh->addWidget(pin);
    m_siteLabel = new QLabel(AppSettings::instance().siteName, site);
    m_siteLabel->setStyleSheet(QStringLiteral("font-weight:600;"));
    sh->addWidget(m_siteLabel);
    m_location = new QComboBox(site);
    m_location->setMinimumWidth(170);
    m_location->setToolTip(tr("Show cameras of one location"));
    m_location->setStyleSheet(QStringLiteral("QComboBox{background:transparent;border:none;}"));
    sh->addWidget(m_location);
    h->addWidget(site);
    connect(m_location, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this] { m_live->setLocationFilter(m_location->currentData().toString()); });

    h->addStretch(1);

    m_analysisBtn = new QPushButton(bar);
    m_analysisBtn->setObjectName(QStringLiteral("StartAnalysis"));
    m_analysisBtn->setCursor(Qt::PointingHandCursor);
    m_analysisBtn->setIconSize(QSize(16, 16));
    m_analysisBtn->setMinimumWidth(170);
    m_analysisBtn->setToolTip(tr("Start or stop AI analysis on all cameras (F5).\n"
                                 "The detection model is only loaded while analysis runs."));
    connect(m_analysisBtn, &QPushButton::clicked, this, &MainWindow::toggleAnalysis);
    h->addWidget(m_analysisBtn);

    auto sep = [bar]() {
        auto* f = new QFrame(bar);
        f->setObjectName(QStringLiteral("TopSeparator"));
        f->setFixedHeight(34);
        return f;
    };
    h->addWidget(sep());
    auto* cpuIcon = new QLabel(bar);
    cpuIcon->setPixmap(Theme::pixmap(QStringLiteral("cpu"), Theme::text2(), 20));
    h->addWidget(cpuIcon);
    m_cpuLabel = makeLabel(QString(), "muted", bar);
    m_cpuLabel->setMinimumWidth(82);
    h->addWidget(m_cpuLabel);
    h->addWidget(sep());

    auto* clockCol = new QVBoxLayout;
    clockCol->setSpacing(0);
    m_timeLabel = new QLabel(bar);
    m_timeLabel->setObjectName(QStringLiteral("ClockTime"));
    m_dateLabel = new QLabel(bar);
    m_dateLabel->setObjectName(QStringLiteral("ClockDate"));
    clockCol->addWidget(m_timeLabel);
    clockCol->addWidget(m_dateLabel);
    h->addLayout(clockCol);
    h->addWidget(sep());

    auto* avatar = new QLabel(bar);
    avatar->setFixedSize(34, 34);
    avatar->setAlignment(Qt::AlignCenter);
    avatar->setStyleSheet(QStringLiteral("background:#1f3a66;border-radius:17px;"));
    avatar->setPixmap(Theme::pixmap(QStringLiteral("user"), QColor("#cfe0ff"), 18));
    h->addWidget(avatar);
    m_userButton = new QToolButton(bar);
    m_userButton->setPopupMode(QToolButton::InstantPopup);
    m_userButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_userButton->setCursor(Qt::PointingHandCursor);
    m_userButton->setStyleSheet(QStringLiteral("QToolButton{font-weight:600;padding:4px 8px;text-align:left;}"
                                               "QToolButton::menu-indicator{image:none;}"));
    auto* userMenu = new QMenu(m_userButton);
    userMenu->addAction(Theme::icon(QStringLiteral("lock")), tr("Change password..."), this, [this] {
        ChangePasswordDialog dlg(Session::instance().user().id, false, this);
        if (dlg.exec() == QDialog::Accepted) {
            m_core.events->account(tr("%1 changed their password").arg(Session::instance().user().username));
            showStatus(tr("Password changed."));
        }
    });
    userMenu->addAction(Theme::icon(QStringLiteral("people")), tr("Manage users..."), this, [this] { navigate(PageUsers); })
        ->setObjectName(QStringLiteral("manageUsers"));
    userMenu->addSeparator();
    userMenu->addAction(Theme::icon(QStringLiteral("power")), tr("Sign out"), this, &MainWindow::signOut);
    m_userButton->setMenu(userMenu);
    h->addWidget(m_userButton);

    const QDateTime now = QDateTime::currentDateTime();
    m_timeLabel->setText(now.toString(QStringLiteral("HH:mm:ss")));
    m_dateLabel->setText(now.toString(QStringLiteral("yyyy-MM-dd  dddd")));
    return bar;
}

QWidget* MainWindow::buildSideBar()
{
    auto* side = new QWidget(this);
    side->setObjectName(QStringLiteral("SideBar"));
    side->setAttribute(Qt::WA_StyledBackground);
    side->setFixedWidth(224);
    auto* v = new QVBoxLayout(side);
    v->setContentsMargins(12, 16, 12, 14);
    v->setSpacing(4);

    m_nav = new QButtonGroup(this);
    m_nav->setExclusive(true);
    const struct { const char* icon; QString text; QString key; } items[] = {
        { "live", tr("Live View"), QStringLiteral("Ctrl+1") },
        { "analytics", tr("Analytics"), QStringLiteral("Ctrl+2") },
        { "zones", tr("Zones & Rules"), QStringLiteral("Ctrl+3") },
        { "events", tr("Events & Alerts"), QStringLiteral("Ctrl+4") },
        { "media", tr("Snapshots & Video"), QStringLiteral("Ctrl+5") },
        { "camera", tr("Cameras"), QStringLiteral("Ctrl+6") },
        { "settings", tr("Settings"), QStringLiteral("Ctrl+7") },
        { "people", tr("Users"), QStringLiteral("Ctrl+8") },
    };
    int idx = 0;
    for (const auto& it : items) {
        auto* b = new QToolButton(side);
        b->setProperty("nav", true);
        b->setIcon(Theme::icon(QString::fromLatin1(it.icon), Theme::text2(), Qt::white));
        b->setIconSize(QSize(20, 20));
        b->setText(QStringLiteral("  ") + QString(it.text).replace(QLatin1Char('&'), QStringLiteral("&&")));   // & is a mnemonic marker
        b->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        b->setCheckable(true);
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        b->setMinimumHeight(42);
        b->setCursor(Qt::PointingHandCursor);
        b->setToolTip(QStringLiteral("%1  (%2)").arg(it.text, it.key));
        if (idx == PageEvents || idx == PageUsers) {
            auto* bl = new QHBoxLayout(b);
            bl->setContentsMargins(0, 0, 10, 0);
            bl->addStretch(1);
            auto* badge = new QLabel(b);
            badge->setObjectName(QStringLiteral("NavBadge"));
            badge->setAttribute(Qt::WA_TransparentForMouseEvents);
            badge->hide();
            bl->addWidget(badge);
            (idx == PageEvents ? m_alertBadge : m_usersBadge) = badge;
        }
        m_nav->addButton(b, idx++);
        v->addWidget(b);
    }
    connect(m_nav, &QButtonGroup::idClicked, this, &MainWindow::navigate);

    v->addStretch(1);

    auto* status = new QFrame(side);
    status->setObjectName(QStringLiteral("StatusCard"));
    auto* sv = new QVBoxLayout(status);
    sv->setContentsMargins(12, 10, 12, 10);
    sv->setSpacing(4);
    auto* row = new QHBoxLayout;
    row->setSpacing(8);
    m_sideStatusDot = new QLabel(status);
    m_sideStatusDot->setFixedSize(10, 10);
    row->addWidget(m_sideStatusDot);
    m_sideStatusText = new QLabel(status);
    m_sideStatusText->setStyleSheet(QStringLiteral("font-weight:600;"));
    row->addWidget(m_sideStatusText, 1);
    sv->addLayout(row);
    m_sideDetector = makeLabel(QString(), "muted", status);
    m_sideDetector->setWordWrap(true);
    sv->addWidget(m_sideDetector);
    m_sideCameras = makeLabel(QString(), "muted", status);
    sv->addWidget(m_sideCameras);
    sv->addWidget(makeLabel(tr("Version %1").arg(QStringLiteral(APP_VERSION)), "muted", status));
    v->addWidget(status);
    return side;
}

void MainWindow::setupTray()
{
    if (!QSystemTrayIcon::isSystemTrayAvailable())
        return;
    m_tray = new QSystemTrayIcon(windowIcon(), this);
    m_tray->setToolTip(tr("Surveillance System"));
    auto* menu = new QMenu(this);
    menu->addAction(tr("Show"), this, &MainWindow::showUserWindow);
    menu->addAction(tr("Start / Stop Analysis"), this, &MainWindow::toggleAnalysis);
    menu->addSeparator();
    menu->addAction(tr("Quit"), this, [this] {
        m_quitting = true;
        close();
    });
    m_tray->setContextMenu(menu);
    connect(m_tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason r) {
        if (r == QSystemTrayIcon::DoubleClick || r == QSystemTrayIcon::Trigger)
            showUserWindow();
    });
    m_tray->show();
}

void MainWindow::setupShortcuts()
{
    for (int i = 0; i < PageCount; ++i) {
        auto* sc = new QShortcut(QKeySequence(Qt::CTRL + Qt::Key_1 + i), this);
        connect(sc, &QShortcut::activated, this, [this, i] { navigate(i); });
    }
    connect(new QShortcut(QKeySequence(Qt::Key_F5), this), &QShortcut::activated, this, &MainWindow::toggleAnalysis);
    connect(new QShortcut(QKeySequence(Qt::Key_F11), this), &QShortcut::activated, this, [this] { setVideoWall(!m_videoWall); });
    connect(new QShortcut(QKeySequence(Qt::Key_F2), this), &QShortcut::activated, this, [this] {
        if (!Session::instance().can(Permission::CaptureMedia))
            return;
        const int id = m_live->grid()->selectedCameraId();
        const QString path = id >= 0 ? m_core.cameras->takeSnapshot(id) : QString();
        showStatus(path.isEmpty() ? tr("Select a camera with a picture to take a snapshot.")
                                  : tr("Snapshot saved: %1").arg(QDir::toNativeSeparators(path)));
    });
}

// ---------------------------------------------------------------------------
// Behaviour
// ---------------------------------------------------------------------------

void MainWindow::navigate(int page)
{
    if (page < 0 || page >= PageCount)
        return;
    if (!canOpen(page)) {
        showStatus(tr("Your role (%1) does not have access to this page.").arg(userRoleName(Session::instance().user().role)));
        if (QAbstractButton* b = m_nav->button(m_currentPage))
            b->setChecked(true);
        return;
    }
    if (m_currentPage == PageZones && page != PageZones && !m_zones->confirmLeave()) {
        if (QAbstractButton* b = m_nav->button(PageZones))
            b->setChecked(true);
        return;
    }
    if (m_videoWall && page != PageLive)
        setVideoWall(false);
    m_currentPage = page;
    m_pages->setCurrentIndex(page);
    if (QAbstractButton* b = m_nav->button(page))
        b->setChecked(true);
}

void MainWindow::toggleAnalysis()
{
    if (!Session::instance().can(Permission::ControlAnalysis)) {
        showStatus(tr("Only operators and administrators can start or stop analysis."));
        return;
    }
    CameraManager* cams = m_core.cameras;
    if (cams->analysisState() == AnalysisState::Stopped) {
        if (cams->count() == 0) {
            showStatus(tr("Add a camera, video or image first (Cameras page or drop a file on the grid)."));
            return;
        }
        cams->startAnalysis();
    } else {
        cams->stopAnalysis();
    }
}

void MainWindow::onAnalysisState(AnalysisState state, const QString& message)
{
    switch (state) {
    case AnalysisState::Stopped:
        m_analysisBtn->setText(tr("  Start Analysis"));
        m_analysisBtn->setIcon(Theme::icon(QStringLiteral("play"), Qt::white));
        m_analysisBtn->setProperty("state", "idle");
        for (const CameraConfig& c : m_core.cameras->cameras())
            m_core.analytics->setCameraInactive(c.id);
        break;
    case AnalysisState::Starting:
        m_analysisBtn->setText(tr("  Loading model..."));
        m_analysisBtn->setIcon(Theme::icon(QStringLiteral("clock"), Theme::warning()));
        m_analysisBtn->setProperty("state", "starting");
        break;
    case AnalysisState::Running:
        m_analysisBtn->setText(tr("  Stop Analysis"));
        m_analysisBtn->setIcon(Theme::icon(QStringLiteral("stop"), Qt::white));
        m_analysisBtn->setProperty("state", "running");
        break;
    }
    Theme::repolish(m_analysisBtn);
    updateSideStatus();
    if (!message.isEmpty())
        showStatus(message, 10000);
}

void MainWindow::onEventAdded(const EventRecord& e)
{
    const AppSettings& s = AppSettings::instance();
    if (e.severity >= Severity::Warning) {
        if (e.cameraId >= 0 && e.type != EventType::CameraOffline)
            m_live->grid()->raiseAlert(e.cameraId, eventTypeName(e.type), e.severity);
        if (s.alertSound)
            QApplication::beep();
        showStatus(QStringLiteral("%1  |  %2  |  %3").arg(eventTypeName(e.type), e.cameraName, e.message), 8000);
    }
    if (e.severity == Severity::Critical && s.trayNotify && m_tray)
        m_tray->showMessage(tr("%1 - %2").arg(eventTypeName(e.type), e.cameraName), e.message, QSystemTrayIcon::Critical, 6000);
}

void MainWindow::updateAlertBadge()
{
    const int n = m_core.events->unacknowledgedToday();
    if (!m_alertBadge)
        return;
    m_alertBadge->setVisible(n > 0);
    m_alertBadge->setText(n > 99 ? QStringLiteral("99+") : QString::number(n));
}

void MainWindow::updateSideStatus()
{
    const AnalysisState st = m_core.cameras->analysisState();
    QColor c = Theme::text3();
    QString text = tr("Analysis idle");
    QString detail = tr("Video only. Model not loaded.");
    if (st == AnalysisState::Starting) {
        c = Theme::warning();
        text = tr("Loading model");
        detail = tr("Preparing the detector...");
    } else if (st == AnalysisState::Running) {
        c = Theme::good();
        text = tr("Analysis running");
        detail = m_core.cameras->detectorInfo();
        double sum = 0.0;
        int n = 0;
        for (const CameraConfig& cam : m_core.cameras->cameras()) {
            const CameraLive live = m_core.analytics->camera(cam.id);
            if (live.active && live.inferenceMs > 0) {
                sum += live.inferenceMs;
                ++n;
            }
        }
        if (n > 0)
            detail += tr("\n%1 ms per frame").arg(qRound(sum / n));
    }
    m_sideStatusDot->setStyleSheet(QStringLiteral("background:%1;border-radius:5px;").arg(c.name()));
    m_sideStatusText->setText(text);
    m_sideDetector->setText(detail);
    m_sideCameras->setText(tr("Cameras: %1 / %2 online").arg(m_core.cameras->onlineCount()).arg(m_core.cameras->count()));
}

void MainWindow::refreshLocations()
{
    const QString cur = m_location->currentData().toString();
    m_location->blockSignals(true);
    m_location->clear();
    m_location->addItem(Theme::icon(QStringLiteral("grid")), tr("All locations"), QString());
    for (const QString& l : m_core.cameras->locations())
        m_location->addItem(l, l);
    const int i = m_location->findData(cur);
    m_location->setCurrentIndex(i >= 0 ? i : 0);
    m_location->blockSignals(false);
    m_live->setLocationFilter(m_location->currentData().toString());
}

void MainWindow::setVideoWall(bool on)
{
    if (on == m_videoWall)
        return;
    m_videoWall = on;
    if (on) {
        navigate(PageLive);
        m_wasMaximized = isMaximized();
        m_topBar->hide();
        m_sideBar->hide();
        statusBar()->hide();
        m_live->setVideoWallMode(true);
        showFullScreen();
    } else {
        m_topBar->show();
        m_sideBar->show();
        statusBar()->show();
        m_live->setVideoWallMode(false);
        if (m_wasMaximized)
            showMaximized();
        else
            showNormal();
    }
}

void MainWindow::showStatus(const QString& text, int ms)
{
    statusBar()->showMessage(text, ms);
}

void MainWindow::cleanupOldMedia(const QString& snapshotDir, const QString& recordDir, int retentionDays)
{
    // Runs on a worker thread: only touches the file system.
    const QDate cutoff = QDate::currentDate().addDays(-retentionDays);
    for (const QString& root : { snapshotDir, recordDir }) {
        const QDir dir(root);
        for (const QString& sub : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            const QDate d = QDate::fromString(sub, QStringLiteral("yyyy-MM-dd"));
            if (d.isValid() && d < cutoff)
                QDir(dir.filePath(sub)).removeRecursively();
        }
    }
}

void MainWindow::keyPressEvent(QKeyEvent* e)
{
    if (e->key() == Qt::Key_Escape && m_videoWall) {
        setVideoWall(false);
        return;
    }
    QMainWindow::keyPressEvent(e);
}

void MainWindow::closeEvent(QCloseEvent* e)
{
    if (!m_quitting && m_tray && AppSettings::instance().minimizeToTray) {
        hide();
        m_tray->showMessage(tr("Surveillance System"), tr("Still monitoring in the background. Double-click the tray icon to open."),
                            QSystemTrayIcon::Information, 3000);
        e->ignore();
        return;
    }
    if (!m_skipConfirm && Session::instance().isSignedIn()) {
        if (QMessageBox::question(this, tr("Exit"),
                                  tr("Exit the Surveillance System?\nMonitoring, analysis and recording will stop.")) !=
            QMessageBox::Yes) {
            m_quitting = false;
            e->ignore();
            return;
        }
    }
    if (!m_zones->confirmLeave()) {
        m_quitting = false;
        e->ignore();
        return;
    }
    AppSettings::instance().save();
    if (Session::instance().isSignedIn())
        m_core.events->account(tr("%1 closed the application").arg(Session::instance().user().username));
    m_core.events->system(tr("Application closed"));
    e->accept();
    QApplication::quit();
}

// ---------------------------------------------------------------------------
// Users & permissions
// ---------------------------------------------------------------------------

bool MainWindow::canOpen(int page) const
{
    const Session& ses = Session::instance();
    switch (page) {
    case PageZones:    return ses.can(Permission::EditZones);
    case PageCameras:  return ses.can(Permission::ManageCameras);
    case PageSettings: return ses.can(Permission::EditSettings);
    case PageUsers:    return ses.can(Permission::ManageUsers);
    default:           return ses.isSignedIn();
    }
}

void MainWindow::applyPermissions()
{
    for (int p = 0; p < PageCount; ++p)
        if (QAbstractButton* b = m_nav->button(p))
            b->setVisible(canOpen(p));
    const bool control = Session::instance().can(Permission::ControlAnalysis);
    m_analysisBtn->setEnabled(control);
    m_analysisBtn->setToolTip(control ? tr("Start or stop AI analysis on all cameras (F5).\n"
                                           "The detection model is only loaded while analysis runs.")
                                      : tr("Only operators and administrators can start or stop analysis."));
    if (QAction* a = m_userButton->menu()->findChild<QAction*>(QStringLiteral("manageUsers")))
        a->setVisible(canOpen(PageUsers));
    m_live->applyPermissions();
    m_eventsPage->applyPermissions();
    m_media->applyPermissions();
    m_analyticsPage->applyPermissions();
    updateUserButton();
    updateUsersBadge();
    if (!canOpen(m_currentPage))
        navigate(PageLive);
}

void MainWindow::updateUserButton()
{
    const UserAccount& u = Session::instance().user();
    m_userButton->setText(QStringLiteral("%1\n%2").arg(u.displayName, userRoleName(u.role)));
    m_userButton->setToolTip(tr("Signed in as %1").arg(u.username));
}

void MainWindow::updateUsersBadge()
{
    int pending = 0;
    if (Session::instance().can(Permission::ManageUsers))
        for (const UserAccount& u : UserManager::instance().users())
            pending += u.status == UserStatus::Pending ? 1 : 0;
    m_usersBadge->setVisible(pending > 0);
    m_usersBadge->setText(QString::number(pending));
    m_usersBadge->setToolTip(tr("%1 account(s) waiting for approval").arg(pending));
}

void MainWindow::showUserWindow()
{
    if (!Session::instance().isSignedIn()) {
        signOut();   // shows the sign-in window first
        return;
    }
    showMaximized();
    raise();
    activateWindow();
}

void MainWindow::signOut()
{
    if (Session::instance().isSignedIn()) {
        if (m_currentPage == PageZones && !m_zones->confirmLeave())
            return;
        m_core.events->account(tr("%1 signed out").arg(Session::instance().user().username));
        Session::instance().signOut();
    }
    if (m_videoWall)
        setVideoWall(false);
    hide();
    // Cameras, analysis and recordings keep running while nobody is signed in.
    LoginDialog dlg(nullptr, tr("Signed out. Monitoring, analysis and recording continue in the background."));
    if (dlg.exec() != QDialog::Accepted) {
        m_quitting = true;
        m_skipConfirm = true;
        close();
        return;
    }
    Session::instance().signIn(dlg.user());
    m_core.events->account(tr("%1 (%2) signed in").arg(dlg.user().username, userRoleName(dlg.user().role)));
    if (dlg.user().mustChangePassword) {
        ChangePasswordDialog pw(dlg.user().id, true, nullptr);
        if (pw.exec() == QDialog::Accepted)
            m_core.events->account(tr("%1 changed their password").arg(dlg.user().username));
    }
    applyPermissions();
    navigate(PageLive);
    showMaximized();
    raise();
    activateWindow();
}
