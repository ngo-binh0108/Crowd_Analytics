#include "EventsPage.h"
#include "core/CameraManager.h"
#include "core/Database.h"
#include "core/EventManager.h"
#include "core/Session.h"
#include "core/Settings.h"
#include "ui/Theme.h"
#include "ui/widgets/Common.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateEdit>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QStandardPaths>
#include <QTableWidget>
#include <QTextStream>
#include <QUrl>
#include <QVBoxLayout>

EventsPage::EventsPage(const AppCore& core, QWidget* parent) : QWidget(parent), m_core(core)
{
    setObjectName(QStringLiteral("Page"));
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(18, 14, 18, 14);
    root->setSpacing(12);

    auto* header = new QHBoxLayout;
    auto* titleCol = new QVBoxLayout;
    titleCol->setSpacing(0);
    titleCol->addWidget(makeLabel(tr("Events & Alerts"), "pageTitle", this));
    titleCol->addWidget(makeLabel(tr("Security alerts, camera health and operator actions"), "secondary", this));
    header->addLayout(titleCol);
    header->addStretch(1);
    auto* ackAll = new QPushButton(Theme::icon(QStringLiteral("check")), tr("Acknowledge All"), this);
    auto* csv = new QPushButton(Theme::icon(QStringLiteral("download")), tr("Export CSV"), this);
    auto* purge = new QPushButton(Theme::icon(QStringLiteral("trash"), QColor("#ffb3b3")), tr("Delete Old..."), this);
    purge->setProperty("variant", "danger");
    m_ackAllBtn = ackAll;
    m_purgeBtn = purge;
    header->addWidget(ackAll);
    header->addWidget(csv);
    header->addWidget(purge);
    root->addLayout(header);

    auto* counters = new QHBoxLayout;
    counters->setSpacing(10);
    m_cCritical = new StatTile(tr("Critical today"), QStringLiteral("alert"), Theme::critical(), this);
    m_cWarning = new StatTile(tr("Warnings today"), QStringLiteral("alert"), Theme::warning(), this);
    m_cInfo = new StatTile(tr("Info today"), QStringLiteral("info"), Theme::accent2(), this);
    m_cOpen = new StatTile(tr("Unacknowledged"), QStringLiteral("events"), Theme::serious(), this);
    for (StatTile* t : { m_cCritical, m_cWarning, m_cInfo, m_cOpen })
        counters->addWidget(t);
    root->addLayout(counters);

    // Filters
    auto* filters = new QHBoxLayout;
    filters->setSpacing(8);
    m_severity = new QComboBox(this);
    m_severity->addItem(tr("All severities"), -1);
    m_severity->addItem(Theme::icon(QStringLiteral("alert"), Theme::critical()), severityName(Severity::Critical), int(Severity::Critical));
    m_severity->addItem(Theme::icon(QStringLiteral("alert"), Theme::warning()), severityName(Severity::Warning), int(Severity::Warning));
    m_severity->addItem(Theme::icon(QStringLiteral("info"), Theme::accent2()), severityName(Severity::Info), int(Severity::Info));
    m_type = new QComboBox(this);
    m_type->addItem(tr("All types"), -1);
    for (int t = 0; t < int(EventType::Count_); ++t)
        m_type->addItem(eventTypeName(EventType(t)), t);
    m_camera = new QComboBox(this);
    m_from = new QDateEdit(QDate::currentDate().addDays(-7), this);
    m_to = new QDateEdit(QDate::currentDate(), this);
    for (QDateEdit* d : { m_from, m_to }) {
        d->setCalendarPopup(true);
        d->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
        d->setMinimumWidth(122);
    }
    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(tr("Search message, zone or camera"));
    m_search->addAction(Theme::icon(QStringLiteral("search")), QLineEdit::LeadingPosition);
    m_search->setClearButtonEnabled(true);
    m_unackOnly = new QCheckBox(tr("Unacknowledged only"), this);
    filters->addWidget(m_severity);
    filters->addWidget(m_type);
    filters->addWidget(m_camera);
    filters->addWidget(makeLabel(tr("From"), "secondary", this));
    filters->addWidget(m_from);
    filters->addWidget(makeLabel(tr("To"), "secondary", this));
    filters->addWidget(m_to);
    filters->addWidget(m_search, 1);
    filters->addWidget(m_unackOnly);
    root->addLayout(filters);

    // Table + details
    auto* body = new QHBoxLayout;
    body->setSpacing(12);
    auto* tableCard = new Card(QString(), QString(), this);
    m_table = new QTableWidget(0, 7, tableCard);
    m_table->setHorizontalHeaderLabels({ tr("Time"), tr("Severity"), tr("Type"), tr("Camera"), tr("Zone"), tr("Message"), tr("Status") });
    m_table->verticalHeader()->hide();
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setAlternatingRowColors(true);
    m_table->setShowGrid(false);
    m_table->setWordWrap(false);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Stretch);
    tableCard->body()->addWidget(m_table);
    body->addWidget(tableCard, 1);

    auto* detail = new Card(tr("Details"), QStringLiteral("info"), this);
    detail->setFixedWidth(380);
    m_preview = new QLabel(detail);
    m_preview->setMinimumHeight(210);
    m_preview->setAlignment(Qt::AlignCenter);
    m_preview->setStyleSheet(QStringLiteral("background:#050a14;border-radius:8px;color:#637593;"));
    m_preview->setText(tr("No snapshot"));
    detail->body()->addWidget(m_preview);
    m_details = new QLabel(detail);
    m_details->setWordWrap(true);
    m_details->setTextFormat(Qt::RichText);
    m_details->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_details->setTextInteractionFlags(Qt::TextSelectableByMouse);
    detail->body()->addWidget(m_details, 1);
    auto* btns = new QHBoxLayout;
    m_ackBtn = new QPushButton(Theme::icon(QStringLiteral("check"), Qt::white), tr("Acknowledge"), detail);
    m_ackBtn->setProperty("variant", "primary");
    m_openBtn = new QPushButton(Theme::icon(QStringLiteral("open")), tr("Open"), detail);
    m_folderBtn = new QPushButton(Theme::icon(QStringLiteral("folder")), tr("Folder"), detail);
    btns->addWidget(m_ackBtn);
    btns->addWidget(m_openBtn);
    btns->addWidget(m_folderBtn);
    detail->body()->addLayout(btns);
    body->addWidget(detail);
    root->addLayout(body, 1);

    m_debounce.setSingleShot(true);
    m_debounce.setInterval(250);
    connect(&m_debounce, &QTimer::timeout, this, &EventsPage::refresh);
    auto schedule = [this] { m_debounce.start(); };
    connect(m_severity, QOverload<int>::of(&QComboBox::currentIndexChanged), this, schedule);
    connect(m_type, QOverload<int>::of(&QComboBox::currentIndexChanged), this, schedule);
    connect(m_camera, QOverload<int>::of(&QComboBox::currentIndexChanged), this, schedule);
    connect(m_from, &QDateEdit::dateChanged, this, schedule);
    connect(m_to, &QDateEdit::dateChanged, this, schedule);
    connect(m_search, &QLineEdit::textChanged, this, schedule);
    connect(m_unackOnly, &QCheckBox::toggled, this, schedule);
    connect(m_table, &QTableWidget::currentCellChanged, this, [this](int row) { showDetails(row); });
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int row) {
        if (row >= 0 && row < m_rows.size() && !m_rows[row].snapshotPath.isEmpty())
            QDesktopServices::openUrl(QUrl::fromLocalFile(m_rows[row].snapshotPath));
    });
    connect(m_ackBtn, &QPushButton::clicked, this, [this] {
        const int row = m_table->currentRow();
        if (row >= 0 && row < m_rows.size())
            m_core.events->acknowledge(m_rows[row].id);
    });
    connect(m_openBtn, &QPushButton::clicked, this, [this] {
        const int row = m_table->currentRow();
        if (row >= 0 && row < m_rows.size() && !m_rows[row].snapshotPath.isEmpty())
            QDesktopServices::openUrl(QUrl::fromLocalFile(m_rows[row].snapshotPath));
    });
    connect(m_folderBtn, &QPushButton::clicked, this, [this] {
        const int row = m_table->currentRow();
        QString dir = AppSettings::instance().snapshotDir;
        if (row >= 0 && row < m_rows.size() && !m_rows[row].snapshotPath.isEmpty())
            dir = QFileInfo(m_rows[row].snapshotPath).absolutePath();
        QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
    });
    connect(ackAll, &QPushButton::clicked, this, [this] { m_core.events->acknowledgeAll(); });
    connect(csv, &QPushButton::clicked, this, &EventsPage::exportCsv);
    connect(purge, &QPushButton::clicked, this, [this] {
        const int days = AppSettings::instance().retentionDays;
        if (QMessageBox::question(this, tr("Delete old events"),
                                  tr("Delete events older than %1 days (retention setting)?").arg(days)) != QMessageBox::Yes)
            return;
        const int n = Database::instance().deleteEventsBefore(QDateTime::currentDateTime().addDays(-days));
        emit statusMessage(tr("%1 events deleted.").arg(n));
        refresh();
    });
    connect(core.events, &EventManager::eventsChanged, this, [this] {
        if (isVisible())
            m_debounce.start();
        else
            updateCounters();
    });
    connect(core.cameras, &CameraManager::camerasChanged, this, &EventsPage::refreshCameraFilter);

    refreshCameraFilter();
    showDetails(-1);
}

void EventsPage::showEvent(QShowEvent* e)
{
    QWidget::showEvent(e);
    if (m_to->date() < QDate::currentDate())
        m_to->setDate(QDate::currentDate());
    refresh();
}

void EventsPage::refreshCameraFilter()
{
    const int cur = m_camera->currentData().toInt();
    m_camera->blockSignals(true);
    m_camera->clear();
    m_camera->addItem(tr("All cameras"), -1);
    for (const CameraConfig& c : m_core.cameras->cameras())
        m_camera->addItem(c.name, c.id);
    const int i = m_camera->findData(cur);
    m_camera->setCurrentIndex(i >= 0 ? i : 0);
    m_camera->blockSignals(false);
}

void EventsPage::updateCounters()
{
    const EventCounts c = Database::instance().eventCounts(QDateTime(QDate::currentDate(), QTime(0, 0)));
    m_cCritical->setValue(QString::number(c.critical));
    m_cWarning->setValue(QString::number(c.warning));
    m_cInfo->setValue(QString::number(c.info));
    m_cOpen->setValue(QString::number(c.unacknowledged));
    m_cOpen->setCaption(c.unacknowledged > 0 ? tr("warnings & critical") : tr("all clear"),
                        c.unacknowledged > 0 ? Theme::warning() : Theme::good());
}

void EventsPage::refresh()
{
    updateCounters();
    EventFilter f;
    f.from = QDateTime(m_from->date(), QTime(0, 0));
    f.to = QDateTime(m_to->date().addDays(1), QTime(0, 0));
    f.severity = m_severity->currentData().toInt();
    f.type = m_type->currentData().toInt();
    f.cameraId = m_camera->currentData().toInt();
    f.text = m_search->text();
    f.unacknowledgedOnly = m_unackOnly->isChecked();

    const qint64 selectedId = (m_table->currentRow() >= 0 && m_table->currentRow() < m_rows.size())
                                  ? m_rows[m_table->currentRow()].id
                                  : -1;
    m_rows = Database::instance().queryEvents(f, 2000);
    m_table->setUpdatesEnabled(false);
    m_table->setRowCount(m_rows.size());
    int reselect = -1;
    for (int r = 0; r < m_rows.size(); ++r) {
        const EventRecord& e = m_rows[r];
        auto set = [&](int col, const QString& text) {
            auto* it = new QTableWidgetItem(text);
            m_table->setItem(r, col, it);
            return it;
        };
        set(0, e.time.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
        auto* sev = set(1, severityName(e.severity));
        sev->setIcon(Theme::icon(e.severity == Severity::Info ? QStringLiteral("info") : QStringLiteral("alert"),
                                 Theme::severityColor(e.severity)));
        set(2, eventTypeName(e.type));
        set(3, e.cameraName);
        set(4, e.zoneName);
        auto* msg = set(5, e.message);
        msg->setToolTip(e.message);
        auto* st = set(6, e.acknowledged ? tr("Acknowledged") : (e.severity == Severity::Info ? tr("-") : tr("Open")));
        if (!e.acknowledged && e.severity != Severity::Info)
            st->setForeground(Theme::warning());
        else
            st->setForeground(Theme::text3());
        if (e.id == selectedId)
            reselect = r;
    }
    m_table->setUpdatesEnabled(true);
    if (reselect >= 0)
        m_table->setCurrentCell(reselect, 0);
    else
        showDetails(-1);
}

void EventsPage::showDetails(int row)
{
    if (row < 0 || row >= m_rows.size()) {
        m_preview->setPixmap(QPixmap());
        m_preview->setText(tr("Select an event"));
        m_details->setText(QString());
        m_ackBtn->setEnabled(false);
        m_openBtn->setEnabled(false);
        return;
    }
    const EventRecord& e = m_rows[row];
    QPixmap pm;
    if (!e.snapshotPath.isEmpty() && pm.load(e.snapshotPath)) {
        m_preview->setPixmap(pm.scaled(QSize(350, 200), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    } else {
        m_preview->setPixmap(QPixmap());
        m_preview->setText(tr("No snapshot for this event"));
    }
    const QString sevColor = Theme::severityColor(e.severity).name();
    m_details->setText(QStringLiteral("<div style='line-height:150%'>"
                                      "<span style='font-size:15px;font-weight:700;color:#ffffff'>%1</span><br>"
                                      "<span style='color:%2;font-weight:600'>&#9679;</span> "
                                      "<span style='color:#e6edf7'>%3</span><br>"
                                      "<span style='color:#9db0cc'>%4</span><br><br>"
                                      "<span style='color:#637593'>Camera</span>&nbsp; %5<br>"
                                      "<span style='color:#637593'>Zone</span>&nbsp; %6<br>"
                                      "<span style='color:#637593'>Status</span>&nbsp; %7<br><br>"
                                      "<span style='color:#e6edf7'>%8</span></div>")
                           .arg(eventTypeName(e.type).toHtmlEscaped(), sevColor, severityName(e.severity),
                                e.time.toString(QStringLiteral("dddd, yyyy-MM-dd HH:mm:ss")),
                                e.cameraName.toHtmlEscaped(), e.zoneName.isEmpty() ? QStringLiteral("-") : e.zoneName.toHtmlEscaped(),
                                e.acknowledged ? tr("Acknowledged") : tr("Open"), e.message.toHtmlEscaped()));
    m_ackBtn->setEnabled(!e.acknowledged && Session::instance().can(Permission::AcknowledgeEvents));
    m_openBtn->setEnabled(!e.snapshotPath.isEmpty() && QFileInfo::exists(e.snapshotPath));
}

void EventsPage::exportCsv()
{
    const QString def = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) +
                        QStringLiteral("/events_%1.csv").arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmm"));
    const QString path = QFileDialog::getSaveFileName(this, tr("Export events"), def, tr("CSV files (*.csv)"));
    if (path.isEmpty())
        return;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        return;
    QTextStream ts(&f);
    ts.setCodec("UTF-8");
    auto q = [](QString s) {
        s.replace(QLatin1String("\""), QLatin1String("\"\""));
        return QLatin1Char('"') + s + QLatin1Char('"');
    };
    ts << "Time,Severity,Type,Camera,Zone,Message,Acknowledged,Snapshot\n";
    for (const EventRecord& e : m_rows)
        ts << e.time.toString(Qt::ISODate) << ',' << severityName(e.severity) << ',' << q(eventTypeName(e.type)) << ','
           << q(e.cameraName) << ',' << q(e.zoneName) << ',' << q(e.message) << ',' << (e.acknowledged ? "yes" : "no")
           << ',' << q(e.snapshotPath) << '\n';
    emit statusMessage(tr("Exported %1 events to %2").arg(m_rows.size()).arg(QDir::toNativeSeparators(path)));
}

void EventsPage::applyPermissions()
{
    const Session& ses = Session::instance();
    m_ackAllBtn->setEnabled(ses.can(Permission::AcknowledgeEvents));
    m_purgeBtn->setVisible(ses.can(Permission::DeleteEvents));
    showDetails(m_table->currentRow());
}
