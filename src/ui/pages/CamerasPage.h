#pragma once

#include "ui/AppCore.h"

#include <QTimer>
#include <QWidget>

class QLabel;
class QTableWidget;

class CamerasPage : public QWidget {
    Q_OBJECT
public:
    CamerasPage(const AppCore& core, QWidget* parent = nullptr);

    void addCamera();
    void editCamera(int cameraId);
    int addFileSources(const QStringList& paths);   // returns number added

signals:
    void statusMessage(const QString& text);

private:
    void rebuild();
    void updateLiveColumns();
    int selectedId() const;

    AppCore m_core;
    QLabel* m_subtitle = nullptr;
    QTableWidget* m_table = nullptr;
    QTimer m_timer;
};
