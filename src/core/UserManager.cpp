#include "UserManager.h"
#include "PasswordHash.h"

#include <QObject>
#include <QRegularExpression>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

QString userRoleName(UserRole r)
{
    switch (r) {
    case UserRole::Viewer:   return QObject::tr("Viewer");
    case UserRole::Operator: return QObject::tr("Operator");
    case UserRole::Admin:    return QObject::tr("Administrator");
    }
    return QString();
}

QString userStatusName(UserStatus s)
{
    switch (s) {
    case UserStatus::Pending:  return QObject::tr("Pending approval");
    case UserStatus::Active:   return QObject::tr("Active");
    case UserStatus::Disabled: return QObject::tr("Disabled");
    }
    return QString();
}

namespace {

const char* kColumns = "id, username, display_name, role, status, created, last_login, failed_attempts, locked_until, "
                       "must_change";

UserAccount fromQuery(const QSqlQuery& q)
{
    UserAccount u;
    u.id = q.value(0).toInt();
    u.username = q.value(1).toString();
    u.displayName = q.value(2).toString();
    u.role = UserRole(qBound(0, q.value(3).toInt(), 2));
    u.status = UserStatus(qBound(0, q.value(4).toInt(), 2));
    u.created = QDateTime::fromMSecsSinceEpoch(q.value(5).toLongLong());
    if (q.value(6).toLongLong() > 0)
        u.lastLogin = QDateTime::fromMSecsSinceEpoch(q.value(6).toLongLong());
    u.failedAttempts = q.value(7).toInt();
    if (q.value(8).toLongLong() > 0)
        u.lockedUntil = QDateTime::fromMSecsSinceEpoch(q.value(8).toLongLong());
    u.mustChangePassword = q.value(9).toInt() != 0;
    return u;
}

} // namespace

UserManager& UserManager::instance()
{
    static UserManager m;
    return m;
}

bool UserManager::init(const QString& connectionName, QString* error)
{
    QSqlDatabase db = QSqlDatabase::database(connectionName);
    if (!db.isOpen()) {
        if (error)
            *error = QObject::tr("Database is not open");
        return false;
    }
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS users ("
                               " id INTEGER PRIMARY KEY AUTOINCREMENT, username TEXT NOT NULL UNIQUE COLLATE NOCASE,"
                               " display_name TEXT, password_hash TEXT NOT NULL, role INTEGER NOT NULL,"
                               " status INTEGER NOT NULL, created INTEGER, last_login INTEGER DEFAULT 0,"
                               " failed_attempts INTEGER DEFAULT 0, locked_until INTEGER DEFAULT 0,"
                               " must_change INTEGER DEFAULT 0)"))) {
        if (error)
            *error = q.lastError().text();
        return false;
    }
    m_conn = connectionName;
    return true;
}

int UserManager::userCount() const
{
    QSqlQuery q(QSqlDatabase::database(m_conn));
    if (q.exec(QStringLiteral("SELECT COUNT(*) FROM users")) && q.next())
        return q.value(0).toInt();
    return 0;
}

int UserManager::activeAdminCount() const
{
    QSqlQuery q(QSqlDatabase::database(m_conn));
    q.prepare(QStringLiteral("SELECT COUNT(*) FROM users WHERE role = ? AND status = ?"));
    q.addBindValue(int(UserRole::Admin));
    q.addBindValue(int(UserStatus::Active));
    if (q.exec() && q.next())
        return q.value(0).toInt();
    return 0;
}

QVector<UserAccount> UserManager::users() const
{
    QVector<UserAccount> out;
    QSqlQuery q(QSqlDatabase::database(m_conn));
    if (q.exec(QStringLiteral("SELECT %1 FROM users ORDER BY status, role DESC, username").arg(QLatin1String(kColumns))))
        while (q.next())
            out.push_back(fromQuery(q));
    return out;
}

bool UserManager::find(const QString& username, UserAccount* out) const
{
    QSqlQuery q(QSqlDatabase::database(m_conn));
    q.prepare(QStringLiteral("SELECT %1 FROM users WHERE username = ?").arg(QLatin1String(kColumns)));
    q.addBindValue(username.trimmed());
    if (!q.exec() || !q.next())
        return false;
    if (out)
        *out = fromQuery(q);
    return true;
}

bool UserManager::findById(int id, UserAccount* out) const
{
    QSqlQuery q(QSqlDatabase::database(m_conn));
    q.prepare(QStringLiteral("SELECT %1 FROM users WHERE id = ?").arg(QLatin1String(kColumns)));
    q.addBindValue(id);
    if (!q.exec() || !q.next())
        return false;
    if (out)
        *out = fromQuery(q);
    return true;
}

bool UserManager::storedHash(int id, QString* hash) const
{
    QSqlQuery q(QSqlDatabase::database(m_conn));
    q.prepare(QStringLiteral("SELECT password_hash FROM users WHERE id = ?"));
    q.addBindValue(id);
    if (!q.exec() || !q.next())
        return false;
    *hash = q.value(0).toString();
    return true;
}

bool UserManager::validUsername(const QString& username, QString* error)
{
    static const QRegularExpression re(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._-]{2,31}$"));
    if (re.match(username).hasMatch())
        return true;
    if (error)
        *error = QObject::tr("User name must be 3-32 characters: letters, digits, '.', '_' or '-'.");
    return false;
}

bool UserManager::validPassword(const QString& password, QString* error)
{
    bool letter = false, digit = false;
    for (const QChar c : password) {
        letter = letter || c.isLetter();
        digit = digit || c.isDigit();
    }
    if (password.size() >= 8 && letter && digit)
        return true;
    if (error)
        *error = QObject::tr("Password must be at least 8 characters and contain letters and digits.");
    return false;
}

int UserManager::createUser(const QString& username, const QString& displayName, const QString& password,
                            UserRole role, UserStatus status, QString* error, bool mustChangePassword)
{
    const QString name = username.trimmed();
    if (!validUsername(name, error) || !validPassword(password, error))
        return 0;
    if (find(name, nullptr)) {
        if (error)
            *error = QObject::tr("The user name \"%1\" is already taken.").arg(name);
        return 0;
    }
    QSqlQuery q(QSqlDatabase::database(m_conn));
    q.prepare(QStringLiteral("INSERT INTO users (username, display_name, password_hash, role, status, created, must_change)"
                             " VALUES (?, ?, ?, ?, ?, ?, ?)"));
    q.addBindValue(name);
    q.addBindValue(displayName.trimmed().isEmpty() ? name : displayName.trimmed());
    q.addBindValue(PasswordHash::make(password));
    q.addBindValue(int(role));
    q.addBindValue(int(status));
    q.addBindValue(QDateTime::currentMSecsSinceEpoch());
    q.addBindValue(mustChangePassword ? 1 : 0);
    if (!q.exec()) {
        if (error)
            *error = q.lastError().text();
        return 0;
    }
    return q.lastInsertId().toInt();
}

UserManager::AuthResult UserManager::authenticate(const QString& username, const QString& password, UserAccount* out,
                                                  QDateTime* lockedUntil)
{
    UserAccount u;
    if (!find(username, &u)) {
        PasswordHash::make(password);   // similar timing whether or not the user exists
        return AuthResult::UnknownOrWrong;
    }
    const QDateTime now = QDateTime::currentDateTime();
    if (u.lockedUntil.isValid() && u.lockedUntil > now) {
        if (lockedUntil)
            *lockedUntil = u.lockedUntil;
        return AuthResult::Locked;
    }
    QString hash;
    storedHash(u.id, &hash);
    QSqlQuery q(QSqlDatabase::database(m_conn));
    if (!PasswordHash::verify(password, hash)) {
        const int failed = u.failedAttempts + 1;
        const bool lock = failed >= kMaxFailedAttempts;
        q.prepare(QStringLiteral("UPDATE users SET failed_attempts = ?, locked_until = ? WHERE id = ?"));
        q.addBindValue(lock ? 0 : failed);
        q.addBindValue(lock ? now.addSecs(kLockMinutes * 60).toMSecsSinceEpoch() : 0);
        q.addBindValue(u.id);
        q.exec();
        if (lock) {
            if (lockedUntil)
                *lockedUntil = now.addSecs(kLockMinutes * 60);
            return AuthResult::Locked;
        }
        return AuthResult::UnknownOrWrong;
    }
    // Correct password: status is only revealed to someone who knows it.
    if (u.status == UserStatus::Pending)
        return AuthResult::Pending;
    if (u.status == UserStatus::Disabled)
        return AuthResult::Disabled;
    q.prepare(QStringLiteral("UPDATE users SET failed_attempts = 0, locked_until = 0, last_login = ? WHERE id = ?"));
    q.addBindValue(now.toMSecsSinceEpoch());
    q.addBindValue(u.id);
    q.exec();
    u.lastLogin = now;
    u.failedAttempts = 0;
    u.lockedUntil = QDateTime();
    if (out)
        *out = u;
    return AuthResult::Ok;
}

bool UserManager::wouldLoseLastAdmin(int id, UserRole newRole, UserStatus newStatus) const
{
    UserAccount u;
    if (!findById(id, &u))
        return false;
    const bool isActiveAdmin = u.role == UserRole::Admin && u.status == UserStatus::Active;
    const bool staysActiveAdmin = newRole == UserRole::Admin && newStatus == UserStatus::Active;
    return isActiveAdmin && !staysActiveAdmin && activeAdminCount() <= 1;
}

bool UserManager::setRole(int id, UserRole role, QString* error)
{
    UserAccount u;
    if (!findById(id, &u))
        return false;
    if (wouldLoseLastAdmin(id, role, u.status)) {
        if (error)
            *error = QObject::tr("There must always be at least one active administrator.");
        return false;
    }
    QSqlQuery q(QSqlDatabase::database(m_conn));
    q.prepare(QStringLiteral("UPDATE users SET role = ? WHERE id = ?"));
    q.addBindValue(int(role));
    q.addBindValue(id);
    return q.exec();
}

bool UserManager::setStatus(int id, UserStatus status, QString* error)
{
    UserAccount u;
    if (!findById(id, &u))
        return false;
    if (wouldLoseLastAdmin(id, u.role, status)) {
        if (error)
            *error = QObject::tr("There must always be at least one active administrator.");
        return false;
    }
    QSqlQuery q(QSqlDatabase::database(m_conn));
    q.prepare(QStringLiteral("UPDATE users SET status = ?, failed_attempts = 0, locked_until = 0 WHERE id = ?"));
    q.addBindValue(int(status));
    q.addBindValue(id);
    return q.exec();
}

bool UserManager::setDisplayName(int id, const QString& name, QString* error)
{
    if (name.trimmed().isEmpty()) {
        if (error)
            *error = QObject::tr("Display name cannot be empty.");
        return false;
    }
    QSqlQuery q(QSqlDatabase::database(m_conn));
    q.prepare(QStringLiteral("UPDATE users SET display_name = ? WHERE id = ?"));
    q.addBindValue(name.trimmed());
    q.addBindValue(id);
    return q.exec();
}

bool UserManager::resetPassword(int id, const QString& newPassword, bool mustChange, QString* error)
{
    if (!validPassword(newPassword, error))
        return false;
    QSqlQuery q(QSqlDatabase::database(m_conn));
    q.prepare(QStringLiteral("UPDATE users SET password_hash = ?, must_change = ?, failed_attempts = 0, locked_until = 0"
                             " WHERE id = ?"));
    q.addBindValue(PasswordHash::make(newPassword));
    q.addBindValue(mustChange ? 1 : 0);
    q.addBindValue(id);
    return q.exec() && q.numRowsAffected() == 1;
}

bool UserManager::changePassword(int id, const QString& oldPassword, const QString& newPassword, QString* error)
{
    QString hash;
    if (!storedHash(id, &hash) || !PasswordHash::verify(oldPassword, hash)) {
        if (error)
            *error = QObject::tr("The current password is not correct.");
        return false;
    }
    if (oldPassword == newPassword) {
        if (error)
            *error = QObject::tr("The new password must be different from the current one.");
        return false;
    }
    return resetPassword(id, newPassword, false, error);
}

bool UserManager::unlock(int id)
{
    QSqlQuery q(QSqlDatabase::database(m_conn));
    q.prepare(QStringLiteral("UPDATE users SET failed_attempts = 0, locked_until = 0 WHERE id = ?"));
    q.addBindValue(id);
    return q.exec();
}

bool UserManager::deleteUser(int id, QString* error)
{
    UserAccount u;
    if (!findById(id, &u))
        return false;
    if (u.role == UserRole::Admin && u.status == UserStatus::Active && activeAdminCount() <= 1) {
        if (error)
            *error = QObject::tr("The last active administrator cannot be deleted.");
        return false;
    }
    QSqlQuery q(QSqlDatabase::database(m_conn));
    q.prepare(QStringLiteral("DELETE FROM users WHERE id = ?"));
    q.addBindValue(id);
    return q.exec();
}
