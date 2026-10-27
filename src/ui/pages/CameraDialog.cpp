#include "CameraDialog.h"
#include "core/CameraWorker.h"
#include "ui/Theme.h"
#include "ui/widgets/Common.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <opencv2/videoio.hpp>

static const char* kVideoFilter = "Video files (*.mp4 *.avi *.mkv *.mov *.wmv *.m4v *.mpg *.mpeg *.ts *.webm *.flv)";
static const char* kImageFilter = "Image files (*.jpg *.jpeg *.png *.bmp *.tif *.tiff *.webp)";

CameraConfig CameraDialog::configForFile(const QString& path)
{
    static const QStringList imageExt = { "jpg", "jpeg", "png", "bmp", "tif", "tiff", "webp" };
    const QFileInfo fi(path);
    CameraConfig c;
    c.type = imageExt.contains(fi.suffix().toLower()) ? SourceType::Image : SourceType::Video;
    c.source = fi.absoluteFilePath();
    c.name = fi.completeBaseName().left(40);
    c.location = QObject::tr("Files");
    c.loop = true;
    return c;
}

CameraDialog::CameraDialog(const CameraConfig& cfg, const QStringList& locations, QWidget* parent)
    : QDialog(parent), m_cfg(cfg)
{
    setWindowTitle(cfg.id > 0 ? tr("Camera Properties") : tr("Add Camera"));
    setMinimumWidth(760);
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(18, 16, 18, 16);
    root->setSpacing(14);

    auto* cols = new QHBoxLayout;
    cols->setSpacing(18);
    auto* left = new QVBoxLayout;
    left->setSpacing(12);

    // Source type selector
    left->addWidget(makeLabel(tr("Source type"), "secondary", this));
    auto* types = new QHBoxLayout;
    types->setSpacing(8);
    m_typeGroup = new QButtonGroup(this);
    const struct { SourceType t; const char* icon; QString text; } defs[] = {
        { SourceType::IpCamera, "ipcam", tr("IP Camera") },
        { SourceType::UsbCamera, "usb", tr("USB Camera") },
        { SourceType::Video, "video", tr("Video File") },
        { SourceType::Image, "image", tr("Image") },
    };
    for (const auto& d : defs) {
        auto* b = new QToolButton(this);
        b->setIcon(Theme::icon(QString::fromLatin1(d.icon), Theme::text2(), Qt::white));
        b->setIconSize(QSize(22, 22));
        b->setText(d.text);
        b->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        b->setProperty("tile", true);
        b->setCheckable(true);
        b->setMinimumSize(96, 62);
        b->setCursor(Qt::PointingHandCursor);
        m_typeGroup->addButton(b, int(d.t));
        types->addWidget(b);
    }
    types->addStretch(1);
    left->addLayout(types);

    auto* form = new QFormLayout;
    form->setSpacing(10);
    m_name = new QLineEdit(cfg.name, this);
    m_name->setPlaceholderText(tr("e.g. Platform 3 North"));
    m_location = new QComboBox(this);
    m_location->setEditable(true);
    QStringList locs = locations;
    for (const QString& preset : { tr("Main Entrance"), tr("Ticket Hall"), tr("Platform 1"), tr("Cinema Lobby"),
                                   tr("Security Checkpoint"), tr("Departure Hall"), tr("Parking Lot") })
        if (!locs.contains(preset))
            locs << preset;
    m_location->addItems(locs);
    m_location->setCurrentText(cfg.location);
    m_location->lineEdit()->setPlaceholderText(tr("Group cameras by area"));
    form->addRow(tr("Name"), m_name);
    form->addRow(tr("Location"), m_location);
    left->addLayout(form);

    // Source pages
    m_stack = new QStackedWidget(this);
    // 0: file (image / video)
    auto* filePage = new QWidget(m_stack);
    auto* fl = new QFormLayout(filePage);
    fl->setContentsMargins(0, 0, 0, 0);
    auto* pathRow = new QHBoxLayout;
    m_path = new QLineEdit(filePage);
    auto* browseBtn = new QPushButton(Theme::icon(QStringLiteral("folder")), tr("Browse..."), filePage);
    pathRow->addWidget(m_path, 1);
    pathRow->addWidget(browseBtn);
    fl->addRow(tr("File"), pathRow);
    m_loop = new QCheckBox(tr("Loop video when it ends"), filePage);
    fl->addRow(QString(), m_loop);
    m_stack->addWidget(filePage);
    // 1: USB
    auto* usbPage = new QWidget(m_stack);
    auto* ul = new QFormLayout(usbPage);
    ul->setContentsMargins(0, 0, 0, 0);
    auto* usbRow = new QHBoxLayout;
    m_usbIndex = new QComboBox(usbPage);
    for (int i = 0; i < 10; ++i)
        m_usbIndex->addItem(tr("Device #%1").arg(i), i);
    m_detectBtn = new QPushButton(Theme::icon(QStringLiteral("search")), tr("Detect"), usbPage);
    usbRow->addWidget(m_usbIndex, 1);
    usbRow->addWidget(m_detectBtn);
    ul->addRow(tr("Device"), usbRow);
    m_usbRes = new QComboBox(usbPage);
    m_usbRes->addItem(tr("Camera default"), QSize());
    m_usbRes->addItem(QStringLiteral("640 x 480"), QSize(640, 480));
    m_usbRes->addItem(QStringLiteral("1280 x 720"), QSize(1280, 720));
    m_usbRes->addItem(QStringLiteral("1920 x 1080"), QSize(1920, 1080));
    ul->addRow(tr("Resolution"), m_usbRes);
    m_stack->addWidget(usbPage);
    // 2: IP camera
    auto* ipPage = new QWidget(m_stack);
    auto* il = new QFormLayout(ipPage);
    il->setContentsMargins(0, 0, 0, 0);
    m_url = new QLineEdit(ipPage);
    m_url->setPlaceholderText(QStringLiteral("rtsp://192.168.1.64:554/Streaming/Channels/101"));
    m_user = new QLineEdit(ipPage);
    m_user->setPlaceholderText(tr("optional"));
    m_pass = new QLineEdit(ipPage);
    m_pass->setEchoMode(QLineEdit::Password);
    m_pass->setPlaceholderText(tr("optional"));
    il->addRow(tr("Stream URL"), m_url);
    il->addRow(tr("Username"), m_user);
    il->addRow(tr("Password"), m_pass);
    auto* ipHint = makeLabel(tr("Supports RTSP, HTTP/MJPEG and RTMP streams (ONVIF cameras expose an RTSP URL). "
                                "Use the camera's sub-stream (e.g. 640x360) when running many channels."),
                             "muted", ipPage);
    ipHint->setWordWrap(true);
    il->addRow(QString(), ipHint);
    m_stack->addWidget(ipPage);
    left->addWidget(m_stack);

    auto* opts = new QFormLayout;
    m_analysis = new QCheckBox(tr("Include this camera in AI analysis"), this);
    m_analysis->setChecked(cfg.analysisEnabled);
    m_capacity = new QSpinBox(this);
    m_capacity->setRange(0, 100000);
    m_capacity->setSpecialValueText(tr("Off"));
    m_capacity->setSuffix(tr(" people"));
    m_capacity->setValue(cfg.capacity);
    m_capacity->setToolTip(tr("Crowd alert threshold for the whole camera view"));
    opts->addRow(QString(), m_analysis);
    opts->addRow(tr("View capacity"), m_capacity);
    left->addLayout(opts);
    left->addStretch(1);
    cols->addLayout(left, 1);

    // Preview column
    auto* right = new QVBoxLayout;
    right->setSpacing(8);
    right->addWidget(makeLabel(tr("Preview"), "secondary", this));
    m_preview = new QLabel(this);
    m_preview->setFixedSize(320, 180);
    m_preview->setAlignment(Qt::AlignCenter);
    m_preview->setStyleSheet(QStringLiteral("background:#050a14;border:1px solid #1d2c47;border-radius:8px;color:#637593;"));
    m_preview->setText(tr("Press Test Connection"));
    right->addWidget(m_preview);
    m_testInfo = makeLabel(QString(), "secondary", this);
    m_testInfo->setWordWrap(true);
    m_testInfo->setMaximumWidth(320);
    right->addWidget(m_testInfo);
    m_testBtn = new QPushButton(Theme::icon(QStringLiteral("refresh")), tr("Test Connection"), this);
    right->addWidget(m_testBtn);
    right->addStretch(1);
    cols->addLayout(right);
    root->addLayout(cols);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(cfg.id > 0 ? tr("Save") : tr("Add Camera"));
    buttons->button(QDialogButtonBox::Ok)->setProperty("variant", "primary");
    root->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, &CameraDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &CameraDialog::reject);
    connect(m_typeGroup, &QButtonGroup::idClicked, this, [this](int id) { setType(SourceType(id)); });
    connect(browseBtn, &QPushButton::clicked, this, &CameraDialog::browse);
    connect(m_testBtn, &QPushButton::clicked, this, &CameraDialog::testConnection);
    connect(m_detectBtn, &QPushButton::clicked, this, &CameraDialog::detectUsb);
    connect(&m_probe, &QFutureWatcher<ProbeResult>::finished, this, [this] {
        const ProbeResult r = m_probe.result();
        m_testBtn->setEnabled(true);
        if (r.ok && !r.preview.isNull()) {
            m_preview->setPixmap(QPixmap::fromImage(r.preview.scaled(m_preview->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation)));
            m_testInfo->setText(tr("Connected: %1").arg(r.info));
            m_testInfo->setStyleSheet(QStringLiteral("color:#3ddc84;"));
        } else {
            m_preview->setPixmap(QPixmap());
            m_preview->setText(tr("No picture"));
            m_testInfo->setText(tr("Failed: %1").arg(r.info));
            m_testInfo->setStyleSheet(QStringLiteral("color:#ff8a8a;"));
        }
    });
    connect(&m_usbProbe, &QFutureWatcher<QStringList>::finished, this, [this] {
        const QStringList found = m_usbProbe.result();
        m_detectBtn->setEnabled(true);
        if (found.isEmpty()) {
            m_testInfo->setText(tr("No USB cameras found."));
            return;
        }
        m_usbIndex->clear();
        for (const QString& f : found) {
            const int idx = f.section(QLatin1Char('|'), 0, 0).toInt();
            m_usbIndex->addItem(f.section(QLatin1Char('|'), 1), idx);
        }
        m_testInfo->setText(tr("%1 USB camera(s) found.").arg(found.size()));
        m_testInfo->setStyleSheet(QString());
    });

    // Initial values
    m_path->setText(cfg.type == SourceType::Image || cfg.type == SourceType::Video ? cfg.source : QString());
    m_loop->setChecked(cfg.loop);
    m_usbIndex->setCurrentIndex(qBound(0, cfg.usbIndex, 9));
    for (int i = 0; i < m_usbRes->count(); ++i)
        if (m_usbRes->itemData(i).toSize() == QSize(cfg.reqWidth, cfg.reqHeight))
            m_usbRes->setCurrentIndex(i);
    if (cfg.type == SourceType::IpCamera) {
        // Split embedded credentials back into the form fields.
        QUrl u(cfg.source);
        m_user->setText(u.userName());
        m_pass->setText(u.password());
        u.setUserInfo(QString());
        m_url->setText(u.toString());
    }
    setType(cfg.type);
}

void CameraDialog::setType(SourceType t)
{
    m_cfg.type = t;
    if (QAbstractButton* b = m_typeGroup->button(int(t)))
        b->setChecked(true);
    switch (t) {
    case SourceType::Image:
    case SourceType::Video:
        m_stack->setCurrentIndex(0);
        m_loop->setVisible(t == SourceType::Video);
        break;
    case SourceType::UsbCamera: m_stack->setCurrentIndex(1); break;
    case SourceType::IpCamera:  m_stack->setCurrentIndex(2); break;
    }
}

void CameraDialog::browse()
{
    const bool image = m_cfg.type == SourceType::Image;
    const QString filter = image ? tr(kImageFilter) : tr(kVideoFilter);
    const QString path = QFileDialog::getOpenFileName(this, image ? tr("Select image") : tr("Select video"),
                                                      QFileInfo(m_path->text()).absolutePath(), filter);
    if (path.isEmpty())
        return;
    m_path->setText(QDir::toNativeSeparators(path));
    if (m_name->text().trimmed().isEmpty())
        m_name->setText(QFileInfo(path).completeBaseName().left(40));
}

QString CameraDialog::composedUrl() const
{
    const QString raw = m_url->text().trimmed();
    if (m_user->text().isEmpty())
        return raw;
    QUrl u(raw);
    if (!u.isValid() || u.scheme().isEmpty())
        return raw;
    u.setUserName(m_user->text());
    u.setPassword(m_pass->text());
    return u.toString();
}

CameraConfig CameraDialog::config() const
{
    CameraConfig c = m_cfg;
    c.name = m_name->text().trimmed();
    c.location = m_location->currentText().trimmed();
    c.analysisEnabled = m_analysis->isChecked();
    c.capacity = m_capacity->value();
    switch (c.type) {
    case SourceType::Image:
    case SourceType::Video:
        c.source = QDir::fromNativeSeparators(m_path->text().trimmed());
        c.loop = m_loop->isChecked();
        break;
    case SourceType::UsbCamera: {
        c.usbIndex = m_usbIndex->currentData().toInt();
        const QSize s = m_usbRes->currentData().toSize();
        c.reqWidth = s.isValid() ? s.width() : 0;
        c.reqHeight = s.isValid() ? s.height() : 0;
        c.source.clear();
        break;
    }
    case SourceType::IpCamera:
        c.source = composedUrl();
        break;
    }
    return c;
}

void CameraDialog::accept()
{
    const CameraConfig c = config();
    if (c.name.isEmpty()) {
        QMessageBox::warning(this, windowTitle(), tr("Please enter a camera name."));
        m_name->setFocus();
        return;
    }
    if ((c.type == SourceType::Image || c.type == SourceType::Video) && !QFileInfo::exists(c.source)) {
        QMessageBox::warning(this, windowTitle(), tr("The selected file does not exist."));
        return;
    }
    if (c.type == SourceType::IpCamera && !c.source.contains(QLatin1String("://"))) {
        QMessageBox::warning(this, windowTitle(), tr("Enter a stream URL such as rtsp://host:554/stream"));
        m_url->setFocus();
        return;
    }
    QDialog::accept();
}

void CameraDialog::testConnection()
{
    if (m_probe.isRunning())
        return;
    const CameraConfig c = config();
    m_testBtn->setEnabled(false);
    m_preview->setPixmap(QPixmap());
    m_preview->setText(tr("Connecting..."));
    m_testInfo->setText(QString());
    m_probe.setFuture(QtConcurrent::run([c]() {
        ProbeResult r;
        r.ok = CameraWorker::probe(c, &r.preview, &r.info);
        return r;
    }));
}

void CameraDialog::detectUsb()
{
    if (m_usbProbe.isRunning())
        return;
    m_detectBtn->setEnabled(false);
    m_testInfo->setText(tr("Scanning USB devices..."));
    m_usbProbe.setFuture(QtConcurrent::run([]() {
        QStringList found;
        for (int i = 0; i < 6; ++i) {
            cv::VideoCapture cap(i, cv::CAP_DSHOW);
            if (!cap.isOpened())
                continue;
            const int w = int(cap.get(cv::CAP_PROP_FRAME_WIDTH));
            const int h = int(cap.get(cv::CAP_PROP_FRAME_HEIGHT));
            found << QStringLiteral("%1|Device #%1  (%2 x %3)").arg(i).arg(w).arg(h);
        }
        return found;
    }));
}
