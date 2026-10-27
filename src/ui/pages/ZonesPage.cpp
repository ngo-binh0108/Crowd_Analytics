#include "ZonesPage.h"
#include "core/CameraManager.h"
#include "core/CameraWorker.h"
#include "ui/Theme.h"
#include "ui/widgets/Common.h"
#include "ui/widgets/ZoneEditor.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

QIcon swatch(const QColor& c)
{
    QPixmap pm(14, 14);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    p.drawRoundedRect(QRectF(1, 1, 12, 12), 3, 3);
    return QIcon(pm);
}

void styleColorButton(QPushButton* b, const QColor& c)
{
    b->setIcon(swatch(c));
    b->setText(c.name().toUpper());
    b->setProperty("color", c);
}

QToolButton* toolButton(const QString& icon, const QString& text, QWidget* parent)
{
    auto* b = new QToolButton(parent);
    b->setIcon(Theme::icon(icon, Theme::text2(), Qt::white));
    b->setText(text);
    b->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    b->setIconSize(QSize(16, 16));
    b->setProperty("tool", true);
    b->setCheckable(true);
    b->setAutoExclusive(true);
    b->setCursor(Qt::PointingHandCursor);
    return b;
}

} // namespace

ZonesPage::ZonesPage(const AppCore& core, QWidget* parent) : QWidget(parent), m_core(core)
{
    setObjectName(QStringLiteral("Page"));
    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(16, 14, 16, 14);
    root->setSpacing(14);

    // Left: cameras
    auto* camCard = new Card(tr("Cameras"), QStringLiteral("camera"), this);
    camCard->setFixedWidth(230);
    m_cameraList = new QListWidget(camCard);
    m_cameraList->setIconSize(QSize(16, 16));
    camCard->body()->addWidget(m_cameraList);
    root->addWidget(camCard);

    // Center: toolbar + editor
    auto* center = new QVBoxLayout;
    center->setSpacing(10);
    auto* tb = new QHBoxLayout;
    tb->setSpacing(6);
    auto* titleCol = new QVBoxLayout;
    titleCol->setSpacing(0);
    titleCol->addWidget(makeLabel(tr("Zones & Rules"), "pageTitle", this));
    m_title = makeLabel(tr("Select a camera"), "secondary", this);
    titleCol->addWidget(m_title);
    tb->addLayout(titleCol);
    tb->addStretch(1);
    m_selectBtn = toolButton(QStringLiteral("pointer"), tr("Select"), this);
    m_polyBtn = toolButton(QStringLiteral("polygon"), tr("Draw Zone"), this);
    m_lineBtn = toolButton(QStringLiteral("line"), tr("Draw Line"), this);
    m_selectBtn->setChecked(true);
    tb->addWidget(m_selectBtn);
    tb->addWidget(m_polyBtn);
    tb->addWidget(m_lineBtn);
    tb->addSpacing(10);
    auto* delBtn = new QPushButton(Theme::icon(QStringLiteral("trash")), tr("Delete"), this);
    auto* clearBtn = new QPushButton(tr("Clear All"), this);
    auto* frameBtn = new QPushButton(Theme::icon(QStringLiteral("refresh")), tr("Refresh Frame"), this);
    m_revertBtn = new QPushButton(tr("Revert"), this);
    m_saveBtn = new QPushButton(Theme::icon(QStringLiteral("save"), Qt::white), tr("Save"), this);
    m_saveBtn->setProperty("variant", "primary");
    tb->addWidget(delBtn);
    tb->addWidget(clearBtn);
    tb->addWidget(frameBtn);
    tb->addSpacing(10);
    tb->addWidget(m_revertBtn);
    tb->addWidget(m_saveBtn);
    center->addLayout(tb);

    auto* editorCard = new Card(QString(), QString(), this);
    m_editor = new ZoneEditor(editorCard);
    editorCard->body()->addWidget(m_editor);
    center->addWidget(editorCard, 1);
    auto* help = makeLabel(tr("Zones count people entering / leaving an area and measure how crowded it is. "
                              "Counting lines count crossings in each direction (arrow = IN). "
                              "Restricted zones raise an intrusion alert; Queue zones alert when the queue exceeds its limit."),
                           "hint", this);
    help->setWordWrap(true);
    center->addWidget(help);
    root->addLayout(center, 1);

    // Right: shapes + properties
    auto* right = new QVBoxLayout;
    right->setSpacing(12);
    auto* shapesCard = new Card(tr("Zones & Lines"), QStringLiteral("layers"), this);
    shapesCard->setFixedWidth(300);
    m_shapeList = new QListWidget(shapesCard);
    m_shapeList->setIconSize(QSize(14, 14));
    m_shapeList->setMinimumHeight(150);
    shapesCard->body()->addWidget(m_shapeList);
    right->addWidget(shapesCard, 1);

    auto* propCard = new Card(tr("Properties"), QStringLiteral("edit"), this);
    propCard->setFixedWidth(300);
    m_props = new QStackedWidget(propCard);
    auto* empty = makeLabel(tr("Select a zone or line to edit its settings."), "muted", m_props);
    empty->setAlignment(Qt::AlignCenter);
    empty->setWordWrap(true);
    m_props->addWidget(empty);

    auto* zoneForm = new QWidget(m_props);
    auto* zf = new QFormLayout(zoneForm);
    zf->setContentsMargins(0, 0, 0, 0);
    zf->setSpacing(8);
    m_zName = new QLineEdit(zoneForm);
    m_zType = new QComboBox(zoneForm);
    m_zType->addItems({ zoneTypeName(ZoneType::Counting), zoneTypeName(ZoneType::Restricted), zoneTypeName(ZoneType::Queue) });
    m_zCapacity = new QSpinBox(zoneForm);
    m_zCapacity->setRange(0, 100000);
    m_zCapacity->setSuffix(tr(" people"));
    m_zArea = new QDoubleSpinBox(zoneForm);
    m_zArea->setRange(0, 1000000);
    m_zArea->setDecimals(1);
    m_zArea->setSuffix(QStringLiteral(" m²"));
    m_zArea->setToolTip(tr("Optional floor area; enables people per square metre"));
    m_zLoiter = new QSpinBox(zoneForm);
    m_zLoiter->setRange(0, 86400);
    m_zLoiter->setSuffix(tr(" s"));
    m_zLoiter->setSpecialValueText(tr("Off"));
    m_zColor = new QPushButton(zoneForm);
    m_zTotals = new QCheckBox(tr("Include in site In / Out totals"), zoneForm);
    m_zTotals->setToolTip(tr("Ignored when this camera has a counting line: the line is then used for the site totals, "
                             "so people are not counted twice."));
    m_zEnabled = new QCheckBox(tr("Enabled"), zoneForm);
    m_zCapLabel = new QLabel(tr("Capacity"), zoneForm);
    zf->addRow(tr("Name"), m_zName);
    zf->addRow(tr("Type"), m_zType);
    zf->addRow(m_zCapLabel, m_zCapacity);
    zf->addRow(tr("Area"), m_zArea);
    zf->addRow(tr("Loitering after"), m_zLoiter);
    zf->addRow(tr("Colour"), m_zColor);
    zf->addRow(m_zTotals);
    zf->addRow(m_zEnabled);
    m_props->addWidget(zoneForm);

    auto* lineForm = new QWidget(m_props);
    auto* lf = new QFormLayout(lineForm);
    lf->setContentsMargins(0, 0, 0, 0);
    lf->setSpacing(8);
    m_lName = new QLineEdit(lineForm);
    m_lInvert = new QCheckBox(tr("Swap IN / OUT direction"), lineForm);
    m_lOneWay = new QCheckBox(tr("One-way: alert on OUT crossing"), lineForm);
    m_lOneWay->setToolTip(tr("Raises a Wrong Way alert when someone crosses against the arrow"));
    m_lColor = new QPushButton(lineForm);
    m_lTotals = new QCheckBox(tr("Include in site In / Out totals"), lineForm);
    m_lEnabled = new QCheckBox(tr("Enabled"), lineForm);
    lf->addRow(tr("Name"), m_lName);
    lf->addRow(m_lInvert);
    lf->addRow(m_lOneWay);
    lf->addRow(tr("Colour"), m_lColor);
    lf->addRow(m_lTotals);
    lf->addRow(m_lEnabled);
    m_props->addWidget(lineForm);
    propCard->body()->addWidget(m_props);
    right->addWidget(propCard);

    auto* camRules = new Card(tr("Camera Rules"), QStringLiteral("shield"), this);
    camRules->setFixedWidth(300);
    auto* cf = new QFormLayout;
    m_camCapacity = new QSpinBox(camRules);
    m_camCapacity->setRange(0, 100000);
    m_camCapacity->setSpecialValueText(tr("Off"));
    m_camCapacity->setSuffix(tr(" people"));
    m_camCapacity->setToolTip(tr("Crowd alert for the whole camera view, without drawing zones"));
    cf->addRow(tr("View capacity"), m_camCapacity);
    camRules->body()->addLayout(cf);
    right->addWidget(camRules);
    root->addLayout(right);

    // Wiring
    connect(m_cameraList, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row < 0)
            return;
        const int id = m_cameraList->item(row)->data(Qt::UserRole).toInt();
        if (id == m_cameraId)
            return;
        if (!confirmLeave()) {
            m_cameraList->blockSignals(true);
            for (int i = 0; i < m_cameraList->count(); ++i)
                if (m_cameraList->item(i)->data(Qt::UserRole).toInt() == m_cameraId)
                    m_cameraList->setCurrentRow(i);
            m_cameraList->blockSignals(false);
            return;
        }
        loadCamera(id);
    });
    connect(m_selectBtn, &QToolButton::clicked, this, [this] { m_editor->setTool(ZoneEditor::Tool::Select); });
    connect(m_polyBtn, &QToolButton::clicked, this, [this] { m_editor->setTool(ZoneEditor::Tool::Polygon); });
    connect(m_lineBtn, &QToolButton::clicked, this, [this] { m_editor->setTool(ZoneEditor::Tool::Line); });
    connect(m_editor, &ZoneEditor::toolChanged, this, [this](ZoneEditor::Tool t) {
        m_selectBtn->setChecked(t == ZoneEditor::Tool::Select);
        m_polyBtn->setChecked(t == ZoneEditor::Tool::Polygon);
        m_lineBtn->setChecked(t == ZoneEditor::Tool::Line);
    });
    connect(delBtn, &QPushButton::clicked, m_editor, &ZoneEditor::deleteSelected);
    connect(clearBtn, &QPushButton::clicked, this, [this] {
        if (QMessageBox::question(this, tr("Clear all"), tr("Remove all zones and lines from this camera?")) ==
            QMessageBox::Yes)
            m_editor->clearAll();
    });
    connect(frameBtn, &QPushButton::clicked, this, &ZonesPage::refreshFrame);
    connect(m_revertBtn, &QPushButton::clicked, this, [this] { loadCamera(m_cameraId); });
    connect(m_saveBtn, &QPushButton::clicked, this, &ZonesPage::save);
    connect(m_editor, &ZoneEditor::shapesChanged, this, [this] {
        setDirty(true);
        refreshShapeList();
        showProperties(m_editor->selectedKind(), m_editor->selectedIndex());
    });
    connect(m_editor, &ZoneEditor::selectionChanged, this, [this](int kind, int index) {
        showProperties(kind, index);
        const int row = kind == ZoneEditor::KindZone ? index
                        : kind == ZoneEditor::KindLine ? m_editor->zones().size() + index
                                                       : -1;
        m_shapeList->blockSignals(true);
        m_shapeList->setCurrentRow(row);
        m_shapeList->blockSignals(false);
    });
    connect(m_shapeList, &QListWidget::currentRowChanged, this, [this](int row) {
        const int nz = m_editor->zones().size();
        if (row < 0)
            m_editor->select(ZoneEditor::KindNone, -1);
        else if (row < nz)
            m_editor->select(ZoneEditor::KindZone, row);
        else
            m_editor->select(ZoneEditor::KindLine, row - nz);
    });

    connect(m_zName, &QLineEdit::textEdited, this, &ZonesPage::pushZoneForm);
    connect(m_zType, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ZonesPage::pushZoneForm);
    connect(m_zCapacity, QOverload<int>::of(&QSpinBox::valueChanged), this, &ZonesPage::pushZoneForm);
    connect(m_zArea, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &ZonesPage::pushZoneForm);
    connect(m_zLoiter, QOverload<int>::of(&QSpinBox::valueChanged), this, &ZonesPage::pushZoneForm);
    connect(m_zTotals, &QCheckBox::toggled, this, &ZonesPage::pushZoneForm);
    connect(m_zEnabled, &QCheckBox::toggled, this, &ZonesPage::pushZoneForm);
    connect(m_zColor, &QPushButton::clicked, this, &ZonesPage::pickColor);
    connect(m_lName, &QLineEdit::textEdited, this, &ZonesPage::pushLineForm);
    connect(m_lInvert, &QCheckBox::toggled, this, &ZonesPage::pushLineForm);
    connect(m_lOneWay, &QCheckBox::toggled, this, &ZonesPage::pushLineForm);
    connect(m_lTotals, &QCheckBox::toggled, this, &ZonesPage::pushLineForm);
    connect(m_lEnabled, &QCheckBox::toggled, this, &ZonesPage::pushLineForm);
    connect(m_lColor, &QPushButton::clicked, this, &ZonesPage::pickColor);
    connect(m_camCapacity, QOverload<int>::of(&QSpinBox::valueChanged), this, [this] {
        if (!m_loadingForm)
            setDirty(true);
    });

    connect(core.cameras, &CameraManager::camerasChanged, this, &ZonesPage::reloadCameraList);
    connect(core.cameras, &CameraManager::cameraStatusChanged, this, [this](int id, CameraStatus s) {
        for (int i = 0; i < m_cameraList->count(); ++i)
            if (m_cameraList->item(i)->data(Qt::UserRole).toInt() == id)
                m_cameraList->item(i)->setIcon(swatch(Theme::statusColor(s)));
    });

    m_frameRetry.setInterval(1000);
    connect(&m_frameRetry, &QTimer::timeout, this, [this] {
        if (isVisible() && !m_editor->hasFrame())
            refreshFrame();
    });
    m_frameRetry.start();

    reloadCameraList();
    setDirty(false);
}

void ZonesPage::showEvent(QShowEvent* e)
{
    QWidget::showEvent(e);
    refreshFrame();
}

void ZonesPage::reloadCameraList()
{
    m_cameraList->blockSignals(true);
    m_cameraList->clear();
    int currentRow = -1;
    for (const CameraConfig& c : m_core.cameras->cameras()) {
        auto* it = new QListWidgetItem(swatch(Theme::statusColor(m_core.cameras->status(c.id))),
                                       QStringLiteral("CAM %1  %2").arg(c.id, 2, 10, QLatin1Char('0')).arg(c.name));
        it->setData(Qt::UserRole, c.id);
        it->setToolTip(tr("%1 zones, %2 lines").arg(c.zones.size()).arg(c.lines.size()));
        m_cameraList->addItem(it);
        if (c.id == m_cameraId)
            currentRow = m_cameraList->count() - 1;
    }
    m_cameraList->blockSignals(false);
    if (currentRow >= 0) {
        m_cameraList->blockSignals(true);
        m_cameraList->setCurrentRow(currentRow);
        m_cameraList->blockSignals(false);
    } else if (m_cameraList->count() > 0) {
        m_cameraId = -1;
        m_dirty = false;
        m_cameraList->setCurrentRow(0);
    } else {
        m_cameraId = -1;
        m_editor->setShapes({}, {});
        m_editor->setFrame(QImage());
        m_title->setText(tr("No cameras yet. Add one on the Cameras page."));
    }
}

void ZonesPage::editCamera(int cameraId)
{
    for (int i = 0; i < m_cameraList->count(); ++i)
        if (m_cameraList->item(i)->data(Qt::UserRole).toInt() == cameraId)
            m_cameraList->setCurrentRow(i);
}

bool ZonesPage::confirmLeave()
{
    if (!m_dirty)
        return true;
    const auto r = QMessageBox::question(this, tr("Unsaved changes"),
                                         tr("Save the zone changes for this camera?"),
                                         QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (r == QMessageBox::Cancel)
        return false;
    if (r == QMessageBox::Save)
        save();
    else
        setDirty(false);
    return true;
}

void ZonesPage::loadCamera(int cameraId)
{
    const CameraConfig* cfg = m_core.cameras->camera(cameraId);
    m_cameraId = cfg ? cameraId : -1;
    if (!cfg) {
        m_editor->setShapes({}, {});
        return;
    }
    m_title->setText(QStringLiteral("CAM %1  %2  |  %3").arg(cfg->id, 2, 10, QLatin1Char('0')).arg(cfg->name, sourceTypeName(cfg->type)));
    m_editor->setShapes(cfg->zones, cfg->lines);
    m_loadingForm = true;
    m_camCapacity->setValue(cfg->capacity);
    m_loadingForm = false;
    refreshFrame();
    refreshShapeList();
    showProperties(ZoneEditor::KindNone, -1);
    setDirty(false);
}

void ZonesPage::refreshFrame()
{
    if (CameraWorker* w = m_core.cameras->worker(m_cameraId))
        m_editor->setFrame(w->latestImage());
}

void ZonesPage::refreshShapeList()
{
    m_shapeList->blockSignals(true);
    m_shapeList->clear();
    for (const Zone& z : m_editor->zones()) {
        auto* it = new QListWidgetItem(swatch(z.type == ZoneType::Restricted ? Theme::critical() : z.color),
                                       QStringLiteral("%1   (%2)").arg(z.name, zoneTypeName(z.type)));
        m_shapeList->addItem(it);
    }
    for (const CountLine& l : m_editor->lines()) {
        auto* it = new QListWidgetItem(swatch(l.color), QStringLiteral("%1   (%2)").arg(l.name, tr("Line")));
        m_shapeList->addItem(it);
    }
    const int kind = m_editor->selectedKind();
    const int idx = m_editor->selectedIndex();
    m_shapeList->setCurrentRow(kind == ZoneEditor::KindZone ? idx
                               : kind == ZoneEditor::KindLine ? m_editor->zones().size() + idx
                                                              : -1);
    m_shapeList->blockSignals(false);
}

void ZonesPage::showProperties(int kind, int index)
{
    m_loadingForm = true;
    if (kind == ZoneEditor::KindZone && index >= 0 && index < m_editor->zones().size()) {
        const Zone& z = m_editor->zones()[index];
        m_zName->setText(z.name);
        m_zType->setCurrentIndex(int(z.type));
        m_zCapacity->setValue(z.capacity);
        m_zArea->setValue(z.areaSqm);
        m_zLoiter->setValue(z.loiterSec);
        styleColorButton(m_zColor, z.color);
        m_zTotals->setChecked(z.includeInTotals);
        m_zEnabled->setChecked(z.enabled);
        m_zCapLabel->setText(z.type == ZoneType::Queue ? tr("Queue limit") : tr("Capacity"));
        m_zCapacity->setEnabled(z.type != ZoneType::Restricted);
        m_props->setCurrentIndex(1);
    } else if (kind == ZoneEditor::KindLine && index >= 0 && index < m_editor->lines().size()) {
        const CountLine& l = m_editor->lines()[index];
        m_lName->setText(l.name);
        m_lInvert->setChecked(l.invert);
        m_lOneWay->setChecked(l.alertOnOut);
        styleColorButton(m_lColor, l.color);
        m_lTotals->setChecked(l.includeInTotals);
        m_lEnabled->setChecked(l.enabled);
        m_props->setCurrentIndex(2);
    } else {
        m_props->setCurrentIndex(0);
    }
    m_loadingForm = false;
}

void ZonesPage::pushZoneForm()
{
    if (m_loadingForm || m_editor->selectedKind() != ZoneEditor::KindZone)
        return;
    const int idx = m_editor->selectedIndex();
    Zone z = m_editor->zones().value(idx);
    z.name = m_zName->text().trimmed().isEmpty() ? z.name : m_zName->text().trimmed();
    z.type = ZoneType(m_zType->currentIndex());
    z.capacity = m_zCapacity->value();
    z.areaSqm = m_zArea->value();
    z.loiterSec = m_zLoiter->value();
    z.color = m_zColor->property("color").value<QColor>();
    z.includeInTotals = m_zTotals->isChecked();
    z.enabled = m_zEnabled->isChecked();
    m_zCapLabel->setText(z.type == ZoneType::Queue ? tr("Queue limit") : tr("Capacity"));
    m_zCapacity->setEnabled(z.type != ZoneType::Restricted);
    m_editor->updateZone(idx, z);
}

void ZonesPage::pushLineForm()
{
    if (m_loadingForm || m_editor->selectedKind() != ZoneEditor::KindLine)
        return;
    const int idx = m_editor->selectedIndex();
    CountLine l = m_editor->lines().value(idx);
    l.name = m_lName->text().trimmed().isEmpty() ? l.name : m_lName->text().trimmed();
    l.invert = m_lInvert->isChecked();
    l.alertOnOut = m_lOneWay->isChecked();
    l.color = m_lColor->property("color").value<QColor>();
    l.includeInTotals = m_lTotals->isChecked();
    l.enabled = m_lEnabled->isChecked();
    m_editor->updateLine(idx, l);
}

void ZonesPage::pickColor()
{
    auto* btn = qobject_cast<QPushButton*>(sender());
    if (!btn)
        return;
    const QColor c = QColorDialog::getColor(btn->property("color").value<QColor>(), this, tr("Choose colour"));
    if (!c.isValid())
        return;
    styleColorButton(btn, c);
    if (btn == m_zColor)
        pushZoneForm();
    else
        pushLineForm();
}

void ZonesPage::setDirty(bool d)
{
    m_dirty = d;
    m_saveBtn->setEnabled(d && m_cameraId >= 0);
    m_revertBtn->setEnabled(d && m_cameraId >= 0);
    m_saveBtn->setText(d ? tr("Save *") : tr("Save"));
}

void ZonesPage::save()
{
    const CameraConfig* c = m_core.cameras->camera(m_cameraId);
    if (!c)
        return;
    CameraConfig cfg = *c;
    cfg.zones = m_editor->zones();
    cfg.lines = m_editor->lines();
    cfg.capacity = m_camCapacity->value();
    const int kind = m_editor->selectedKind();
    const int idx = m_editor->selectedIndex();
    setDirty(false);
    m_core.cameras->updateCamera(cfg);   // applies to the running analysis immediately
    m_editor->select(kind, idx);
    emit statusMessage(tr("Zones saved for %1 (%2 zones, %3 lines).").arg(cfg.name).arg(cfg.zones.size()).arg(cfg.lines.size()));
}
