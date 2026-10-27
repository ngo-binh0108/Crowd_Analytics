#include "Session.h"

Session& Session::instance()
{
    static Session s;
    return s;
}

void Session::refresh()
{
    UserAccount u;
    if (m_user.id > 0 && UserManager::instance().findById(m_user.id, &u))
        m_user = u;
}

bool Session::allowed(UserRole role, Permission p)
{
    switch (p) {
    case Permission::ControlAnalysis:
    case Permission::AcknowledgeEvents:
    case Permission::CaptureMedia:
    case Permission::ResetCounters:
    case Permission::ToggleFunctions:
        return role == UserRole::Operator || role == UserRole::Admin;
    case Permission::DeleteMedia:
    case Permission::DeleteEvents:
    case Permission::EditZones:
    case Permission::ManageCameras:
    case Permission::EditSettings:
    case Permission::ManageUsers:
        return role == UserRole::Admin;
    }
    return false;
}
