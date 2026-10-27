#include "EventManager.h"
#include "Database.h"

EventManager::EventManager(QObject* parent) : QObject(parent) {}

void EventManager::addEvent(EventRecord e)
{
    if (!e.time.isValid())
        e.time = QDateTime::currentDateTime();
    e.id = Database::instance().insertEvent(e);
    emit eventAdded(e);
    emit eventsChanged();
}

void EventManager::system(const QString& message, Severity severity)
{
    EventRecord e;
    e.type = EventType::System;
    e.severity = severity;
    e.cameraName = tr("System");
    e.message = message;
    addEvent(e);
}

void EventManager::account(const QString& message, Severity severity)
{
    EventRecord e;
    e.type = EventType::Account;
    e.severity = severity;
    e.cameraName = tr("Users");
    e.message = message;
    addEvent(e);
}

void EventManager::acknowledge(qint64 id)
{
    Database::instance().acknowledge(id);
    emit eventsChanged();
}

void EventManager::acknowledgeAll()
{
    Database::instance().acknowledgeAll();
    emit eventsChanged();
}

int EventManager::unacknowledgedToday() const
{
    return Database::instance().eventCounts(QDateTime(QDate::currentDate(), QTime(0, 0))).unacknowledged;
}
