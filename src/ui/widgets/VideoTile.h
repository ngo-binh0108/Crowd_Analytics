#pragma once

#include "core/Overlay.h"
#include "core/Types.h"

#include <QImage>
#include <QWidget>

// One camera panel in the live grid: video, analysis overlay, badges.
class VideoTile : public QWidget {
    Q_OBJECT
public:
    explicit VideoTile(int index, QWidget* parent = nullptr);

    int index() const { return m_index; }
    int cameraId() const { return m_cameraId; }

    void setCamera(int cameraId, const CameraConfig* cfg);
    void setFrame(const QImage& img);
    void setStatus(CameraStatus s, const QString& message);
    void setResult(const AnalysisResult& r);
    void clearResult();
    void setAnalyzing(bool on);
    void setRecording(bool on);
    void setSelected(bool on);
    void setHeatmap(const QImage& heat);
    void setOverlayOptions(const OverlayOptions& o);
    void setFlowTotals(qint64 in, qint64 out);
    void raiseAlert(const QString& text, Severity severity);
    void setCanAdd(bool on);
    const QImage& frame() const { return m_frame; }

signals:
    void clicked(int index);
    void doubleClicked(int index);
    void contextMenuRequested(int index, const QPoint& globalPos);
    void fileDropped(int index, const QString& path);
    void addRequested(int index);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void contextMenuEvent(QContextMenuEvent* e) override;
    void enterEvent(QEvent*) override;
    void leaveEvent(QEvent*) override;
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dragLeaveEvent(QDragLeaveEvent* e) override;
    void dropEvent(QDropEvent* e) override;

private:
    QRectF imageRect() const;
    void paintEmpty(QPainter& p, const QRectF& r);
    void paintHeader(QPainter& p, const QRectF& r);
    void paintChips(QPainter& p, const QRectF& img);
    bool alertActive() const;

    const int m_index;
    int m_cameraId = -1;
    QString m_name;
    SourceType m_type = SourceType::Video;
    QVector<Zone> m_zones;
    QVector<CountLine> m_lines;

    QImage m_frame;
    QImage m_heat;
    CameraStatus m_status = CameraStatus::Stopped;
    QString m_statusMsg;
    AnalysisResult m_result;
    bool m_hasResult = false;
    bool m_analyzing = false;
    bool m_recording = false;
    bool m_selected = false;
    bool m_hover = false;
    bool m_dragOver = false;
    bool m_canAdd = true;
    OverlayOptions m_opts;
    qint64 m_in = 0;
    qint64 m_out = 0;

    QString m_alertText;
    Severity m_alertSeverity = Severity::Info;
    qint64 m_alertUntilMs = 0;
};
