#pragma once

#include "ui/AppCore.h"

#include <QWidget>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QSpinBox;

class SettingsPage : public QWidget {
    Q_OBJECT
public:
    SettingsPage(const AppCore& core, QWidget* parent = nullptr);
    void load();

signals:
    void settingsSaved();
    void statusMessage(const QString& text);

protected:
    void showEvent(QShowEvent* e) override;

private:
    void save();
    QString browseDir(QLineEdit* target);

    AppCore m_core;

    // General
    QLineEdit* m_siteName = nullptr;
    QComboBox* m_siteType = nullptr;
    QSpinBox* m_siteCapacity = nullptr;
    QCheckBox* m_autoStart = nullptr;
    QCheckBox* m_tray = nullptr;
    QCheckBox* m_rtspTcp = nullptr;

    // Detection
    QComboBox* m_model = nullptr;      // editable: bundled models + any path
    QComboBox* m_format = nullptr;
    QComboBox* m_device = nullptr;
    QSpinBox* m_gpuId = nullptr;
    QComboBox* m_inputSize = nullptr;
    QDoubleSpinBox* m_conf = nullptr;
    QDoubleSpinBox* m_nms = nullptr;
    QDoubleSpinBox* m_fps = nullptr;
    QSpinBox* m_jobs = nullptr;
    QSpinBox* m_threads = nullptr;
    QCheckBox* m_unload = nullptr;
    QCheckBox* m_bags = nullptr;
    QLabel* m_modelStatus = nullptr;

    // Alerts
    QSpinBox* m_warnPct = nullptr;
    QSpinBox* m_critPct = nullptr;
    QSpinBox* m_abandon = nullptr;
    QDoubleSpinBox* m_runSpeed = nullptr;
    QSpinBox* m_cooldown = nullptr;
    QCheckBox* m_sound = nullptr;
    QCheckBox* m_notify = nullptr;
    QCheckBox* m_autoSnap = nullptr;
    QSpinBox* m_autoRecord = nullptr;

    // Storage
    QLineEdit* m_dataDir = nullptr;
    QLineEdit* m_snapDir = nullptr;
    QLineEdit* m_recDir = nullptr;
    QComboBox* m_recFormat = nullptr;
    QSpinBox* m_retention = nullptr;

    // Display
    QSpinBox* m_displayFps = nullptr;
    QCheckBox* m_showIds = nullptr;
    QCheckBox* m_showZones = nullptr;
    QCheckBox* m_showConf = nullptr;
};
