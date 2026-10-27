#pragma once

#include "UserManager.h"

// Signed-in user and what each role may do.
//   Viewer   - watch video, analytics and events (read only)
//   Operator - + start/stop analysis, acknowledge alerts, snapshots/recordings,
//              reset counters, switch AI functions
//   Admin    - + cameras, zones & rules, settings, user management
enum class Permission {
    ControlAnalysis,
    AcknowledgeEvents,
    CaptureMedia,
    DeleteMedia,
    ResetCounters,
    ToggleFunctions,
    EditZones,
    ManageCameras,
    EditSettings,
    ManageUsers,
    DeleteEvents,
};

class Session {
public:
    static Session& instance();

    void signIn(const UserAccount& user) { m_user = user; }
    void signOut() { m_user = UserAccount(); }
    bool isSignedIn() const { return m_user.id > 0; }
    const UserAccount& user() const { return m_user; }
    void refresh();   // reload the account (e.g. after an admin changed its role)

    bool can(Permission p) const { return allowed(m_user.role, p) && isSignedIn(); }
    static bool allowed(UserRole role, Permission p);

private:
    UserAccount m_user;
};
