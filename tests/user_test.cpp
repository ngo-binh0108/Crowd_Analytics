// User accounts: password hashing and the account rules, on an in-memory
// SQLite database (the real users table code, nothing mocked).
#include "core/PasswordHash.h"
#include "core/Session.h"
#include "core/UserManager.h"

#include <QSqlDatabase>
#include <cstdio>

void check(bool ok, const char* what);   // defined in counting_test.cpp

void userTests()
{
    // --- PBKDF2-HMAC-SHA256 against published test vectors ---------------------
    const auto hex = [](int iterations) {
        return PasswordHash::pbkdf2Sha256("password", "salt", iterations, 32).toHex();
    };
    check(hex(1) == "120fb6cffcf8b32c43e7225256c4f837a86548c92ccc35480805987cb70be17b" &&
              hex(2) == "ae4d0c95af6b46d32d0adff928f06dd02a303f8ef3c251dfd6e2d85a95474c43" &&
              hex(4096) == "c5e478d59288c841aa530db6845c4c8d962893a001ce4e11a4963873aa98134a",
          "password: PBKDF2-HMAC-SHA256 matches the published test vectors");
    const QString stored = PasswordHash::make(QStringLiteral("Secret123"));
    QString tampered = stored;
    tampered[tampered.size() - 3] = tampered[tampered.size() - 3] == QLatin1Char('A') ? QLatin1Char('B') : QLatin1Char('A');
    check(PasswordHash::verify(QStringLiteral("Secret123"), stored) && !PasswordHash::verify(QStringLiteral("secret123"), stored) &&
              !PasswordHash::verify(QStringLiteral("Secret123"), tampered) &&
              PasswordHash::make(QStringLiteral("Secret123")) != stored && !stored.contains(QStringLiteral("Secret123")),
          "password: salted hash verifies, rejects wrong/tampered, never stores the password");

    // --- account rules on a fresh in-memory database -----------------------------
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("usertest"));
        db.setDatabaseName(QStringLiteral(":memory:"));
        db.open();
    }
    UserManager& um = UserManager::instance();
    QString err;
    check(um.init(QStringLiteral("usertest"), &err) && um.userCount() == 0, "users: table created, empty at first start");

    const int admin = um.createUser(QStringLiteral("admin"), QStringLiteral("Admin"), QStringLiteral("AdminPass1"),
                                    UserRole::Admin, UserStatus::Active, &err);
    check(admin > 0 && um.activeAdminCount() == 1, "users: first administrator created");
    check(um.createUser(QStringLiteral("ADMIN"), QString(), QStringLiteral("Another123"), UserRole::Viewer,
                        UserStatus::Pending, &err) == 0,
          "users: duplicate user name rejected (case-insensitive)");
    check(um.createUser(QStringLiteral("bob"), QString(), QStringLiteral("short1"), UserRole::Viewer, UserStatus::Pending, &err) == 0 &&
              um.createUser(QStringLiteral("bob"), QString(), QStringLiteral("lettersonly"), UserRole::Viewer,
                            UserStatus::Pending, &err) == 0 &&
              um.createUser(QStringLiteral("b"), QString(), QStringLiteral("GoodPass12"), UserRole::Viewer,
                            UserStatus::Pending, &err) == 0,
          "users: weak passwords and invalid user names rejected");

    // sign-up -> pending -> approved
    const int bob = um.createUser(QStringLiteral("bob"), QStringLiteral("Bob"), QStringLiteral("BobPass123"),
                                  UserRole::Viewer, UserStatus::Pending, &err);
    UserAccount u;
    check(bob > 0 && um.authenticate(QStringLiteral("bob"), QStringLiteral("BobPass123"), &u) == UserManager::AuthResult::Pending,
          "sign-up: new account cannot sign in before approval");
    um.setStatus(bob, UserStatus::Active, &err);
    check(um.authenticate(QStringLiteral("Bob"), QStringLiteral("BobPass123"), &u) == UserManager::AuthResult::Ok &&
              u.id == bob && u.lastLogin.isValid(),
          "sign-up: approved account signs in (user name not case-sensitive)");
    check(um.authenticate(QStringLiteral("nobody"), QStringLiteral("whatever1"), &u) == UserManager::AuthResult::UnknownOrWrong,
          "sign-in: unknown user rejected");

    // lock-out after 5 wrong passwords
    UserManager::AuthResult r = UserManager::AuthResult::Ok;
    for (int i = 0; i < UserManager::kMaxFailedAttempts; ++i)
        r = um.authenticate(QStringLiteral("bob"), QStringLiteral("wrongPass9"), &u);
    const bool lockedEvenWithRightPassword =
        um.authenticate(QStringLiteral("bob"), QStringLiteral("BobPass123"), &u) == UserManager::AuthResult::Locked;
    um.unlock(bob);
    check(r == UserManager::AuthResult::Locked && lockedEvenWithRightPassword &&
              um.authenticate(QStringLiteral("bob"), QStringLiteral("BobPass123"), &u) == UserManager::AuthResult::Ok,
          "sign-in: 5 wrong passwords lock the account (even the right one), unlock restores it");

    um.setStatus(bob, UserStatus::Disabled, &err);
    check(um.authenticate(QStringLiteral("bob"), QStringLiteral("BobPass123"), &u) == UserManager::AuthResult::Disabled,
          "admin: disabled account cannot sign in");
    um.setStatus(bob, UserStatus::Active, &err);

    // the last administrator is protected
    check(!um.setRole(admin, UserRole::Operator, &err) && !um.setStatus(admin, UserStatus::Disabled, &err) &&
              !um.deleteUser(admin, &err) && um.activeAdminCount() == 1,
          "admin: last active administrator cannot be demoted, disabled or deleted");
    um.setRole(bob, UserRole::Admin, &err);
    check(um.setRole(admin, UserRole::Operator, &err) && um.activeAdminCount() == 1,
          "admin: with a second administrator the first one can be demoted");

    // passwords
    check(!um.changePassword(bob, QStringLiteral("wrongOld1"), QStringLiteral("NewPass456"), &err) &&
              um.changePassword(bob, QStringLiteral("BobPass123"), QStringLiteral("NewPass456"), &err) &&
              um.authenticate(QStringLiteral("bob"), QStringLiteral("NewPass456"), &u) == UserManager::AuthResult::Ok,
          "password: change needs the current password");
    um.resetPassword(admin, QStringLiteral("Temp12345"), true, &err);
    check(um.authenticate(QStringLiteral("admin"), QStringLiteral("Temp12345"), &u) == UserManager::AuthResult::Ok &&
              u.mustChangePassword,
          "password: admin reset forces a change at next sign-in");

    // role permissions
    using P = Permission;
    check(!Session::allowed(UserRole::Viewer, P::ControlAnalysis) && !Session::allowed(UserRole::Viewer, P::AcknowledgeEvents) &&
              Session::allowed(UserRole::Operator, P::ControlAnalysis) && Session::allowed(UserRole::Operator, P::CaptureMedia) &&
              !Session::allowed(UserRole::Operator, P::ManageCameras) && !Session::allowed(UserRole::Operator, P::ManageUsers) &&
              Session::allowed(UserRole::Admin, P::ManageUsers) && Session::allowed(UserRole::Admin, P::EditSettings),
          "roles: viewer read-only, operator runs analysis, only admin manages cameras/settings/users");
}
