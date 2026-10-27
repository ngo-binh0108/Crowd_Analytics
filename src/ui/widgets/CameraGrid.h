#pragma once

#include "core/Types.h"

#include <QHash>
#include <QTimer>
#include <QWidget>

class AnalyticsStore;
class CameraManager;
class QGridLayout;
class VideoTile;

// Grid of up to 32 video tiles with paging, single view and camera tour.
class CameraGrid : public QWidget {
    Q_OBJECT
public:
    static QVector<int> supportedLayouts() { return { 1, 4, 6, 9, 16, 25, 32 }; }

    CameraGrid(CameraManager* cameras, AnalyticsStore* store, QWidget* parent = nullptr);

    void setCameraIds(const QVector<int>& ids);
    void setLayoutCount(int n);
    int layoutCount() const { return m_layoutN; }
    int page() const { return m_page; }
    int pageCount() const;
    void nextPage();
    void prevPage();

    int selectedCameraId() const { return m_selected; }
    void selectCamera(int id);
    void toggleSingleView(int cameraId);
    bool isSingleView() const { return m_singleId >= 0; }
    void setTour(bool on, int seconds = 8);
    bool isTourRunning() const { return m_tour.isActive(); }

    void refreshConfig();
    void setCanAddCameras(bool on);
    void applyDisplayOptions();
    void raiseAlert(int cameraId, const QString& text, Severity severity);

signals:
    void selectionChanged(int cameraId);
    void pageChanged(int page, int pageCount);
    void layoutChanged(int n);
    void singleViewChanged(bool single);
    void addCameraRequested();
    void fileDropped(const QString& path);
    void contextMenuRequested(int cameraId, const QPoint& globalPos);

protected:
    void resizeEvent(QResizeEvent* e) override;

private:
    void rebuild();
    void updateDisplayHints();
    void updateHeatmaps();
    VideoTile* tileFor(int cameraId) const { return m_tileFor.value(cameraId, nullptr); }
    static void gridDims(int n, int& cols, int& rows);

    CameraManager* m_cams;
    AnalyticsStore* m_store;
    QGridLayout* m_grid;
    QVector<VideoTile*> m_tiles;
    QHash<int, VideoTile*> m_tileFor;
    QVector<int> m_ids;
    int m_layoutN = 4;
    int m_page = 0;
    int m_selected = -1;
    int m_singleId = -1;
    int m_tourPos = 0;
    QTimer m_tour;
    QTimer m_heatTimer;
    QTimer m_hintTimer;
};
