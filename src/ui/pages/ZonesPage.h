#pragma once

#include "ui/AppCore.h"

#include <QTimer>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;
class QStackedWidget;
class QToolButton;
class ZoneEditor;

class ZonesPage : public QWidget {
    Q_OBJECT
public:
    ZonesPage(const AppCore& core, QWidget* parent = nullptr);

    void editCamera(int cameraId);
    bool confirmLeave();   // asks to save unsaved changes; false = stay

signals:
    void statusMessage(const QString& text);

protected:
    void showEvent(QShowEvent* e) override;

private:
    void reloadCameraList();
    void loadCamera(int cameraId);
    void refreshFrame();
    void refreshShapeList();
    void showProperties(int kind, int index);
    void pushZoneForm();
    void pushLineForm();
    void setDirty(bool d);
    void save();
    void pickColor();

    AppCore m_core;
    int m_cameraId = -1;
    bool m_dirty = false;
    bool m_loadingForm = false;

    QListWidget* m_cameraList = nullptr;
    ZoneEditor* m_editor = nullptr;
    QToolButton* m_selectBtn = nullptr;
    QToolButton* m_polyBtn = nullptr;
    QToolButton* m_lineBtn = nullptr;
    QPushButton* m_saveBtn = nullptr;
    QPushButton* m_revertBtn = nullptr;
    QLabel* m_title = nullptr;

    QListWidget* m_shapeList = nullptr;
    QStackedWidget* m_props = nullptr;

    // zone form
    QLineEdit* m_zName = nullptr;
    QComboBox* m_zType = nullptr;
    QLabel* m_zCapLabel = nullptr;
    QSpinBox* m_zCapacity = nullptr;
    QDoubleSpinBox* m_zArea = nullptr;
    QSpinBox* m_zLoiter = nullptr;
    QPushButton* m_zColor = nullptr;
    QCheckBox* m_zTotals = nullptr;
    QCheckBox* m_zEnabled = nullptr;

    // line form
    QLineEdit* m_lName = nullptr;
    QCheckBox* m_lInvert = nullptr;
    QCheckBox* m_lOneWay = nullptr;
    QPushButton* m_lColor = nullptr;
    QCheckBox* m_lTotals = nullptr;
    QCheckBox* m_lEnabled = nullptr;

    QSpinBox* m_camCapacity = nullptr;
    QTimer m_frameRetry;   // fetches the first picture once the camera delivers one
};
