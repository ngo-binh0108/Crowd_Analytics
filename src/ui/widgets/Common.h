#pragma once

#include <QAbstractButton>
#include <QFrame>
#include <QLabel>

class QHBoxLayout;
class QProgressBar;
class QVBoxLayout;

QLabel* makeLabel(const QString& text, const char* role = nullptr, QWidget* parent = nullptr);
QFrame* makeHLine(QWidget* parent = nullptr);

// Rounded panel with a title row and a body layout.
class Card : public QFrame {
    Q_OBJECT
public:
    explicit Card(const QString& title = QString(), const QString& iconName = QString(), QWidget* parent = nullptr);

    QVBoxLayout* body() const { return m_body; }
    QHBoxLayout* header() const { return m_header; }
    void addHeaderWidget(QWidget* w);
    void setTitle(const QString& title);

private:
    QVBoxLayout* m_body;
    QHBoxLayout* m_header;
    QLabel* m_title = nullptr;
};

// iOS-style on/off switch.
class ToggleSwitch : public QAbstractButton {
    Q_OBJECT
public:
    explicit ToggleSwitch(QWidget* parent = nullptr);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent*) override;
    void enterEvent(QEvent*) override;
    void leaveEvent(QEvent*) override;

private:
    bool m_hover = false;
};

// Headline number with icon, caption and optional capacity bar.
class KpiCard : public QFrame {
    Q_OBJECT
public:
    KpiCard(const QString& title, const QString& iconName, const QColor& accent, QWidget* parent = nullptr);

    void setValue(const QString& value);
    void setCaption(const QString& caption, const QColor& color = QColor());
    void setProgress(int percent, const QColor& color);   // percent < 0 hides the bar
    void setTooltipText(const QString& text) { setToolTip(text); }

private:
    QLabel* m_value;
    QLabel* m_caption;
    QProgressBar* m_bar;
};

// Compact tile used in the Live View statistics block.
class StatTile : public QFrame {
    Q_OBJECT
public:
    StatTile(const QString& title, const QString& iconName, const QColor& accent, QWidget* parent = nullptr);
    void setValue(const QString& value);
    void setCaption(const QString& caption, const QColor& color = QColor());

private:
    QLabel* m_value;
    QLabel* m_caption;
};
