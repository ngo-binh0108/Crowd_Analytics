#pragma once

#include "core/Types.h"
#include "ui/AppCore.h"

#include <QTimer>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QDateEdit;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;
class StatTile;

class EventsPage : public QWidget {
    Q_OBJECT
public:
    EventsPage(const AppCore& core, QWidget* parent = nullptr);
    void refresh();
    void applyPermissions();
    void refreshCameraFilter();

signals:
    void statusMessage(const QString& text);

protected:
    void showEvent(QShowEvent* e) override;

private:
    void showDetails(int row);
    void updateCounters();
    void exportCsv();

    AppCore m_core;
    QVector<EventRecord> m_rows;

    StatTile* m_cCritical = nullptr;
    StatTile* m_cWarning = nullptr;
    StatTile* m_cInfo = nullptr;
    StatTile* m_cOpen = nullptr;

    QComboBox* m_severity = nullptr;
    QComboBox* m_type = nullptr;
    QComboBox* m_camera = nullptr;
    QDateEdit* m_from = nullptr;
    QDateEdit* m_to = nullptr;
    QLineEdit* m_search = nullptr;
    QCheckBox* m_unackOnly = nullptr;
    QTableWidget* m_table = nullptr;

    QLabel* m_preview = nullptr;
    QLabel* m_details = nullptr;
    QPushButton* m_ackBtn = nullptr;
    QPushButton* m_ackAllBtn = nullptr;
    QPushButton* m_purgeBtn = nullptr;
    QPushButton* m_openBtn = nullptr;
    QPushButton* m_folderBtn = nullptr;
    QTimer m_debounce;
};
