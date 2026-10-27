#pragma once

#include "core/CameraManager.h"
#include "core/SystemMonitor.h"
#include "ui/AppCore.h"

#include <QMainWindow>
#include <QSystemTrayIcon>
#include <QTimer>

class AnalyticsPage;
class AnalyticsStore;
class CamerasPage;
class EventManager;
class EventsPage;
class LiveViewPage;
class MediaPage;
class QButtonGroup;
class QComboBox;
class QLabel;
class QPushButton;
class QStackedWidget;
class QToolButton;
class SettingsPage;
class UsersPage;
class ZonesPage;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    void quitWithoutPrompt()
    {
        m_quitting = true;
        m_skipConfirm = true;
        close();
    }

    void showPage(int page) { navigate(page); }

protected:
    void closeEvent(QCloseEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;

private:
    enum Page { PageLive = 0, PageAnalytics, PageZones, PageEvents, PageMedia, PageCameras, PageSettings, PageUsers, PageCount };

    QWidget* buildTopBar();
    QWidget* buildSideBar();
    void setupTray();
    void setupShortcuts();
    void navigate(int page);
    bool canOpen(int page) const;
    void applyPermissions();
    void updateUserButton();
    void updateUsersBadge();
    void signOut();
    void showUserWindow();
    void toggleAnalysis();
    void onAnalysisState(AnalysisState state, const QString& message);
    void onEventAdded(const EventRecord& e);
    void updateAlertBadge();
    void updateSideStatus();
    void refreshLocations();
    void setVideoWall(bool on);
    void showStatus(const QString& text, int ms = 6000);
    static void cleanupOldMedia(const QString& snapshotDir, const QString& recordDir, int retentionDays);

    AppCore m_core;
    SystemMonitor m_monitor;

    QStackedWidget* m_pages = nullptr;
    LiveViewPage* m_live = nullptr;
    AnalyticsPage* m_analyticsPage = nullptr;
    ZonesPage* m_zones = nullptr;
    EventsPage* m_eventsPage = nullptr;
    MediaPage* m_media = nullptr;
    CamerasPage* m_camerasPage = nullptr;
    SettingsPage* m_settingsPage = nullptr;
    UsersPage* m_usersPage = nullptr;

    QWidget* m_topBar = nullptr;
    QWidget* m_sideBar = nullptr;
    QButtonGroup* m_nav = nullptr;
    QLabel* m_alertBadge = nullptr;
    QPushButton* m_analysisBtn = nullptr;
    QComboBox* m_location = nullptr;
    QLabel* m_siteLabel = nullptr;
    QLabel* m_cpuLabel = nullptr;
    QLabel* m_timeLabel = nullptr;
    QLabel* m_dateLabel = nullptr;
    QToolButton* m_userButton = nullptr;
    QLabel* m_usersBadge = nullptr;
    QLabel* m_sideStatusDot = nullptr;
    QLabel* m_sideStatusText = nullptr;
    QLabel* m_sideDetector = nullptr;
    QLabel* m_sideCameras = nullptr;

    QSystemTrayIcon* m_tray = nullptr;
    QTimer m_clock;
    int m_currentPage = PageLive;
    bool m_videoWall = false;
    bool m_quitting = false;
    bool m_skipConfirm = false;
    bool m_wasMaximized = true;
};
