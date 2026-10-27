#pragma once

#include "core/Types.h"

#include <QImage>
#include <QWidget>

// Interactive editor for polygon zones and counting lines drawn over a
// camera still. Geometry is stored normalized to the frame.
class ZoneEditor : public QWidget {
    Q_OBJECT
public:
    enum class Tool { Select, Polygon, Line };
    enum Kind { KindNone = -1, KindZone = 0, KindLine = 1 };

    explicit ZoneEditor(QWidget* parent = nullptr);

    void setFrame(const QImage& img);
    bool hasFrame() const { return !m_frame.isNull(); }
    void setShapes(const QVector<Zone>& zones, const QVector<CountLine>& lines);
    const QVector<Zone>& zones() const { return m_zones; }
    const QVector<CountLine>& lines() const { return m_lines; }

    void setTool(Tool t);
    Tool tool() const { return m_tool; }
    void select(int kind, int index);
    int selectedKind() const { return m_selKind; }
    int selectedIndex() const { return m_selIndex; }
    void deleteSelected();
    void clearAll();
    void updateZone(int index, const Zone& z);
    void updateLine(int index, const CountLine& l);

    QSize sizeHint() const override { return QSize(800, 450); }

signals:
    void shapesChanged();
    void selectionChanged(int kind, int index);
    void toolChanged(ZoneEditor::Tool tool);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;

private:
    enum class Drag { None, ZoneVertex, ZoneBody, LineEndA, LineEndB, LineBody };

    QRectF imageRect() const;
    QPointF toNorm(const QPointF& w) const;
    QPointF toWidget(const QPointF& n) const;
    void finishPolygon();
    void cancelDrawing();
    QString nextZoneName() const;

    QImage m_frame;
    QVector<Zone> m_zones;
    QVector<CountLine> m_lines;
    Tool m_tool = Tool::Select;
    int m_selKind = KindNone;
    int m_selIndex = -1;

    QVector<QPointF> m_draft;       // polygon being drawn (normalized)
    bool m_lineDraft = false;
    QPointF m_lineStart;
    QPointF m_cursor;               // widget coordinates

    Drag m_drag = Drag::None;
    int m_dragIndex = -1;
    int m_dragVertex = -1;
    QPointF m_dragLast;             // normalized
};
