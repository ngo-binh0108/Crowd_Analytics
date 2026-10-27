#pragma once

#include "core/Types.h"
#include "ui/AppCore.h"

#include <QHash>
#include <QTimer>
#include <QWidget>

class CameraGrid;
class Card;
class HeatmapView;
class LineChart;
class QButtonGroup;
class QLabel;
class QListWidget;
class QScrollArea;
class QToolButton;
class QVBoxLayout;
class StatTile;
class ToggleSwitch;
class ZoneBars;
class QProgressBar;

class LiveViewPage : public QWidget {
    Q_OBJECT
public:
    LiveViewPage(const AppCore& core, QWidget* parent = nullptr);

    CameraGrid* grid() const { return m_grid; }
    void setLocationFilter(const QString& location);
    void setVideoWallMode(bool on);
    void refreshCameras();
    void showCameraMenu(int cameraId, const QPoint& globalPos);
    void applyPermissions();

signals:
    void addCameraRequested();
    void fileDropped(const QString& path);
    void editZonesRequested(int cameraId);
    void editCameraRequested(int cameraId);
    void viewAllEventsRequested();
    void fullScreenRequested();
    void statusMessage(const QString& text);

private:
    QWidget* buildRightPanel();
    QWidget* buildBottomStrip();
    void addFunctionRow(QVBoxLayout* lay, const QString& icon, const QString& label, const QString& tip, bool* setting);
    void updateStats();
    void updateFlowChart();
    void updateZoneBars();
    void updateHeatmap();
    void updateQuickControls();
    void addEventRow(const EventRecord& e);
    void addSnapshot(const QString& path, const QString& caption);
    void loadRecentSnapshots();
    int selectedCamera() const;

    AppCore m_core;
    QString m_location;
    CameraGrid* m_grid = nullptr;
    QLabel* m_subtitle = nullptr;
    QLabel* m_pageLabel = nullptr;
    QToolButton* m_prevBtn = nullptr;
    QToolButton* m_nextBtn = nullptr;
    QWidget* m_rightPanel = nullptr;
    QWidget* m_bottomStrip = nullptr;
    QWidget* m_headerRow = nullptr;

    StatTile* m_statPeople = nullptr;
    StatTile* m_statIn = nullptr;
    StatTile* m_statOut = nullptr;
    StatTile* m_statAlerts = nullptr;
    QLabel* m_occLabel = nullptr;
    QProgressBar* m_occBar = nullptr;

    QToolButton* m_recordBtn = nullptr;
    QToolButton* m_snapBtn = nullptr;
    QToolButton* m_resetBtn = nullptr;
    QToolButton* m_reconnectBtn = nullptr;
    QVector<ToggleSwitch*> m_fnSwitches;
    QToolButton* m_tourBtn = nullptr;
    QButtonGroup* m_layoutGroup = nullptr;
    QVBoxLayout* m_eventsLayout = nullptr;

    LineChart* m_flowChart = nullptr;
    ZoneBars* m_zoneBars = nullptr;
    HeatmapView* m_heatmap = nullptr;
    Card* m_heatCard = nullptr;
    QListWidget* m_snapshots = nullptr;

    QTimer m_slowTimer;
    QTimer m_heatTimer;
};
