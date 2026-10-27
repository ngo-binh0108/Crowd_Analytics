#include "SettingsPage.h"
#include "core/CameraManager.h"
#include "core/Detector.h"
#include "core/Settings.h"
#include "ui/Theme.h"
#include "ui/widgets/Common.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QVBoxLayout>

namespace {

QFormLayout* formIn(Card* card)
{
    auto* f = new QFormLayout;
    f->setSpacing(9);
    f->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    f->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    card->body()->addLayout(f);
    return f;
}

QWidget* withButton(QLineEdit* edit, QPushButton* button, QWidget* parent)
{
    auto* w = new QWidget(parent);
    auto* h = new QHBoxLayout(w);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(6);
    h->addWidget(edit, 1);
    h->addWidget(button);
    return w;
}

} // namespace

SettingsPage::SettingsPage(const AppCore& core, QWidget* parent) : QWidget(parent), m_core(core)
{
    setObjectName(QStringLiteral("Page"));
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto* scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("PageScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    outer->addWidget(scroll);
    auto* content = new QWidget(scroll);
    content->setObjectName(QStringLiteral("Page"));
    auto* root = new QVBoxLayout(content);
    root->setContentsMargins(18, 14, 18, 18);
    root->setSpacing(12);

    auto* header = new QHBoxLayout;
    auto* titleCol = new QVBoxLayout;
    titleCol->setSpacing(0);
    titleCol->addWidget(makeLabel(tr("Settings"), "pageTitle", content));
    titleCol->addWidget(makeLabel(tr("Site, detection model, alert rules, storage and display"), "secondary", content));
    header->addLayout(titleCol);
    header->addStretch(1);
    auto* revertBtn = new QPushButton(tr("Revert"), content);
    auto* saveBtn = new QPushButton(Theme::icon(QStringLiteral("save"), Qt::white), tr("Save Settings"), content);
    saveBtn->setProperty("variant", "primary");
    header->addWidget(revertBtn);
    header->addWidget(saveBtn);
    root->addLayout(header);

    auto* grid = new QGridLayout;
    grid->setSpacing(12);

    // General
    auto* general = new Card(tr("Site"), QStringLiteral("pin"), content);
    auto* gf = formIn(general);
    m_siteName = new QLineEdit(general);
    m_siteType = new QComboBox(general);
    m_siteType->addItems(AppSettings::siteTypes());
    m_siteCapacity = new QSpinBox(general);
    m_siteCapacity->setRange(1, 1000000);
    m_siteCapacity->setSuffix(tr(" people"));
    m_siteCapacity->setToolTip(tr("Maximum safe number of people for the whole site"));
    m_autoStart = new QCheckBox(tr("Start analysis automatically when the application starts"), general);
    m_tray = new QCheckBox(tr("Keep running in the system tray when the window is closed"), general);
    m_rtspTcp = new QCheckBox(tr("Use TCP for RTSP streams (more reliable; applies after restart)"), general);
    gf->addRow(tr("Site name"), m_siteName);
    gf->addRow(tr("Site type"), m_siteType);
    gf->addRow(tr("Site capacity"), m_siteCapacity);
    gf->addRow(m_autoStart);
    gf->addRow(m_tray);
    gf->addRow(m_rtspTcp);
    grid->addWidget(general, 0, 0);

    // Detection
    auto* det = new Card(tr("Detection Model"), QStringLiteral("cpu"), content);
    auto* df = formIn(det);
    m_model = new QComboBox(det);
    m_model->setEditable(true);
    m_model->setInsertPolicy(QComboBox::NoInsert);
    m_model->lineEdit()->setPlaceholderText(QStringLiteral("models/yolo26n.onnx"));
    m_model->setToolTip(tr("Pick a bundled model (models folder) or browse for any YOLO .onnx file"));
    auto* browseModel = new QPushButton(Theme::icon(QStringLiteral("folder")), tr("Browse"), det);
    m_format = new QComboBox(det);
    m_format->addItems({ tr("Auto-detect"), QStringLiteral("YOLOv5"), QStringLiteral("YOLOv8 / YOLO11"),
                         tr("End-to-end, NMS-free (YOLO26, YOLOv10)") });
    m_device = new QComboBox(det);
    m_device->addItems({ tr("CPU"), tr("NVIDIA GPU (CUDA 12)") });
    m_gpuId = new QSpinBox(det);
    m_gpuId->setRange(0, 15);
    m_gpuId->setPrefix(tr("GPU "));
    auto* devRow = new QWidget(det);
    auto* devH = new QHBoxLayout(devRow);
    devH->setContentsMargins(0, 0, 0, 0);
    devH->addWidget(m_device, 1);
    devH->addWidget(m_gpuId);
    m_inputSize = new QComboBox(det);
    for (int s : { 320, 416, 480, 512, 640, 736, 800, 960, 1024, 1280 })
        m_inputSize->addItem(QStringLiteral("%1 x %1").arg(s), s);
    m_inputSize->setToolTip(tr("Used only for models with a dynamic input size. Larger finds smaller/farther people but is slower."));
    m_conf = new QDoubleSpinBox(det);
    m_conf->setRange(0.05, 0.95);
    m_conf->setSingleStep(0.05);
    m_nms = new QDoubleSpinBox(det);
    m_nms->setRange(0.1, 0.9);
    m_nms->setSingleStep(0.05);
    m_fps = new QDoubleSpinBox(det);
    m_fps->setRange(0.5, 30);
    m_fps->setSingleStep(1);
    m_fps->setDecimals(1);
    m_fps->setSuffix(tr(" fps per camera"));
    m_fps->setToolTip(tr("How often each camera is analysed. Lower values save CPU/GPU with many cameras."));
    m_jobs = new QSpinBox(det);
    m_jobs->setRange(1, 16);
    m_jobs->setToolTip(tr("Number of cameras analysed in parallel"));
    m_threads = new QSpinBox(det);
    m_threads->setRange(1, 32);
    m_threads->setToolTip(tr("CPU threads used by one inference"));
    m_unload = new QCheckBox(tr("Release the model from memory when analysis stops"), det);
    m_bags = new QCheckBox(tr("Detect bags / suitcases (needed for abandoned-object alerts)"), det);
    m_modelStatus = makeLabel(QString(), "hint", det);
    m_modelStatus->setWordWrap(true);
    auto* modelRow = new QWidget(det);
    auto* modelH = new QHBoxLayout(modelRow);
    modelH->setContentsMargins(0, 0, 0, 0);
    modelH->setSpacing(6);
    modelH->addWidget(m_model, 1);
    modelH->addWidget(browseModel);
    df->addRow(tr("Model file"), modelRow);
    df->addRow(tr("Output format"), m_format);
    df->addRow(tr("Inference device"), devRow);
    df->addRow(tr("Input size"), m_inputSize);
    df->addRow(tr("Confidence"), m_conf);
    df->addRow(tr("NMS IoU"), m_nms);
    df->addRow(tr("Analysis rate"), m_fps);
    df->addRow(tr("Parallel cameras"), m_jobs);
    df->addRow(tr("Threads / inference"), m_threads);
    df->addRow(m_unload);
    df->addRow(m_bags);
    df->addRow(m_modelStatus);
    grid->addWidget(det, 0, 1, 2, 1);

    // Alerts
    auto* alerts = new Card(tr("Alert Rules"), QStringLiteral("alert"), content);
    auto* af = formIn(alerts);
    m_warnPct = new QSpinBox(alerts);
    m_warnPct->setRange(10, 500);
    m_warnPct->setSuffix(tr(" % of capacity"));
    m_critPct = new QSpinBox(alerts);
    m_critPct->setRange(10, 500);
    m_critPct->setSuffix(tr(" % of capacity"));
    m_abandon = new QSpinBox(alerts);
    m_abandon->setRange(5, 3600);
    m_abandon->setSuffix(tr(" s"));
    m_runSpeed = new QDoubleSpinBox(alerts);
    m_runSpeed->setRange(0.8, 6.0);
    m_runSpeed->setSingleStep(0.1);
    m_runSpeed->setSuffix(tr(" body heights / s"));
    m_cooldown = new QSpinBox(alerts);
    m_cooldown->setRange(5, 3600);
    m_cooldown->setSuffix(tr(" s"));
    m_sound = new QCheckBox(tr("Play a sound on warnings and critical alerts"), alerts);
    m_notify = new QCheckBox(tr("Show desktop notifications for critical alerts"), alerts);
    m_autoSnap = new QCheckBox(tr("Save an annotated snapshot with every alert"), alerts);
    m_autoRecord = new QSpinBox(alerts);
    m_autoRecord->setRange(0, 600);
    m_autoRecord->setSuffix(tr(" s"));
    m_autoRecord->setSpecialValueText(tr("Off"));
    af->addRow(tr("High density at"), m_warnPct);
    af->addRow(tr("Overcrowding at"), m_critPct);
    af->addRow(tr("Unattended object after"), m_abandon);
    af->addRow(tr("Running speed"), m_runSpeed);
    af->addRow(tr("Repeat alert after"), m_cooldown);
    af->addRow(tr("Record clip on critical"), m_autoRecord);
    af->addRow(m_sound);
    af->addRow(m_notify);
    af->addRow(m_autoSnap);
    grid->addWidget(alerts, 1, 0);

    // Storage
    auto* storage = new Card(tr("Storage"), QStringLiteral("folder"), content);
    auto* sf = formIn(storage);
    m_dataDir = new QLineEdit(storage);
    m_dataDir->setReadOnly(true);
    m_snapDir = new QLineEdit(storage);
    m_recDir = new QLineEdit(storage);
    auto* snapBrowse = new QPushButton(Theme::icon(QStringLiteral("folder")), QString(), storage);
    auto* recBrowse = new QPushButton(Theme::icon(QStringLiteral("folder")), QString(), storage);
    m_recFormat = new QComboBox(storage);
    m_recFormat->addItems({ tr("MP4 (MPEG-4)"), tr("AVI (Motion JPEG)") });
    m_retention = new QSpinBox(storage);
    m_retention->setRange(1, 3650);
    m_retention->setSuffix(tr(" days"));
    sf->addRow(tr("Data folder"), m_dataDir);
    sf->addRow(tr("Snapshots"), withButton(m_snapDir, snapBrowse, storage));
    sf->addRow(tr("Recordings"), withButton(m_recDir, recBrowse, storage));
    sf->addRow(tr("Recording format"), m_recFormat);
    sf->addRow(tr("Keep history for"), m_retention);
    grid->addWidget(storage, 2, 0);

    // Display
    auto* display = new Card(tr("Display"), QStringLiteral("live"), content);
    auto* vf = formIn(display);
    m_displayFps = new QSpinBox(display);
    m_displayFps->setRange(1, 60);
    m_displayFps->setSuffix(tr(" fps"));
    m_displayFps->setToolTip(tr("Upper limit for on-screen video refresh per camera"));
    m_showIds = new QCheckBox(tr("Show person IDs and alert labels"), display);
    m_showZones = new QCheckBox(tr("Show zones and counting lines on video"), display);
    m_showConf = new QCheckBox(tr("Show detection confidence"), display);
    vf->addRow(tr("Max display rate"), m_displayFps);
    vf->addRow(m_showIds);
    vf->addRow(m_showZones);
    vf->addRow(m_showConf);
    grid->addWidget(display, 2, 1);

    // About
    auto* about = new Card(tr("About"), QStringLiteral("info"), content);
    auto* aboutText = makeLabel(tr("<b>Surveillance System</b> %1<br>Crowd analytics and security monitoring for stations, "
                                   "airports, subways, cinemas and other public venues.<br><br>"
                                   "Qt %2  |  %3<br><br>"
                                   "<span style='color:#9db0cc'>Detection models are not bundled. Check the licence of the model "
                                   "you deploy (e.g. Ultralytics YOLO models are AGPL-3.0 unless you hold an enterprise licence).</span>")
                                    .arg(QStringLiteral(APP_VERSION), QString::fromLatin1(qVersion()), Detector::libraryVersions()),
                                nullptr, about);
    aboutText->setWordWrap(true);
    aboutText->setTextFormat(Qt::RichText);
    about->body()->addWidget(aboutText);
    grid->addWidget(about, 3, 0, 1, 2);

    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(1, 1);
    root->addLayout(grid);
    root->addStretch(1);
    scroll->setWidget(content);

    connect(saveBtn, &QPushButton::clicked, this, &SettingsPage::save);
    connect(revertBtn, &QPushButton::clicked, this, &SettingsPage::load);
    connect(browseModel, &QPushButton::clicked, this, [this] {
        const QString start = QFileInfo(AppSettings::instance().resolvedModelPath()).absolutePath();
        const QString path = QFileDialog::getOpenFileName(this, tr("Select ONNX model"), start, tr("ONNX models (*.onnx)"));
        if (path.isEmpty())
            return;
        // Store paths inside the application folder relative to it (portable installs).
        const QDir appDir(QCoreApplication::applicationDirPath());
        const QString rel = appDir.relativeFilePath(path);
        m_model->setCurrentText(rel.startsWith(QLatin1String("..")) ? QDir::toNativeSeparators(path) : rel);
    });
    connect(snapBrowse, &QPushButton::clicked, this, [this] { browseDir(m_snapDir); });
    connect(recBrowse, &QPushButton::clicked, this, [this] { browseDir(m_recDir); });
    connect(m_device, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int i) { m_gpuId->setEnabled(i == 1); });

    load();
}

void SettingsPage::showEvent(QShowEvent* e)
{
    QWidget::showEvent(e);
    const AppSettings& s = AppSettings::instance();
    const QString path = s.resolvedModelPath();
    QString status;
    if (m_core.cameras->isAnalysisRunning())
        status = tr("Active detector: %1").arg(m_core.cameras->detectorInfo());
    else if (QFileInfo::exists(path))
        status = tr("Will use %1. The model is loaded only while analysis runs.").arg(QDir::toNativeSeparators(path));
    else
        status = tr("Model file not found. Until one is provided the built-in OpenCV HOG people detector is used "
                    "(lower accuracy). Export one with:  yolo export model=yolo26n.pt format=onnx  "
                    "and place it in the models folder.");
    m_modelStatus->setText(status);
}

QString SettingsPage::browseDir(QLineEdit* target)
{
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Select folder"), target->text());
    if (!dir.isEmpty())
        target->setText(QDir::toNativeSeparators(dir));
    return dir;
}

void SettingsPage::load()
{
    const AppSettings& s = AppSettings::instance();
    m_siteName->setText(s.siteName);
    m_siteType->setCurrentIndex(qBound(0, s.siteType, m_siteType->count() - 1));
    m_siteCapacity->setValue(s.siteCapacity);
    m_autoStart->setChecked(s.autoStartAnalysis);
    m_tray->setChecked(s.minimizeToTray);
    m_rtspTcp->setChecked(s.rtspOverTcp);

    m_model->clear();
    for (const QString& m : AppSettings::bundledModels())
        m_model->addItem(QDir::toNativeSeparators(m));
    m_model->setCurrentText(QDir::toNativeSeparators(s.modelPath));
    m_format->setCurrentIndex(qBound(0, s.modelFormat, 3));
    m_device->setCurrentIndex(qBound(0, s.device, 1));
    m_gpuId->setValue(s.cudaDeviceId);
    m_gpuId->setEnabled(s.device == 1);
    const int idx = m_inputSize->findData(s.inputSize);
    m_inputSize->setCurrentIndex(idx >= 0 ? idx : m_inputSize->findData(640));
    m_conf->setValue(s.confThreshold);
    m_nms->setValue(s.nmsThreshold);
    m_fps->setValue(s.analysisFps);
    m_jobs->setValue(s.inferenceThreads);
    m_threads->setValue(s.intraOpThreads);
    m_unload->setChecked(s.unloadOnStop);
    m_bags->setChecked(s.detectBags);

    m_warnPct->setValue(s.densityWarnPct);
    m_critPct->setValue(s.densityCritPct);
    m_abandon->setValue(s.abandonSec);
    m_runSpeed->setValue(s.runSpeed);
    m_cooldown->setValue(s.alertCooldownSec);
    m_sound->setChecked(s.alertSound);
    m_notify->setChecked(s.trayNotify);
    m_autoSnap->setChecked(s.autoSnapshot);
    m_autoRecord->setValue(s.autoRecordSec);

    m_dataDir->setText(QDir::toNativeSeparators(s.dataDir));
    m_snapDir->setText(QDir::toNativeSeparators(s.snapshotDir));
    m_recDir->setText(QDir::toNativeSeparators(s.recordDir));
    m_recFormat->setCurrentIndex(qBound(0, s.recordFormat, 1));
    m_retention->setValue(s.retentionDays);

    m_displayFps->setValue(s.maxDisplayFps);
    m_showIds->setChecked(s.showIds);
    m_showZones->setChecked(s.showZones);
    m_showConf->setChecked(s.showConfidence);
}

void SettingsPage::save()
{
    if (m_critPct->value() <= m_warnPct->value()) {
        QMessageBox::warning(this, tr("Settings"), tr("The overcrowding threshold must be higher than the high-density threshold."));
        return;
    }
    AppSettings& s = AppSettings::instance();
    const bool modelChanged = QDir::fromNativeSeparators(m_model->currentText().trimmed()) != s.modelPath ||
                              m_device->currentIndex() != s.device || m_gpuId->value() != s.cudaDeviceId ||
                              m_inputSize->currentData().toInt() != s.inputSize || m_format->currentIndex() != s.modelFormat ||
                              m_threads->value() != s.intraOpThreads;

    s.siteName = m_siteName->text().trimmed();
    s.siteType = m_siteType->currentIndex();
    s.siteCapacity = m_siteCapacity->value();
    s.autoStartAnalysis = m_autoStart->isChecked();
    s.minimizeToTray = m_tray->isChecked();
    s.rtspOverTcp = m_rtspTcp->isChecked();

    s.modelPath = QDir::fromNativeSeparators(m_model->currentText().trimmed());
    s.modelFormat = m_format->currentIndex();
    s.device = m_device->currentIndex();
    s.cudaDeviceId = m_gpuId->value();
    s.inputSize = m_inputSize->currentData().toInt();
    s.confThreshold = float(m_conf->value());
    s.nmsThreshold = float(m_nms->value());
    s.analysisFps = m_fps->value();
    s.inferenceThreads = m_jobs->value();
    s.intraOpThreads = m_threads->value();
    s.unloadOnStop = m_unload->isChecked();
    s.detectBags = m_bags->isChecked();

    s.densityWarnPct = m_warnPct->value();
    s.densityCritPct = m_critPct->value();
    s.abandonSec = m_abandon->value();
    s.runSpeed = m_runSpeed->value();
    s.alertCooldownSec = m_cooldown->value();
    s.alertSound = m_sound->isChecked();
    s.trayNotify = m_notify->isChecked();
    s.autoSnapshot = m_autoSnap->isChecked();
    s.autoRecordSec = m_autoRecord->value();

    s.snapshotDir = QDir::fromNativeSeparators(m_snapDir->text().trimmed());
    s.recordDir = QDir::fromNativeSeparators(m_recDir->text().trimmed());
    s.recordFormat = m_recFormat->currentIndex();
    s.retentionDays = m_retention->value();

    s.maxDisplayFps = m_displayFps->value();
    s.showIds = m_showIds->isChecked();
    s.showZones = m_showZones->isChecked();
    s.showConfidence = m_showConf->isChecked();

    s.ensureDirectories();
    s.save();
    m_core.cameras->applySettings();
    emit settingsSaved();
    emit statusMessage(tr("Settings saved."));

    if (modelChanged && m_core.cameras->analysisState() != AnalysisState::Stopped) {
        if (QMessageBox::question(this, tr("Restart analysis"),
                                  tr("Detection model settings changed. Restart analysis now to apply them?")) ==
            QMessageBox::Yes) {
            m_core.cameras->stopAnalysis();
            m_core.cameras->startAnalysis();
        }
    }
}
