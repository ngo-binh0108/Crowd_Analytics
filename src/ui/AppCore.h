#pragma once

class AnalyticsStore;
class CameraManager;
class EventManager;

// Services shared by all pages (owned by MainWindow).
struct AppCore {
    CameraManager* cameras = nullptr;
    AnalyticsStore* analytics = nullptr;
    EventManager* events = nullptr;
};
