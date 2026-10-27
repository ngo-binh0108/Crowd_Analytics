#pragma once

#include "core/Types.h"

#include <QColor>
#include <QImage>
#include <QStringList>
#include <QVector>
#include <QWidget>

struct ChartSeries {
    QString name;
    QColor color;
    QVector<double> values;
    bool area = false;
};

// Time-series line/area chart with crosshair tooltip.
class LineChart : public QWidget {
    Q_OBJECT
public:
    explicit LineChart(QWidget* parent = nullptr);
    void setData(const QStringList& xLabels, const QVector<ChartSeries>& series);
    void setEmptyText(const QString& t) { m_emptyText = t; update(); }
    void setDecimals(int d) { m_decimals = d; }
    QSize sizeHint() const override { return QSize(420, 200); }
    QSize minimumSizeHint() const override { return QSize(200, 120); }

protected:
    void paintEvent(QPaintEvent*) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void leaveEvent(QEvent*) override;

private:
    QRectF plotRect() const;
    QStringList m_labels;
    QVector<ChartSeries> m_series;
    QString m_emptyText;
    int m_hover = -1;
    int m_decimals = 0;
};

// Vertical bar chart (e.g. entries per hour) with a highlighted bar.
class BarChart : public QWidget {
    Q_OBJECT
public:
    explicit BarChart(QWidget* parent = nullptr);
    void setData(const QStringList& labels, const QVector<double>& values, const QColor& color);
    void setHighlight(int index, const QColor& color, const QString& note);
    void setEmptyText(const QString& t) { m_emptyText = t; update(); }
    QSize sizeHint() const override { return QSize(420, 200); }
    QSize minimumSizeHint() const override { return QSize(200, 120); }

protected:
    void paintEvent(QPaintEvent*) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void leaveEvent(QEvent*) override;

private:
    QRectF plotRect() const;
    QStringList m_labels;
    QVector<double> m_values;
    QColor m_color;
    int m_highlight = -1;
    QColor m_highlightColor;
    QString m_highlightNote;
    QString m_emptyText;
    int m_hover = -1;
};

// Semicircular occupancy gauge.
class GaugeWidget : public QWidget {
    Q_OBJECT
public:
    explicit GaugeWidget(QWidget* parent = nullptr);
    void setValue(int value, int capacity, DensityLevel level, const QString& caption);
    QSize sizeHint() const override { return QSize(220, 170); }

protected:
    void paintEvent(QPaintEvent*) override;

private:
    int m_value = 0;
    int m_capacity = 0;
    DensityLevel m_level = DensityLevel::Low;
    QString m_caption;
};

struct ZoneBarItem {
    QString name;
    QString subtitle;
    int count = 0;
    int capacity = 0;
    DensityLevel level = DensityLevel::Low;
};

// List of zones with occupancy bars coloured by crowd level.
class ZoneBars : public QWidget {
    Q_OBJECT
public:
    explicit ZoneBars(QWidget* parent = nullptr);
    void setItems(const QVector<ZoneBarItem>& items);
    void setEmptyText(const QString& t) { m_emptyText = t; update(); }
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override { return QSize(180, 80); }

protected:
    void paintEvent(QPaintEvent*) override;

private:
    QVector<ZoneBarItem> m_items;
    QString m_emptyText;
};

// Camera still with a heat overlay and a Low->High legend.
class HeatmapView : public QWidget {
    Q_OBJECT
public:
    explicit HeatmapView(QWidget* parent = nullptr);
    void setImages(const QImage& background, const QImage& heat);
    void setEmptyText(const QString& t) { m_emptyText = t; update(); }
    QSize sizeHint() const override { return QSize(320, 200); }

protected:
    void paintEvent(QPaintEvent*) override;

private:
    QImage m_bg;
    QImage m_heat;
    QString m_emptyText;
};

namespace ChartUtil {
double niceCeil(double v);
QColor infernoColor(double t);   // 0..1 sequential scale used by heatmaps
}
