#pragma once

#include "Types.h"

#include <QObject>

// Stores events in the database and broadcasts them to the UI.
class EventManager : public QObject {
    Q_OBJECT
public:
    explicit EventManager(QObject* parent = nullptr);

    void addEvent(EventRecord e);
    void system(const QString& message, Severity severity = Severity::Info);
    void account(const QString& message, Severity severity = Severity::Info);   // audit trail
    void acknowledge(qint64 id);
    void acknowledgeAll();
    int unacknowledgedToday() const;

signals:
    void eventAdded(const EventRecord& e);
    void eventsChanged();
};
