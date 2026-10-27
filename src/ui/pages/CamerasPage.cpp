#include "CamerasPage.h"
#include "CameraDialog.h"
#include "core/CameraManager.h"
#include "core/CameraWorker.h"
#include "ui/Theme.h"
#include "ui/widgets/Common.h"

#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {
enum Col { ColId, ColName, ColLocation, ColType, ColSource, ColResolution, ColFps, ColStatus, ColAi, ColCount };

QString typeIcon(SourceType t)
{
    switch (t) {
    case SourceType::Image:     return QStringLiteral("image");
    case SourceType::Video:     return QStringLiteral("video");
    case SourceType::UsbCamera: return QStringLiteral("usb");
    case SourceType::IpCamera:  return QStringLiteral("ipcam");
    }
    return QStringLiteral("camera");
}
} // namespace

CamerasPage::CamerasPage(const AppCore& core, QWidget* parent) : QWidget(parent), m_core(core)
{
    setObjectName(QStringLiteral("Page"));
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(18, 14, 18, 14);
    root->setSpacing(12);

    auto* header = new QHBoxLayout;
    auto* titleCol = new QVBoxLayout;
    titleCol->setSpacing(0);
    titleCol->addWidget(makeLabel(tr("Cameras"), "pageTitle", this));
    m_subtitle = makeLabel(QString(), "secondary", this);
    titleCol->addWidget(m_subtitle);
    header->addLayout(titleCol);
    header->addStretch(1);
    auto* addBtn = new QPushButton(Theme::icon(QStringLiteral("plus"), Qt::white), tr("Add Camera"), this);
    addBtn->setProperty("variant", "primary");
    auto* filesBtn = new QPushButton(Theme::icon(QStringLiteral("folder")), tr("Add Files..."), this);
    filesBtn->setToolTip(tr("Add several video or image files at once"));
    auto* editBtn = new QPushButton(Theme::icon(QStringLiteral("edit")), tr("Edit"), this);
    auto* reconnectBtn = new QPushButton(Theme::icon(QStringLiteral("refresh")), tr("Reconnect"), this);
    auto* upBtn = new QPushButton(tr("Move Up"), this);
    auto* downBtn = new QPushButton(tr("Move Down"), this);
    auto* removeBtn = new QPushButton(Theme::icon(QStringLiteral("trash"), QColor("#ffb3b3")), tr("Remove"), this);
    removeBtn->setProperty("variant", "danger");
    for (QPushButton* b : { addBtn, filesBtn, editBtn, reconnectBtn, upBtn, downBtn, removeBtn })
        header->addWidget(b);
    root->addLayout(header);

    auto* card = new Card(QString(), QString(), this);
    m_table = new QTableWidget(0, ColCount, card);
    m_table->setHorizontalHeaderLabels({ tr("CH"), tr("Name"), tr("Location"), tr("Type"), tr("Source"), tr("Resolution"),
                                         tr("FPS"), tr("Status"), tr("AI") });
    m_table->verticalHeader()->hide();
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setAlternatingRowColors(true);
    m_table->setShowGrid(false);
    m_table->setIconSize(QSize(16, 16));
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(ColSource, QHeaderView::Stretch);
    card->body()->addWidget(m_table);
    root->addWidget(card, 1);

    auto* hint = makeLabel(tr("Up to %1 channels. Tip: drag video or image files onto an empty tile in Live View to add them "
                              "instantly. Analysis only runs after pressing Start Analysis, so idle cameras use little memory.")
                               .arg(CameraManager::kMaxCameras),
                           "hint", this);
    hint->setWordWrap(true);
    root->addWidget(hint);

    connect(addBtn, &QPushButton::clicked, this, &CamerasPage::addCamera);
    connect(filesBtn, &QPushButton::clicked, this, [this] {
        const QStringList files = QFileDialog::getOpenFileNames(
            this, tr("Add video / image files"), QString(),
            tr("Media (*.mp4 *.avi *.mkv *.mov *.wmv *.m4v *.mpg *.mpeg *.ts *.webm *.jpg *.jpeg *.png *.bmp *.tif *.tiff *.webp)"));
        if (!files.isEmpty())
            addFileSources(files);
    });
    connect(editBtn, &QPushButton::clicked, this, [this] { editCamera(selectedId()); });
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this] { editCamera(selectedId()); });
    connect(reconnectBtn, &QPushButton::clicked, this, [this] {
        if (selectedId() >= 0)
            m_core.cameras->reconnect(selectedId());
    });
    connect(upBtn, &QPushButton::clicked, this, [this] {
        const int id = selectedId();
        m_core.cameras->moveCamera(id, -1);
        for (int r = 0; r < m_table->rowCount(); ++r)
            if (m_table->item(r, ColId)->data(Qt::UserRole).toInt() == id)
                m_table->selectRow(r);
    });
    connect(downBtn, &QPushButton::clicked, this, [this] {
        const int id = selectedId();
        m_core.cameras->moveCamera(id, 1);
        for (int r = 0; r < m_table->rowCount(); ++r)
            if (m_table->item(r, ColId)->data(Qt::UserRole).toInt() == id)
                m_table->selectRow(r);
    });
    connect(removeBtn, &QPushButton::clicked, this, [this] {
        const int id = selectedId();
        const CameraConfig* c = m_core.cameras->camera(id);
        if (!c)
            return;
        if (QMessageBox::question(this, tr("Remove camera"), tr("Remove \"%1\" and its zones?").arg(c->name)) == QMessageBox::Yes)
            m_core.cameras->removeCamera(id);
    });
    connect(core.cameras, &CameraManager::camerasChanged, this, &CamerasPage::rebuild);
    connect(core.cameras, &CameraManager::cameraStatusChanged, this, &CamerasPage::updateLiveColumns);
    connect(core.cameras, &CameraManager::analysisStateChanged, this, &CamerasPage::updateLiveColumns);

    m_timer.setInterval(1000);
    connect(&m_timer, &QTimer::timeout, this, [this] {
        if (isVisible())
            updateLiveColumns();
    });
    m_timer.start();
    rebuild();
}

int CamerasPage::selectedId() const
{
    const int row = m_table->currentRow();
    if (row < 0 || !m_table->item(row, ColId))
        return -1;
    return m_table->item(row, ColId)->data(Qt::UserRole).toInt();
}

void CamerasPage::addCamera()
{
    if (m_core.cameras->count() >= CameraManager::kMaxCameras) {
        QMessageBox::information(this, tr("Add camera"), tr("All %1 channels are in use.").arg(CameraManager::kMaxCameras));
        return;
    }
    CameraConfig cfg;
    cfg.type = SourceType::IpCamera;
    CameraDialog dlg(cfg, m_core.cameras->locations(), this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    const int id = m_core.cameras->addCamera(dlg.config());
    if (id > 0)
        emit statusMessage(tr("Camera %1 added.").arg(dlg.config().name));
}

void CamerasPage::editCamera(int cameraId)
{
    const CameraConfig* c = m_core.cameras->camera(cameraId);
    if (!c)
        return;
    CameraDialog dlg(*c, m_core.cameras->locations(), this);
    if (dlg.exec() == QDialog::Accepted)
        m_core.cameras->updateCamera(dlg.config());
}

int CamerasPage::addFileSources(const QStringList& paths)
{
    int added = 0;
    for (const QString& p : paths) {
        if (m_core.cameras->count() >= CameraManager::kMaxCameras) {
            emit statusMessage(tr("Channel limit reached (%1).").arg(CameraManager::kMaxCameras));
            break;
        }
        if (m_core.cameras->addCamera(CameraDialog::configForFile(p)) > 0)
            ++added;
    }
    if (added > 0)
        emit statusMessage(tr("%1 file source(s) added.").arg(added));
    return added;
}

void CamerasPage::rebuild()
{
    const int keep = selectedId();
    const auto& cams = m_core.cameras->cameras();
    m_subtitle->setText(tr("%1 of %2 channels in use").arg(cams.size()).arg(CameraManager::kMaxCameras));
    m_table->setRowCount(cams.size());
    for (int r = 0; r < cams.size(); ++r) {
        const CameraConfig& c = cams[r];
        auto* id = new QTableWidgetItem(QStringLiteral("%1").arg(c.id, 2, 10, QLatin1Char('0')));
        id->setData(Qt::UserRole, c.id);
        m_table->setItem(r, ColId, id);
        m_table->setItem(r, ColName, new QTableWidgetItem(c.name));
        m_table->setItem(r, ColLocation, new QTableWidgetItem(c.location));
        m_table->setItem(r, ColType, new QTableWidgetItem(Theme::icon(typeIcon(c.type)), sourceTypeName(c.type)));
        auto* src = new QTableWidgetItem(c.sourceDescription());
        src->setToolTip(c.sourceDescription());
        m_table->setItem(r, ColSource, src);
        for (int col : { ColResolution, ColFps, ColStatus, ColAi })
            m_table->setItem(r, col, new QTableWidgetItem);
        if (c.id == keep)
            m_table->selectRow(r);
    }
    updateLiveColumns();
}

void CamerasPage::updateLiveColumns()
{
    for (int r = 0; r < m_table->rowCount(); ++r) {
        QTableWidgetItem* idItem = m_table->item(r, ColId);
        if (!idItem)
            continue;
        const int id = idItem->data(Qt::UserRole).toInt();
        const CameraStatus st = m_core.cameras->status(id);
        CameraWorker* w = m_core.cameras->worker(id);
        const QSize sz = w ? w->frameSize() : QSize();
        m_table->item(r, ColResolution)->setText(sz.width() > 0 ? QStringLiteral("%1 x %2").arg(sz.width()).arg(sz.height()) : QStringLiteral("-"));
        m_table->item(r, ColFps)->setText(w && w->measuredFps() > 0 ? QString::number(w->measuredFps(), 'f', 1) : QStringLiteral("-"));
        QTableWidgetItem* s = m_table->item(r, ColStatus);
        const QString msg = m_core.cameras->statusMessage(id);
        s->setText(cameraStatusName(st) + (msg.isEmpty() ? QString() : QStringLiteral(" - ") + msg));
        s->setIcon(Theme::icon(QStringLiteral("record"), Theme::statusColor(st)));
        const CameraConfig* c = m_core.cameras->camera(id);
        QTableWidgetItem* ai = m_table->item(r, ColAi);
        if (m_core.cameras->isCameraAnalyzing(id))
            ai->setText(tr("Running"));
        else
            ai->setText(c && c->analysisEnabled ? tr("Ready") : tr("Excluded"));
        ai->setForeground(m_core.cameras->isCameraAnalyzing(id) ? Theme::good() : Theme::text3());
    }
}
