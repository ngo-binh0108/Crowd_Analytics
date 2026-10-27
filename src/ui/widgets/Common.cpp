#include "Common.h"
#include "ui/Theme.h"

#include <QHBoxLayout>
#include <QPainter>
#include <QProgressBar>
#include <QVBoxLayout>

QLabel* makeLabel(const QString& text, const char* role, QWidget* parent)
{
    auto* l = new QLabel(text, parent);
    if (role)
        Theme::setRole(l, role);
    return l;
}

QFrame* makeHLine(QWidget* parent)
{
    auto* f = new QFrame(parent);
    f->setFixedHeight(1);
    f->setStyleSheet(QStringLiteral("background:#1d2c47;"));
    return f;
}

static QLabel* iconBadge(const QString& iconName, const QColor& accent, int box, int iconSize, QWidget* parent)
{
    auto* l = new QLabel(parent);
    l->setFixedSize(box, box);
    l->setAlignment(Qt::AlignCenter);
    QColor bg = accent;
    bg.setAlpha(46);
    l->setStyleSheet(QStringLiteral("background:%1; border-radius:%2px;").arg(bg.name(QColor::HexArgb)).arg(box / 4));
    l->setPixmap(Theme::pixmap(iconName, accent, iconSize));
    return l;
}

// ---------------------------------------------------------------------------

Card::Card(const QString& title, const QString& iconName, QWidget* parent) : QFrame(parent)
{
    setProperty("card", true);
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(14, 12, 14, 12);
    outer->setSpacing(10);

    m_header = new QHBoxLayout;
    m_header->setSpacing(8);
    if (!iconName.isEmpty()) {
        auto* ic = new QLabel(this);
        ic->setPixmap(Theme::pixmap(iconName, Theme::accent2(), 16));
        ic->setStyleSheet(QStringLiteral("background:transparent;"));
        m_header->addWidget(ic);
    }
    if (!title.isEmpty() || !iconName.isEmpty()) {
        m_title = makeLabel(title, "cardTitle", this);
        m_header->addWidget(m_title);
        m_header->addStretch(1);
        outer->addLayout(m_header);
    }

    m_body = new QVBoxLayout;
    m_body->setContentsMargins(0, 0, 0, 0);
    m_body->setSpacing(8);
    outer->addLayout(m_body, 1);
}

void Card::addHeaderWidget(QWidget* w)
{
    m_header->addWidget(w);
}

void Card::setTitle(const QString& title)
{
    if (m_title)
        m_title->setText(title);
}

// ---------------------------------------------------------------------------

ToggleSwitch::ToggleSwitch(QWidget* parent) : QAbstractButton(parent)
{
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

QSize ToggleSwitch::sizeHint() const
{
    return QSize(38, 20);
}

void ToggleSwitch::enterEvent(QEvent*)
{
    m_hover = true;
    update();
}

void ToggleSwitch::leaveEvent(QEvent*)
{
    m_hover = false;
    update();
}

void ToggleSwitch::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = QRectF(rect()).adjusted(1, 1, -1, -1);
    QColor track = isChecked() ? Theme::accent() : QColor(0x24, 0x35, 0x55);
    if (!isEnabled())
        track = QColor(0x1a, 0x26, 0x3d);
    else if (m_hover)
        track = track.lighter(112);
    p.setPen(Qt::NoPen);
    p.setBrush(track);
    p.drawRoundedRect(r, r.height() / 2, r.height() / 2);
    const qreal d = r.height() - 4;
    const qreal x = isChecked() ? r.right() - d - 2 : r.left() + 2;
    p.setBrush(isEnabled() ? QColor(Qt::white) : Theme::text3());
    p.drawEllipse(QRectF(x, r.top() + 2, d, d));
}

// ---------------------------------------------------------------------------

KpiCard::KpiCard(const QString& title, const QString& iconName, const QColor& accent, QWidget* parent)
    : QFrame(parent)
{
    setProperty("card", true);
    setMinimumHeight(98);
    auto* lay = new QHBoxLayout(this);
    lay->setContentsMargins(14, 12, 14, 12);
    lay->setSpacing(12);
    lay->addWidget(iconBadge(iconName, accent, 40, 22, this), 0, Qt::AlignTop);

    auto* col = new QVBoxLayout;
    col->setSpacing(2);
    col->addWidget(makeLabel(title, "secondary", this));
    m_value = makeLabel(QStringLiteral("-"), "kpi", this);
    col->addWidget(m_value);
    m_caption = makeLabel(QString(), "muted", this);
    col->addWidget(m_caption);
    m_bar = new QProgressBar(this);
    m_bar->setRange(0, 100);
    m_bar->setTextVisible(false);
    m_bar->setFixedHeight(6);
    m_bar->hide();
    col->addWidget(m_bar);
    col->addStretch(1);
    lay->addLayout(col, 1);
}

void KpiCard::setValue(const QString& value)
{
    m_value->setText(value);
}

void KpiCard::setCaption(const QString& caption, const QColor& color)
{
    m_caption->setText(caption);
    m_caption->setStyleSheet(color.isValid() ? QStringLiteral("color:%1;").arg(color.name()) : QString());
}

void KpiCard::setProgress(int percent, const QColor& color)
{
    if (percent < 0) {
        m_bar->hide();
        return;
    }
    m_bar->show();
    m_bar->setValue(qBound(0, percent, 100));
    m_bar->setStyleSheet(QStringLiteral("QProgressBar::chunk{background:%1;border-radius:3px;}").arg(color.name()));
}

// ---------------------------------------------------------------------------

StatTile::StatTile(const QString& title, const QString& iconName, const QColor& accent, QWidget* parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("StatTile"));
    setStyleSheet(QStringLiteral("QFrame#StatTile{background:#14223a;border:1px solid #1f3050;border-radius:10px;}"));
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(10, 8, 10, 8);
    lay->setSpacing(2);
    auto* top = new QHBoxLayout;
    top->setSpacing(6);
    top->addWidget(iconBadge(iconName, accent, 22, 14, this));
    auto* t = makeLabel(title, "secondary", this);
    t->setStyleSheet(QStringLiteral("font-size:11px;"));
    top->addWidget(t, 1);
    lay->addLayout(top);
    m_value = makeLabel(QStringLiteral("0"), "kpiSmall", this);
    lay->addWidget(m_value);
    m_caption = makeLabel(QString(), "muted", this);
    lay->addWidget(m_caption);
}

void StatTile::setValue(const QString& value)
{
    m_value->setText(value);
}

void StatTile::setCaption(const QString& caption, const QColor& color)
{
    m_caption->setText(caption);
    m_caption->setStyleSheet(color.isValid() ? QStringLiteral("color:%1;font-size:11px;").arg(color.name())
                                             : QString());
}
