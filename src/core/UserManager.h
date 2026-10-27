#pragma once

#include <QDateTime>
#include <QString>
#include <QVector>

enum class UserRole { Viewer = 0, Operator = 1, Admin = 2 };
enum class UserStatus { Pending = 0, Active = 1, Disabled = 2 };

QString userRoleName(UserRole r);
QString userStatusName(UserStatus s);

struct UserAccount {
    int id = 0;
    QString username;
    QString displayName;
    UserRole role = UserRole::Viewer;
    UserStatus status = UserStatus::Pending;
    QDateTime created;
    QDateTime lastLogin;
    int failedAttempts = 0;
    QDateTime lockedUntil;
    bool mustChangePassword = false;
};

// User accounts stored in SQLite (table "users"). Passwords are kept only as
// salted PBKDF2 hashes. Enforces the account rules:
//  - self sign-up creates a Pending account that an administrator approves;
//  - 5 wrong passwords lock the account for 5 minutes;
//  - the last active administrator can never be removed, disabled or demoted.
class UserManager {
public:
    enum class AuthResult { Ok, UnknownOrWrong, Pending, Disabled, Locked };

    static UserManager& instance();

    bool init(const QString& connectionName, QString* error);
    bool isReady() const { return !m_conn.isEmpty(); }

    int userCount() const;
    int activeAdminCount() const;
    QVector<UserAccount> users() const;
    bool find(const QString& username, UserAccount* out) const;
    bool findById(int id, UserAccount* out) const;

    // Returns the new id, or 0 with *error set.
    int createUser(const QString& username, const QString& displayName, const QString& password, UserRole role,
                   UserStatus status, QString* error, bool mustChangePassword = false);
    AuthResult authenticate(const QString& username, const QString& password, UserAccount* out,
                            QDateTime* lockedUntil = nullptr);

    bool setRole(int id, UserRole role, QString* error);
    bool setStatus(int id, UserStatus status, QString* error);
    bool setDisplayName(int id, const QString& name, QString* error);
    bool resetPassword(int id, const QString& newPassword, bool mustChange, QString* error);
    bool changePassword(int id, const QString& oldPassword, const QString& newPassword, QString* error);
    bool unlock(int id);
    bool deleteUser(int id, QString* error);

    static bool validUsername(const QString& username, QString* error);
    static bool validPassword(const QString& password, QString* error);

    static constexpr int kMaxFailedAttempts = 5;
    static constexpr int kLockMinutes = 5;

private:
    UserManager() = default;
    bool wouldLoseLastAdmin(int id, UserRole newRole, UserStatus newStatus) const;
    bool storedHash(int id, QString* hash) const;

    QString m_conn;
};
