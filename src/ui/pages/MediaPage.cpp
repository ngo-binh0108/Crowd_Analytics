#include "MediaPage.h"
#include "core/CameraManager.h"
#include "core/Session.h"
#include "core/Settings.h"
#include "ui/Theme.h"
#include "ui/widgets/Common.h"

#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QImageReader>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QTabWidget>
#include <QTableWidget>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>

namespace {

QFileInfoList listFiles(const QString& dir, const QStringList& patterns, const QDateTime& since)
{
    QFileInfoList out;
    QDirIterator it(dir, patterns, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        const QFileInfo fi = it.fileInfo();
        if (!since.isValid() || fi.lastModified() >= since)
            out << fi;
    }
    std::sort(out.begin(), out.end(), [](const QFileInfo& a, const QFileInfo& b) { return a.lastModified() > b.lastModified(); });
    return out;
}

QString humanSize(qint64 bytes)
{
    if (bytes >= 1024LL * 1024 * 1024)
        return QStringLiteral("%1 GB").arg(bytes / (1024.0 * 1024 * 1024), 0, 'f', 2);
    if (bytes >= 1024 * 1024)
        return QStringLiteral("%1 MB").arg(bytes / (1024.0 * 1024), 0, 'f', 1);
    return QStringLiteral("%1 KB").arg(qMax<qint64>(1, bytes / 1024));
}

} // namespace

MediaPage::MediaPage(const AppCore& core, QWidget* parent) : QWidget(parent), m_core(core)
{
    setObjectName(QStringLiteral("Page"));
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(18, 14, 18, 14);
    root->setSpacing(12);

    auto* header = new QHBoxLayout;
    auto* titleCol = new QVBoxLayout;
    titleCol->setSpacing(0);
    titleCol->addWidget(makeLabel(tr("Snapshots & Recordings"), "pageTitle", this));
    titleCol->addWidget(makeLabel(tr("Evidence captured manually and automatically on alerts"), "secondary", this));
    header->addLayout(titleCol);
    header->addStretch(1);
    m_period = new QComboBox(this);
    m_period->addItems({ tr("Today"), tr("Last 7 days"), tr("Last 30 days"), tr("All") });
    m_period->setCurrentIndex(1);
    auto* refreshBtn = new QPushButton(Theme::icon(QStringLiteral("refresh")), tr("Refresh"), this);
    auto* folderBtn = new QPushButton(Theme::icon(QStringLiteral("folder")), tr("Open Folder"), this);
    header->addWidget(m_period);
    header->addWidget(refreshBtn);
    header->addWidget(folderBtn);
    root->addLayout(header);

    m_tabs = new QTabWidget(this);

    // Snapshots
    auto* snapPage = new QWidget(m_tabs);
    auto* sl = new QVBoxLayout(snapPage);
    sl->setContentsMargins(10, 10, 10, 10);
    m_snapshots = new QListWidget(snapPage);
    m_snapshots->setViewMode(QListView::IconMode);
    m_snapshots->setIconSize(QSize(240, 135));
    m_snapshots->setGridSize(QSize(256, 176));
    m_snapshots->setResizeMode(QListView::Adjust);
    m_snapshots->setMovement(QListView::Static);
    m_snapshots->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_snapshots->setContextMenuPolicy(Qt::CustomContextMenu);
    m_snapshots->setStyleSheet(QStringLiteral("QListWidget::item{color:#9db0cc;}"));
    sl->addWidget(m_snapshots);
    m_tabs->addTab(snapPage, Theme::icon(QStringLiteral("snapshot")), tr("Snapshots"));

    // Recordings
    auto* recPage = new QWidget(m_tabs);
    auto* rl = new QVBoxLayout(recPage);
    rl->setContentsMargins(10, 10, 10, 10);
    auto* recButtons = new QHBoxLayout;
    auto* playBtn = new QPushButton(Theme::icon(QStringLiteral("play"), Qt::white), tr("Play"), recPage);
    playBtn->setProperty("variant", "primary");
    auto* sourceBtn = new QPushButton(Theme::icon(QStringLiteral("plus")), tr("Analyse as Camera Source"), recPage);
    sourceBtn->setToolTip(tr("Adds the recording as a video source so it can be reviewed with analytics"));
    auto* delBtn = new QPushButton(Theme::icon(QStringLiteral("trash"), QColor("#ffb3b3")), tr("Delete"), recPage);
    delBtn->setProperty("variant", "danger");
    m_delBtn = delBtn;
    m_sourceBtn = sourceBtn;
    recButtons->addWidget(playBtn);
    recButtons->addWidget(sourceBtn);
    recButtons->addStretch(1);
    recButtons->addWidget(delBtn);
    rl->addLayout(recButtons);
    m_recordings = new QTableWidget(0, 4, recPage);
    m_recordings->setHorizontalHeaderLabels({ tr("File"), tr("Folder"), tr("Recorded"), tr("Size") });
    m_recordings->verticalHeader()->hide();
    m_recordings->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_recordings->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_recordings->setSelectionMode(QAbstractItemView::SingleSelection);
    m_recordings->setAlternatingRowColors(true);
    m_recordings->setShowGrid(false);
    m_recordings->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_recordings->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    rl->addWidget(m_recordings);
    m_tabs->addTab(recPage, Theme::icon(QStringLiteral("video")), tr("Recordings"));
    root->addWidget(m_tabs, 1);

    connect(m_period, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MediaPage::refresh);
    connect(refreshBtn, &QPushButton::clicked, this, &MediaPage::refresh);
    connect(folderBtn, &QPushButton::clicked, this, [this] {
        const AppSettings& s = AppSettings::instance();
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_tabs->currentIndex() == 0 ? s.snapshotDir : s.recordDir));
    });
    connect(m_snapshots, &QListWidget::itemDoubleClicked, this, [](QListWidgetItem* it) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(it->data(Qt::UserRole).toString()));
    });
    connect(m_snapshots, &QListWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        QListWidgetItem* it = m_snapshots->itemAt(pos);
        if (!it)
            return;
        QMenu menu(this);
        QAction* open = menu.addAction(Theme::icon(QStringLiteral("open")), tr("Open"));
        QAction* folder = menu.addAction(Theme::icon(QStringLiteral("folder")), tr("Show in folder"));
        menu.addSeparator();
        QAction* del = menu.addAction(Theme::icon(QStringLiteral("trash"), Theme::critical()), tr("Delete selected"));
        del->setVisible(Session::instance().can(Permission::DeleteMedia));
        QAction* chosen = menu.exec(m_snapshots->viewport()->mapToGlobal(pos));
        const QString path = it->data(Qt::UserRole).toString();
        if (chosen == open) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(path));
        } else if (chosen == folder) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
        } else if (chosen == del) {
            const QList<QListWidgetItem*> sel = m_snapshots->selectedItems();
            if (QMessageBox::question(this, tr("Delete snapshots"), tr("Delete %1 snapshot(s)?").arg(sel.size())) != QMessageBox::Yes)
                return;
            for (QListWidgetItem* s : sel) {
                QFile::remove(s->data(Qt::UserRole).toString());
                delete s;
            }
        }
    });
    connect(playBtn, &QPushButton::clicked, this, [this] {
        const QString p = selectedRecording();
        if (!p.isEmpty())
            QDesktopServices::openUrl(QUrl::fromLocalFile(p));
    });
    connect(m_recordings, &QTableWidget::cellDoubleClicked, this, [this] {
        const QString p = selectedRecording();
        if (!p.isEmpty())
            QDesktopServices::openUrl(QUrl::fromLocalFile(p));
    });
    connect(sourceBtn, &QPushButton::clicked, this, [this] {
        const QString p = selectedRecording();
        if (!p.isEmpty())
            emit addVideoSourceRequested(p);
    });
    connect(delBtn, &QPushButton::clicked, this, [this] {
        const QString p = selectedRecording();
        if (p.isEmpty())
            return;
        if (QMessageBox::question(this, tr("Delete recording"), tr("Delete %1?").arg(QFileInfo(p).fileName())) != QMessageBox::Yes)
            return;
        if (!QFile::remove(p))
            emit statusMessage(tr("Could not delete the file (it may still be recording)."));
        loadRecordings();
    });
    connect(core.cameras, &CameraManager::recordingChanged, this, [this](int, bool recording) {
        if (!recording && isVisible())
            loadRecordings();
    });
}

void MediaPage::showEvent(QShowEvent* e)
{
    QWidget::showEvent(e);
    refresh();
}

void MediaPage::refresh()
{
    loadSnapshots();
    loadRecordings();
}

static QDateTime sinceFor(int periodIndex)
{
    switch (periodIndex) {
    case 0: return QDateTime(QDate::currentDate(), QTime(0, 0));
    case 1: return QDateTime(QDate::currentDate().addDays(-6), QTime(0, 0));
    case 2: return QDateTime(QDate::currentDate().addDays(-29), QTime(0, 0));
    default: return QDateTime();
    }
}

void MediaPage::loadSnapshots()
{
    m_snapshots->clear();
    const QFileInfoList files = listFiles(AppSettings::instance().snapshotDir, { QStringLiteral("*.jpg"), QStringLiteral("*.png") },
                                          sinceFor(m_period->currentIndex()));
    const int limit = qMin(files.size(), 400);
    for (int i = 0; i < limit; ++i) {
        const QFileInfo& fi = files[i];
        QImageReader reader(fi.absoluteFilePath());
        const QSize sz = reader.size();
        if (sz.isValid())
            reader.setScaledSize(sz.scaled(QSize(240, 135), Qt::KeepAspectRatio));
        const QImage img = reader.read();
        if (img.isNull())
            continue;
        const bool alert = fi.completeBaseName().endsWith(QLatin1String("_alert"));
        auto* it = new QListWidgetItem(QIcon(QPixmap::fromImage(img)),
                                       fi.lastModified().toString(QStringLiteral("MM-dd HH:mm:ss")) +
                                           (alert ? tr("  |  alert") : tr("  |  manual")));
        it->setData(Qt::UserRole, fi.absoluteFilePath());
        it->setToolTip(QDir::toNativeSeparators(fi.absoluteFilePath()));
        m_snapshots->addItem(it);
    }
    m_tabs->setTabText(0, tr("Snapshots (%1)").arg(m_snapshots->count()));
}

void MediaPage::loadRecordings()
{
    const QFileInfoList files = listFiles(AppSettings::instance().recordDir, { QStringLiteral("*.mp4"), QStringLiteral("*.avi") },
                                          sinceFor(m_period->currentIndex()));
    m_recordings->setRowCount(files.size());
    for (int r = 0; r < files.size(); ++r) {
        const QFileInfo& fi = files[r];
        auto* name = new QTableWidgetItem(Theme::icon(QStringLiteral("video")), fi.fileName());
        name->setData(Qt::UserRole, fi.absoluteFilePath());
        m_recordings->setItem(r, 0, name);
        m_recordings->setItem(r, 1, new QTableWidgetItem(QDir::toNativeSeparators(fi.absolutePath())));
        m_recordings->setItem(r, 2, new QTableWidgetItem(fi.lastModified().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))));
        auto* size = new QTableWidgetItem(humanSize(fi.size()));
        size->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_recordings->setItem(r, 3, size);
    }
    m_tabs->setTabText(1, tr("Recordings (%1)").arg(files.size()));
}

QString MediaPage::selectedRecording() const
{
    const int row = m_recordings->currentRow();
    if (row < 0 || !m_recordings->item(row, 0))
        return QString();
    return m_recordings->item(row, 0)->data(Qt::UserRole).toString();
}

void MediaPage::applyPermissions()
{
    const Session& ses = Session::instance();
    m_delBtn->setVisible(ses.can(Permission::DeleteMedia));
    m_sourceBtn->setVisible(ses.can(Permission::ManageCameras));
}
