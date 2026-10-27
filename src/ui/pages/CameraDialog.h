#pragma once

#include "core/Types.h"

#include <QDialog>
#include <QFutureWatcher>
#include <QImage>

class QButtonGroup;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QStackedWidget;

struct ProbeResult {
    bool ok = false;
    QImage preview;
    QString info;
};

class CameraDialog : public QDialog {
    Q_OBJECT
public:
    CameraDialog(const CameraConfig& cfg, const QStringList& locations, QWidget* parent = nullptr);
    CameraConfig config() const;

    static CameraConfig configForFile(const QString& path);

protected:
    void accept() override;

private:
    void setType(SourceType t);
    void browse();
    void testConnection();
    void detectUsb();
    QString composedUrl() const;

    CameraConfig m_cfg;
    QLineEdit* m_name = nullptr;
    QComboBox* m_location = nullptr;
    QButtonGroup* m_typeGroup = nullptr;
    QStackedWidget* m_stack = nullptr;

    QLineEdit* m_path = nullptr;
    QCheckBox* m_loop = nullptr;
    QComboBox* m_usbIndex = nullptr;
    QComboBox* m_usbRes = nullptr;
    QLineEdit* m_url = nullptr;
    QLineEdit* m_user = nullptr;
    QLineEdit* m_pass = nullptr;

    QCheckBox* m_analysis = nullptr;
    QSpinBox* m_capacity = nullptr;
    QLabel* m_preview = nullptr;
    QLabel* m_testInfo = nullptr;
    QPushButton* m_testBtn = nullptr;
    QPushButton* m_detectBtn = nullptr;
    QFutureWatcher<ProbeResult> m_probe;
    QFutureWatcher<QStringList> m_usbProbe;
};
