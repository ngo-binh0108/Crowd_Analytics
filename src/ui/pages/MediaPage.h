#pragma once

#include "ui/AppCore.h"

#include <QWidget>

class QComboBox;
class QListWidget;
class QPushButton;
class QTableWidget;
class QTabWidget;

class MediaPage : public QWidget {
    Q_OBJECT
public:
    MediaPage(const AppCore& core, QWidget* parent = nullptr);
    void refresh();
    void applyPermissions();

signals:
    void addVideoSourceRequested(const QString& path);
    void statusMessage(const QString& text);

protected:
    void showEvent(QShowEvent* e) override;

private:
    void loadSnapshots();
    void loadRecordings();
    QString selectedRecording() const;

    AppCore m_core;
    QTabWidget* m_tabs = nullptr;
    QComboBox* m_period = nullptr;
    QListWidget* m_snapshots = nullptr;
    QTableWidget* m_recordings = nullptr;
    QPushButton* m_delBtn = nullptr;
    QPushButton* m_sourceBtn = nullptr;
};
