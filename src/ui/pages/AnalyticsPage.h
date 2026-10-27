#pragma once

#include "core/Database.h"
#include "ui/AppCore.h"

#include <QTimer>
#include <QWidget>

class BarChart;
class GaugeWidget;
class HeatmapView;
class KpiCard;
class LineChart;
class QComboBox;
class QLabel;
class QPushButton;
class QTableWidget;

class AnalyticsPage : public QWidget {
    Q_OBJECT
public:
    AnalyticsPage(const AppCore& core, QWidget* parent = nullptr);
    void refresh();
    void refreshCameraLists();
    void applyPermissions();

signals:
    void statusMessage(const QString& text);

protected:
    void showEvent(QShowEvent* e) override;

private:
    void rangeFor(QDateTime& from, QDateTime& to, int& bucketMinutes, QString& labelFormat) const;
    int cameraFilter() const;
    void updateLive();
    void updateTable();
    void updateHeatmap();
    void exportCsv();
    void exportPdf();

    AppCore m_core;
    QWidget* m_content = nullptr;
    QComboBox* m_camera = nullptr;
    QComboBox* m_range = nullptr;
    QComboBox* m_heatCamera = nullptr;
    QPushButton* m_resetBtn = nullptr;
    QPushButton* m_heatResetBtn = nullptr;

    KpiCard* m_kIn = nullptr;
    KpiCard* m_kOut = nullptr;
    KpiCard* m_kNet = nullptr;
    KpiCard* m_kOcc = nullptr;
    KpiCard* m_kPeople = nullptr;
    KpiCard* m_kDwell = nullptr;

    LineChart* m_flow = nullptr;
    LineChart* m_people = nullptr;
    BarChart* m_hourly = nullptr;
    GaugeWidget* m_gauge = nullptr;
    QTableWidget* m_table = nullptr;
    HeatmapView* m_heat = nullptr;

    QVector<MinuteRow> m_series;
    QStringList m_labels;
    QTimer m_timer;
};
