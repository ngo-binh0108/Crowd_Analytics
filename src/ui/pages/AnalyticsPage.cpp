#include "AnalyticsPage.h"
#include "core/AnalyticsStore.h"
#include "core/CameraManager.h"
#include "core/CameraWorker.h"
#include "core/Session.h"
#include "core/Settings.h"
#include "ui/Theme.h"
#include "ui/widgets/Charts.h"
#include "ui/widgets/Common.h"

#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QPainter>
#include <QPdfWriter>
#include <QPushButton>
#include <QScrollArea>
#include <QStandardPaths>
#include <QTableWidget>
#include <QTextStream>
#include <QVBoxLayout>

namespace {

QString formatDuration(double sec)
{
    if (sec <= 0)
        return QStringLiteral("-");
    const int s = int(sec + 0.5);
    if (s < 60)
        return QStringLiteral("%1s").arg(s);
    if (s < 3600)
        return QStringLiteral("%1m %2s").arg(s / 60).arg(s % 60, 2, 10, QLatin1Char('0'));
    return QStringLiteral("%1h %2m").arg(s / 3600).arg((s % 3600) / 60, 2, 10, QLatin1Char('0'));
}

QIcon dotIcon(const QColor& c)
{
    QPixmap pm(12, 12);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    p.drawEllipse(QRectF(1.5, 1.5, 9, 9));
    return QIcon(pm);
}

QTableWidgetItem* item(const QString& text, Qt::Alignment align = Qt::AlignLeft | Qt::AlignVCenter)
{
    auto* it = new QTableWidgetItem(text);
    it->setTextAlignment(int(align));
    return it;
}

} // namespace

AnalyticsPage::AnalyticsPage(const AppCore& core, QWidget* parent) : QWidget(parent), m_core(core)
{
    setObjectName(QStringLiteral("Page"));
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto* scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("PageScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    outer->addWidget(scroll);

    m_content = new QWidget(scroll);
    m_content->setObjectName(QStringLiteral("Page"));
    auto* col = new QVBoxLayout(m_content);
    col->setContentsMargins(18, 14, 18, 18);
    col->setSpacing(12);

    // Header & filters (one row above the charts)
    auto* header = new QHBoxLayout;
    auto* titleCol = new QVBoxLayout;
    titleCol->setSpacing(0);
    titleCol->addWidget(makeLabel(tr("Analytics"), "pageTitle", m_content));
    titleCol->addWidget(makeLabel(tr("People flow, occupancy and crowd levels"), "secondary", m_content));
    header->addLayout(titleCol);
    header->addStretch(1);
    m_camera = new QComboBox(m_content);
    m_camera->setMinimumWidth(180);
    m_range = new QComboBox(m_content);
    m_range->addItems({ tr("Last hour"), tr("Today"), tr("Last 24 hours"), tr("Last 7 days") });
    m_range->setCurrentIndex(1);
    auto* refreshBtn = new QPushButton(Theme::icon(QStringLiteral("refresh")), tr("Refresh"), m_content);
    auto* resetBtn = new QPushButton(Theme::icon(QStringLiteral("refresh"), QColor("#ffb3b3")), tr("Reset Counters"), m_content);
    resetBtn->setProperty("variant", "danger");
    m_resetBtn = resetBtn;
    auto* csvBtn = new QPushButton(Theme::icon(QStringLiteral("download")), tr("Export CSV"), m_content);
    auto* pdfBtn = new QPushButton(Theme::icon(QStringLiteral("report"), Qt::white), tr("Export PDF"), m_content);
    pdfBtn->setProperty("variant", "primary");
    header->addWidget(makeLabel(tr("Camera"), "secondary", m_content));
    header->addWidget(m_camera);
    header->addSpacing(6);
    header->addWidget(makeLabel(tr("Range"), "secondary", m_content));
    header->addWidget(m_range);
    header->addWidget(refreshBtn);
    header->addSpacing(12);
    header->addWidget(resetBtn);
    header->addWidget(csvBtn);
    header->addWidget(pdfBtn);
    col->addLayout(header);

    // KPI row
    auto* kpis = new QGridLayout;
    kpis->setSpacing(12);
    m_kIn = new KpiCard(tr("Total In"), QStringLiteral("in"), Theme::seriesIn(), m_content);
    m_kOut = new KpiCard(tr("Total Out"), QStringLiteral("out"), Theme::seriesOut(), m_content);
    m_kNet = new KpiCard(tr("Net Flow"), QStringLiteral("flow"), Theme::accent2(), m_content);
    m_kOcc = new KpiCard(tr("Occupancy (In - Out)"), QStringLiteral("gauge"), Theme::good(), m_content);
    m_kPeople = new KpiCard(tr("People in View"), QStringLiteral("people"), Theme::seriesPeople(), m_content);
    m_kDwell = new KpiCard(tr("Avg. Dwell Time"), QStringLiteral("clock"), QColor("#d55181"), m_content);
    m_kOcc->setToolTip(tr("Entries minus exits since midnight (or the last reset), compared with the site capacity.\n"
                          "Accurate when every entrance has a counting line or zone."));
    m_kPeople->setToolTip(tr("People currently detected by all analysed cameras."));
    m_kDwell->setToolTip(tr("Average time people spent inside zones before leaving (today)."));
    const QList<KpiCard*> cards{ m_kIn, m_kOut, m_kNet, m_kOcc, m_kPeople, m_kDwell };
    for (int i = 0; i < cards.size(); ++i)
        kpis->addWidget(cards[i], 0, i);
    col->addLayout(kpis);

    // Row 2: flow + gauge
    auto* row2 = new QHBoxLayout;
    row2->setSpacing(12);
    auto* flowCard = new Card(tr("People Flow  (In / Out)"), QStringLiteral("analytics"), m_content);
    m_flow = new LineChart(flowCard);
    m_flow->setMinimumHeight(230);
    m_flow->setEmptyText(tr("No entries or exits in this period. Draw counting lines or zones and start analysis."));
    flowCard->body()->addWidget(m_flow);
    row2->addWidget(flowCard, 3);
    auto* gaugeCard = new Card(tr("Crowd Level"), QStringLiteral("gauge"), m_content);
    m_gauge = new GaugeWidget(gaugeCard);
    gaugeCard->body()->addWidget(m_gauge);
    row2->addWidget(gaugeCard, 1);
    col->addLayout(row2);

    // Row 3: people in view + hourly
    auto* row3 = new QHBoxLayout;
    row3->setSpacing(12);
    auto* peopleCard = new Card(tr("People in View  (average)"), QStringLiteral("people"), m_content);
    m_people = new LineChart(peopleCard);
    m_people->setMinimumHeight(200);
    m_people->setDecimals(1);
    m_people->setEmptyText(tr("No crowd measurements in this period"));
    peopleCard->body()->addWidget(m_people);
    row3->addWidget(peopleCard, 3);
    auto* hourCard = new Card(tr("Entries by Hour of Day"), QStringLiteral("calendar"), m_content);
    m_hourly = new BarChart(hourCard);
    m_hourly->setMinimumHeight(200);
    hourCard->body()->addWidget(m_hourly);
    row3->addWidget(hourCard, 2);
    col->addLayout(row3);

    // Row 4: table + heatmap
    auto* row4 = new QHBoxLayout;
    row4->setSpacing(12);
    auto* tableCard = new Card(tr("Zones & Counting Lines  (today)"), QStringLiteral("zones"), m_content);
    m_table = new QTableWidget(0, 10, tableCard);
    m_table->setHorizontalHeaderLabels({ tr("Zone / Line"), tr("Camera"), tr("Type"), tr("In"), tr("Out"), tr("Now"),
                                         tr("Capacity"), tr("Load"), tr("Level"), tr("Avg. Dwell") });
    m_table->verticalHeader()->hide();
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setAlternatingRowColors(true);
    m_table->setShowGrid(false);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->setMinimumHeight(260);
    tableCard->body()->addWidget(m_table);
    row4->addWidget(tableCard, 3);

    auto* heatCard = new Card(tr("Heatmap"), QStringLiteral("flame"), m_content);
    m_heatCamera = new QComboBox(heatCard);
    heatCard->addHeaderWidget(m_heatCamera);
    auto* heatReset = new QPushButton(tr("Reset"), heatCard);
    heatReset->setProperty("variant", "ghost");
    m_heatResetBtn = heatReset;
    heatCard->addHeaderWidget(heatReset);
    m_heat = new HeatmapView(heatCard);
    m_heat->setMinimumHeight(240);
    heatCard->body()->addWidget(m_heat);
    row4->addWidget(heatCard, 2);
    col->addLayout(row4);
    col->addStretch(1);

    scroll->setWidget(m_content);

    connect(m_camera, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { refresh(); });
    connect(m_range, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { refresh(); });
    connect(m_heatCamera, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { updateHeatmap(); });
    connect(refreshBtn, &QPushButton::clicked, this, &AnalyticsPage::refresh);
    connect(csvBtn, &QPushButton::clicked, this, &AnalyticsPage::exportCsv);
    connect(pdfBtn, &QPushButton::clicked, this, &AnalyticsPage::exportPdf);
    connect(heatReset, &QPushButton::clicked, this, [this] {
        if (CameraWorker* w = m_core.cameras->worker(m_heatCamera->currentData().toInt()))
            w->resetHeatmap();
        updateHeatmap();
    });
    connect(resetBtn, &QPushButton::clicked, this, [this] {
        m_core.analytics->resetCounters();
        m_core.cameras->resetCounters();
        refresh();
        emit statusMessage(tr("Counters reset."));
    });
    connect(core.cameras, &CameraManager::camerasChanged, this, &AnalyticsPage::refreshCameraLists);
    connect(core.analytics, &AnalyticsStore::updated, this, [this] {
        if (isVisible())
            updateLive();
    });

    m_timer.setInterval(10000);
    connect(&m_timer, &QTimer::timeout, this, [this] {
        if (isVisible())
            refresh();
    });
    m_timer.start();
    refreshCameraLists();
}

void AnalyticsPage::showEvent(QShowEvent* e)
{
    QWidget::showEvent(e);
    refresh();
}

void AnalyticsPage::refreshCameraLists()
{
    const int cur = m_camera->currentData().toInt();
    const int curHeat = m_heatCamera->currentData().toInt();
    m_camera->blockSignals(true);
    m_heatCamera->blockSignals(true);
    m_camera->clear();
    m_heatCamera->clear();
    m_camera->addItem(tr("All cameras"), -1);
    for (const CameraConfig& c : m_core.cameras->cameras()) {
        m_camera->addItem(c.name, c.id);
        m_heatCamera->addItem(c.name, c.id);
    }
    const int i = m_camera->findData(cur);
    m_camera->setCurrentIndex(i >= 0 ? i : 0);
    const int j = m_heatCamera->findData(curHeat);
    m_heatCamera->setCurrentIndex(j >= 0 ? j : 0);
    m_camera->blockSignals(false);
    m_heatCamera->blockSignals(false);
    if (isVisible())
        refresh();
}

int AnalyticsPage::cameraFilter() const
{
    return m_camera->currentData().toInt();
}

void AnalyticsPage::rangeFor(QDateTime& from, QDateTime& to, int& bucket, QString& fmt) const
{
    to = QDateTime::currentDateTime();
    switch (m_range->currentIndex()) {
    case 0:
        from = to.addSecs(-59 * 60);
        from.setTime(QTime(from.time().hour(), from.time().minute()));
        bucket = 1;
        fmt = QStringLiteral("HH:mm");
        break;
    case 1:
        from = QDateTime(QDate::currentDate(), QTime(0, 0));
        bucket = 15;
        fmt = QStringLiteral("HH:mm");
        break;
    case 2:
        from = to.addSecs(-23 * 3600);
        from.setTime(QTime(from.time().hour(), 0));
        bucket = 60;
        fmt = QStringLiteral("HH:00");
        break;
    default:
        from = QDateTime(QDate::currentDate().addDays(-6), QTime(0, 0));
        bucket = 24 * 60;
        fmt = QStringLiteral("ddd MM-dd");
        break;
    }
}

void AnalyticsPage::refresh()
{
    QDateTime from, to;
    int bucket = 1;
    QString fmt;
    rangeFor(from, to, bucket, fmt);
    const int cam = cameraFilter();

    m_series = m_core.analytics->series(from, to, bucket, cam);
    m_labels.clear();
    ChartSeries in{ tr("In"), Theme::seriesIn(), {}, true };
    ChartSeries out{ tr("Out"), Theme::seriesOut(), {}, true };
    ChartSeries people{ tr("People"), Theme::seriesPeople(), {}, true };
    qint64 sumIn = 0, sumOut = 0;
    int peak = 0;
    QDateTime peakAt;
    for (const MinuteRow& r : m_series) {
        m_labels << Database::timeOfMinute(r.minute).toString(fmt);
        in.values << double(r.in);
        out.values << double(r.out);
        people.values << r.people;
        sumIn += r.in;
        sumOut += r.out;
        if (r.peopleMax > peak) {
            peak = r.peopleMax;
            peakAt = Database::timeOfMinute(r.minute);
        }
    }
    m_flow->setData(m_labels, { in, out });
    m_people->setData(m_labels, { people });

    const QString rangeName = m_range->currentText();
    m_kIn->setValue(QString::number(sumIn));
    m_kIn->setCaption(rangeName);
    m_kOut->setValue(QString::number(sumOut));
    m_kOut->setCaption(rangeName);
    const qint64 net = sumIn - sumOut;
    m_kNet->setValue((net > 0 ? QStringLiteral("+") : QString()) + QString::number(net));
    m_kNet->setCaption(tr("In minus Out, %1").arg(rangeName.toLower()));
    m_kPeople->setCaption(peak > 0 ? tr("Peak %1 at %2").arg(peak).arg(peakAt.toString(fmt)) : tr("No peak recorded"));

    // Hourly distribution with the peak hour highlighted.
    const QVector<double> hours = m_core.analytics->hourlyIn(from, to, cam);
    QStringList hl;
    int peakHour = -1;
    double peakVal = 0;
    for (int h = 0; h < 24; ++h) {
        hl << QStringLiteral("%1").arg(h, 2, 10, QLatin1Char('0'));
        if (hours[h] > peakVal) {
            peakVal = hours[h];
            peakHour = h;
        }
    }
    m_hourly->setData(hl, hours, Theme::seriesIn());
    m_hourly->setHighlight(peakHour, Theme::accent2(), peakHour >= 0 ? tr("Peak %1:00").arg(peakHour, 2, 10, QLatin1Char('0')) : QString());

    updateLive();
    updateTable();
    updateHeatmap();
}

void AnalyticsPage::updateLive()
{
    const AppSettings& s = AppSettings::instance();
    const int cam = cameraFilter();
    AnalyticsStore* st = m_core.analytics;

    const int occ = st->occupancy();
    const int cap = s.siteCapacity;
    const DensityLevel occLevel = densityLevelFor(occ, cap, s.densityWarnPct, s.densityCritPct);
    m_kOcc->setValue(QString::number(occ));
    m_kOcc->setCaption(tr("of %1 capacity  |  %2").arg(cap).arg(densityLevelName(occLevel)));
    m_kOcc->setProgress(cap > 0 ? qRound(100.0 * occ / cap) : 0, Theme::levelColor(occLevel));

    const int people = st->peopleInView(cam);
    m_kPeople->setValue(QString::number(people));
    m_kDwell->setValue(formatDuration(st->avgDwellSec(cam)));
    m_kDwell->setCaption(tr("inside zones, today"));

    // Gauge: live people against site capacity (or the camera's own capacity).
    int gaugeCap = cap;
    QString caption = tr("All cameras vs. site capacity");
    if (cam >= 0) {
        gaugeCap = 0;
        if (const CameraConfig* c = m_core.cameras->camera(cam)) {
            gaugeCap = c->capacity;
            if (gaugeCap <= 0)
                for (const Zone& z : c->zones)
                    if (z.enabled && z.type != ZoneType::Restricted)
                        gaugeCap += z.capacity;
            caption = c->name;
        }
    }
    m_gauge->setValue(people, gaugeCap, densityLevelFor(people, gaugeCap, s.densityWarnPct, s.densityCritPct),
                      m_core.cameras->isAnalysisRunning() ? caption : tr("Analysis stopped"));
    updateTable();
}

void AnalyticsPage::updateTable()
{
    struct Row {
        QString cells[10];
        QColor nameDot;
        QColor levelDot;   // invalid = no icon
    };
    const int cam = cameraFilter();
    const bool running = m_core.cameras->isAnalysisRunning();
    QVector<Row> rows;
    for (const CameraConfig& c : m_core.cameras->cameras()) {
        if (cam >= 0 && c.id != cam)
            continue;
        const bool active = running && m_core.cameras->isCameraAnalyzing(c.id);
        for (const Zone& z : c.zones) {
            const TargetLive t = m_core.analytics->target(c.id, z.id);
            const bool hasCap = z.type != ZoneType::Restricted && z.capacity > 0;
            Row r;
            r.nameDot = z.color;
            r.cells[0] = z.name;
            r.cells[1] = c.name;
            r.cells[2] = zoneTypeName(z.type);
            r.cells[3] = QString::number(t.in);
            r.cells[4] = QString::number(t.out);
            r.cells[5] = active ? QString::number(t.current) : QStringLiteral("-");
            r.cells[6] = hasCap ? QString::number(z.capacity) : QStringLiteral("-");
            r.cells[7] = hasCap && active ? QStringLiteral("%1%").arg(qRound(100.0 * t.current / z.capacity)) : QStringLiteral("-");
            r.cells[8] = active && hasCap ? densityLevelName(t.level) : QStringLiteral("-");
            if (active && hasCap)
                r.levelDot = Theme::levelColor(t.level);
            r.cells[9] = formatDuration(t.avgDwell());
            rows.push_back(r);
        }
        for (const CountLine& l : c.lines) {
            const TargetLive t = m_core.analytics->target(c.id, l.id);
            Row r;
            r.nameDot = l.color;
            r.cells[0] = l.name;
            r.cells[1] = c.name;
            r.cells[2] = l.alertOnOut ? tr("One-way line") : tr("Counting line");
            r.cells[3] = QString::number(t.in);
            r.cells[4] = QString::number(t.out);
            for (int k = 5; k <= 9; ++k)
                r.cells[k] = QStringLiteral("-");
            rows.push_back(r);
        }
    }

    // Update in place so selection and scroll position survive live refreshes.
    m_table->setRowCount(rows.size());
    for (int r = 0; r < rows.size(); ++r) {
        for (int c = 0; c < 10; ++c) {
            QTableWidgetItem* it = m_table->item(r, c);
            if (!it) {
                it = item(QString(), c >= 3 && c != 8 ? Qt::AlignRight | Qt::AlignVCenter : Qt::AlignLeft | Qt::AlignVCenter);
                m_table->setItem(r, c, it);
            }
            if (it->text() != rows[r].cells[c])
                it->setText(rows[r].cells[c]);
        }
        m_table->item(r, 0)->setIcon(dotIcon(rows[r].nameDot));
        m_table->item(r, 8)->setIcon(rows[r].levelDot.isValid() ? dotIcon(rows[r].levelDot) : QIcon());
    }
}

void AnalyticsPage::updateHeatmap()
{
    const int id = m_heatCamera->currentData().toInt();
    CameraWorker* w = m_core.cameras->worker(id);
    if (!w) {
        m_heat->setImages(QImage(), QImage());
        return;
    }
    m_heat->setImages(w->latestImage(800), w->heatmapImage());
}

void AnalyticsPage::exportCsv()
{
    const QString def = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) +
                        QStringLiteral("/people_flow_%1.csv").arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmm"));
    const QString path = QFileDialog::getSaveFileName(this, tr("Export CSV"), def, tr("CSV files (*.csv)"));
    if (path.isEmpty())
        return;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        emit statusMessage(tr("Cannot write %1").arg(path));
        return;
    }
    QTextStream ts(&f);
    ts.setCodec("UTF-8");
    ts << "Section,Time,In,Out,People avg,People max\n";
    for (int i = 0; i < m_series.size(); ++i) {
        const MinuteRow& r = m_series[i];
        ts << "Flow," << Database::timeOfMinute(r.minute).toString(Qt::ISODate) << ',' << r.in << ',' << r.out << ','
           << QString::number(r.people, 'f', 2) << ',' << r.peopleMax << '\n';
    }
    ts << "\nZone / Line,Camera,Type,In,Out,Now,Capacity,Load,Level,Avg dwell\n";
    for (int row = 0; row < m_table->rowCount(); ++row) {
        QStringList cells;
        for (int c = 0; c < m_table->columnCount(); ++c) {
            QString v = m_table->item(row, c) ? m_table->item(row, c)->text() : QString();
            if (v.contains(',') || v.contains('"'))
                v = QLatin1Char('"') + v.replace(QLatin1String("\""), QLatin1String("\"\"")) + QLatin1Char('"');
            cells << v;
        }
        ts << cells.join(',') << '\n';
    }
    emit statusMessage(tr("Exported %1").arg(QDir::toNativeSeparators(path)));
}

void AnalyticsPage::exportPdf()
{
    const QString def = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) +
                        QStringLiteral("/analytics_report_%1.pdf").arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmm"));
    const QString path = QFileDialog::getSaveFileName(this, tr("Export PDF report"), def, tr("PDF files (*.pdf)"));
    if (path.isEmpty())
        return;
    const QPixmap shot = m_content->grab();
    QPdfWriter pdf(path);
    pdf.setPageSize(QPageSize(QPageSize::A4));
    pdf.setPageOrientation(QPageLayout::Landscape);
    pdf.setResolution(150);
    pdf.setTitle(tr("Surveillance System - Analytics Report"));
    pdf.setCreator(QStringLiteral("Surveillance System"));
    QPainter p(&pdf);
    if (!p.isActive()) {
        emit statusMessage(tr("Cannot write %1").arg(path));
        return;
    }
    const QRect page = p.viewport();
    p.fillRect(page, QColor(0x0a, 0x12, 0x22));
    QFont f = font();
    f.setPointSize(14);
    f.setBold(true);
    p.setFont(f);
    p.setPen(Qt::white);
    const AppSettings& s = AppSettings::instance();
    p.drawText(QRect(page.left() + 40, page.top() + 20, page.width() - 80, 50), Qt::AlignLeft | Qt::AlignVCenter,
               tr("%1  -  Analytics Report").arg(s.siteName));
    f.setPointSize(9);
    f.setBold(false);
    p.setFont(f);
    p.setPen(Theme::text2());
    p.drawText(QRect(page.left() + 40, page.top() + 20, page.width() - 80, 50), Qt::AlignRight | Qt::AlignVCenter,
               tr("%1  |  %2  |  generated %3")
                   .arg(m_camera->currentText(), m_range->currentText(),
                        QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm"))));
    const QRect area(page.left() + 40, page.top() + 80, page.width() - 80, page.height() - 110);
    const QSize fit = shot.size().scaled(area.size(), Qt::KeepAspectRatio);
    p.drawPixmap(QRect(area.left(), area.top(), fit.width(), fit.height()), shot);
    p.end();
    emit statusMessage(tr("Report saved: %1").arg(QDir::toNativeSeparators(path)));
}

void AnalyticsPage::applyPermissions()
{
    const bool can = Session::instance().can(Permission::ResetCounters);
    m_resetBtn->setVisible(can);
    m_heatResetBtn->setVisible(can);
}
