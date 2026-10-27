#include "AccountDialogs.h"
#include "core/Database.h"
#include "core/Session.h"
#include "ui/Theme.h"
#include "ui/widgets/Common.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace {

QLineEdit* field(const QString& placeholder, bool password, QWidget* parent)
{
    auto* e = new QLineEdit(parent);
    e->setPlaceholderText(placeholder);
    e->setMinimumHeight(34);
    if (password)
        e->setEchoMode(QLineEdit::Password);
    return e;
}

QPushButton* primary(const QString& text, QWidget* parent)
{
    auto* b = new QPushButton(text, parent);
    b->setProperty("variant", "primary");
    b->setMinimumHeight(38);
    b->setCursor(Qt::PointingHandCursor);
    return b;
}

// The sign-in window runs before the main window exists, so it writes its
// audit entries straight to the database.
void audit(const QString& message, Severity severity)
{
    EventRecord e;
    e.time = QDateTime::currentDateTime();
    e.type = EventType::Account;
    e.severity = severity;
    e.cameraName = QObject::tr("Users");
    e.message = message;
    Database::instance().insertEvent(e);
}

QLabel* messageLabel(QWidget* parent)
{
    auto* l = new QLabel(parent);
    l->setWordWrap(true);
    l->hide();
    return l;
}

} // namespace

// =============================================================================
// LoginDialog
// =============================================================================

LoginDialog::LoginDialog(QWidget* parent, const QString& lockedMessage) : QDialog(parent)
{
    setWindowTitle(tr("Surveillance System - Sign in"));
    setFixedWidth(440);
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(32, 26, 32, 26);
    root->setSpacing(14);

    auto* brand = new QHBoxLayout;
    auto* logo = new QLabel(this);
    logo->setPixmap(Theme::pixmap(QStringLiteral("logo"), QColor(), 44));
    brand->addWidget(logo);
    auto* titles = new QVBoxLayout;
    titles->setSpacing(0);
    auto* title = new QLabel(tr("Surveillance System"), this);
    title->setObjectName(QStringLiteral("AppTitle"));
    titles->addWidget(title);
    titles->addWidget(makeLabel(tr("Crowd Analytics & Security Platform"), "muted", this));
    brand->addLayout(titles, 1);
    root->addLayout(brand);

    if (!lockedMessage.isEmpty()) {
        auto* note = makeLabel(lockedMessage, "hint", this);
        note->setWordWrap(true);
        root->addWidget(note);
    }

    const bool firstRun = UserManager::instance().userCount() == 0;
    auto* tabs = new QHBoxLayout;
    tabs->setSpacing(6);
    m_tabIn = new QPushButton(tr("Sign in"), this);
    m_tabUp = new QPushButton(tr("Sign up"), this);
    for (QPushButton* b : { m_tabIn, m_tabUp }) {
        b->setCheckable(true);
        b->setAutoExclusive(true);
        b->setCursor(Qt::PointingHandCursor);
        tabs->addWidget(b);
    }
    auto* tabRow = new QWidget(this);
    tabRow->setLayout(tabs);
    tabRow->setVisible(!firstRun);
    root->addWidget(tabRow);

    m_stack = new QStackedWidget(this);
    m_stack->addWidget(buildSignIn());
    m_stack->addWidget(buildSignUp());
    m_stack->addWidget(buildFirstRun());
    root->addWidget(m_stack);

    connect(m_tabIn, &QPushButton::clicked, this, [this] { showPage(0); });
    connect(m_tabUp, &QPushButton::clicked, this, [this] { showPage(1); });
    showPage(firstRun ? 2 : 0);
}

void LoginDialog::showPage(int page)
{
    m_stack->setCurrentIndex(page);
    m_tabIn->setChecked(page == 0);
    m_tabUp->setChecked(page == 1);
    if (page == 0)
        m_inUser->setFocus();
    else if (page == 1)
        m_upUser->setFocus();
    else
        m_frUser->setFocus();
    adjustSize();
}

void LoginDialog::setMessage(QLabel* label, const QString& text, bool error)
{
    label->setText(text);
    label->setStyleSheet(error ? QStringLiteral("color:#ff8a8a;") : QStringLiteral("color:#3ddc84;"));
    label->setVisible(!text.isEmpty());
    adjustSize();
}

QWidget* LoginDialog::buildSignIn()
{
    auto* w = new QWidget(this);
    auto* v = new QVBoxLayout(w);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(10);
    m_inUser = field(tr("User name"), false, w);
    m_inPass = field(tr("Password"), true, w);
    m_inMsg = messageLabel(w);
    auto* btn = primary(tr("Sign in"), w);
    btn->setDefault(true);
    v->addWidget(makeLabel(tr("User name"), "secondary", w));
    v->addWidget(m_inUser);
    v->addWidget(makeLabel(tr("Password"), "secondary", w));
    v->addWidget(m_inPass);
    v->addWidget(m_inMsg);
    v->addSpacing(4);
    v->addWidget(btn);
    auto* help = makeLabel(tr("No account yet? Use Sign up. An administrator must approve new accounts."), "muted", w);
    help->setWordWrap(true);
    v->addWidget(help);
    connect(btn, &QPushButton::clicked, this, &LoginDialog::doSignIn);
    connect(m_inPass, &QLineEdit::returnPressed, this, &LoginDialog::doSignIn);
    connect(m_inUser, &QLineEdit::returnPressed, m_inPass, qOverload<>(&QWidget::setFocus));
    return w;
}

QWidget* LoginDialog::buildSignUp()
{
    auto* w = new QWidget(this);
    auto* v = new QVBoxLayout(w);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(8);
    m_upUser = field(tr("User name (3-32 letters, digits, . _ -)"), false, w);
    m_upName = field(tr("Full name"), false, w);
    m_upPass = field(tr("Password (8+ characters, letters and digits)"), true, w);
    m_upPass2 = field(tr("Repeat password"), true, w);
    m_upMsg = messageLabel(w);
    auto* btn = primary(tr("Create account"), w);
    for (QWidget* x : { static_cast<QWidget*>(m_upUser), static_cast<QWidget*>(m_upName),
                        static_cast<QWidget*>(m_upPass), static_cast<QWidget*>(m_upPass2) })
        v->addWidget(x);
    v->addWidget(m_upMsg);
    v->addSpacing(4);
    v->addWidget(btn);
    auto* help = makeLabel(tr("New accounts start as Viewer and can sign in after an administrator approves them."),
                           "muted", w);
    help->setWordWrap(true);
    v->addWidget(help);
    connect(btn, &QPushButton::clicked, this, &LoginDialog::doSignUp);
    connect(m_upPass2, &QLineEdit::returnPressed, this, &LoginDialog::doSignUp);
    return w;
}

QWidget* LoginDialog::buildFirstRun()
{
    auto* w = new QWidget(this);
    auto* v = new QVBoxLayout(w);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(8);
    auto* intro = makeLabel(tr("Welcome. Create the administrator account. The administrator manages cameras, "
                               "settings and the other users."),
                            "hint", w);
    intro->setWordWrap(true);
    v->addWidget(intro);
    m_frUser = field(tr("Administrator user name"), false, w);
    m_frUser->setText(QStringLiteral("admin"));
    m_frName = field(tr("Full name"), false, w);
    m_frName->setText(tr("Administrator"));
    m_frPass = field(tr("Password (8+ characters, letters and digits)"), true, w);
    m_frPass2 = field(tr("Repeat password"), true, w);
    m_frMsg = messageLabel(w);
    auto* btn = primary(tr("Create administrator and sign in"), w);
    for (QWidget* x : { static_cast<QWidget*>(m_frUser), static_cast<QWidget*>(m_frName),
                        static_cast<QWidget*>(m_frPass), static_cast<QWidget*>(m_frPass2) })
        v->addWidget(x);
    v->addWidget(m_frMsg);
    v->addSpacing(4);
    v->addWidget(btn);
    connect(btn, &QPushButton::clicked, this, &LoginDialog::doFirstRun);
    connect(m_frPass2, &QLineEdit::returnPressed, this, &LoginDialog::doFirstRun);
    return w;
}

void LoginDialog::doSignIn()
{
    UserManager& um = UserManager::instance();
    QDateTime lockedUntil;
    UserAccount u;
    switch (um.authenticate(m_inUser->text().trimmed(), m_inPass->text(), &u, &lockedUntil)) {
    case UserManager::AuthResult::Ok:
        m_user = u;
        m_inPass->clear();
        QDialog::accept();
        return;
    case UserManager::AuthResult::UnknownOrWrong:
        setMessage(m_inMsg, tr("Wrong user name or password."), true);
        audit(tr("Failed sign-in attempt for \"%1\"").arg(m_inUser->text().trimmed()), Severity::Warning);
        break;
    case UserManager::AuthResult::Pending:
        setMessage(m_inMsg, tr("This account is waiting for administrator approval."), true);
        break;
    case UserManager::AuthResult::Disabled:
        setMessage(m_inMsg, tr("This account has been disabled. Contact an administrator."), true);
        break;
    case UserManager::AuthResult::Locked:
        audit(tr("Account \"%1\" locked after repeated wrong passwords").arg(m_inUser->text().trimmed()),
              Severity::Critical);
        setMessage(m_inMsg,
                   tr("Too many wrong passwords. The account is locked until %1.")
                       .arg(lockedUntil.toString(QStringLiteral("HH:mm:ss"))),
                   true);
        break;
    }
    m_inPass->clear();
    m_inPass->setFocus();
}

void LoginDialog::doSignUp()
{
    if (m_upPass->text() != m_upPass2->text()) {
        setMessage(m_upMsg, tr("The passwords do not match."), true);
        return;
    }
    QString err;
    const int id = UserManager::instance().createUser(m_upUser->text(), m_upName->text(), m_upPass->text(),
                                                      UserRole::Viewer, UserStatus::Pending, &err);
    if (id <= 0) {
        setMessage(m_upMsg, err, true);
        return;
    }
    const QString name = m_upUser->text().trimmed();
    audit(tr("New account \"%1\" requested - waiting for administrator approval").arg(name), Severity::Warning);
    for (QLineEdit* e : { m_upUser, m_upName, m_upPass, m_upPass2 })
        e->clear();
    showPage(0);
    m_inUser->setText(name);
    setMessage(m_inMsg, tr("Account \"%1\" created. You can sign in once an administrator has approved it.").arg(name),
               false);
}

void LoginDialog::doFirstRun()
{
    if (m_frPass->text() != m_frPass2->text()) {
        setMessage(m_frMsg, tr("The passwords do not match."), true);
        return;
    }
    QString err;
    const int id = UserManager::instance().createUser(m_frUser->text(), m_frName->text(), m_frPass->text(),
                                                      UserRole::Admin, UserStatus::Active, &err);
    if (id <= 0) {
        setMessage(m_frMsg, err, true);
        return;
    }
    audit(tr("Administrator account \"%1\" created (first start)").arg(m_frUser->text().trimmed()), Severity::Info);
    UserAccount u;
    if (UserManager::instance().authenticate(m_frUser->text().trimmed(), m_frPass->text(), &u) ==
        UserManager::AuthResult::Ok) {
        m_user = u;
        QDialog::accept();
    }
}

// =============================================================================
// ChangePasswordDialog
// =============================================================================

ChangePasswordDialog::ChangePasswordDialog(int userId, bool forced, QWidget* parent)
    : QDialog(parent), m_userId(userId)
{
    setWindowTitle(tr("Change password"));
    setFixedWidth(400);
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(24, 20, 24, 20);
    v->setSpacing(8);
    if (forced) {
        auto* note = makeLabel(tr("Your password was set by an administrator. Please choose a new one."), "hint", this);
        note->setWordWrap(true);
        v->addWidget(note);
    }
    m_old = field(tr("Current password"), true, this);
    m_new = field(tr("New password (8+ characters, letters and digits)"), true, this);
    m_new2 = field(tr("Repeat new password"), true, this);
    m_msg = messageLabel(this);
    m_msg->setStyleSheet(QStringLiteral("color:#ff8a8a;"));
    v->addWidget(m_old);
    v->addWidget(m_new);
    v->addWidget(m_new2);
    v->addWidget(m_msg);
    auto* row = new QHBoxLayout;
    row->addStretch(1);
    auto* cancel = new QPushButton(forced ? tr("Later") : tr("Cancel"), this);
    auto* ok = primary(tr("Change password"), this);
    row->addWidget(cancel);
    row->addWidget(ok);
    v->addLayout(row);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(ok, &QPushButton::clicked, this, &ChangePasswordDialog::accept);
}

void ChangePasswordDialog::accept()
{
    if (m_new->text() != m_new2->text()) {
        m_msg->setText(tr("The new passwords do not match."));
        m_msg->show();
        return;
    }
    QString err;
    if (!UserManager::instance().changePassword(m_userId, m_old->text(), m_new->text(), &err)) {
        m_msg->setText(err);
        m_msg->show();
        return;
    }
    QDialog::accept();
}

// =============================================================================
// UserEditDialog
// =============================================================================

UserEditDialog::UserEditDialog(const UserAccount* existing, QWidget* parent)
    : QDialog(parent), m_create(existing == nullptr)
{
    setWindowTitle(m_create ? tr("Add user") : tr("Edit user"));
    setFixedWidth(440);
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(24, 20, 24, 20);
    auto* form = new QFormLayout;
    form->setSpacing(9);
    m_user = new QLineEdit(this);
    m_name = new QLineEdit(this);
    m_role = new QComboBox(this);
    for (UserRole r : { UserRole::Viewer, UserRole::Operator, UserRole::Admin })
        m_role->addItem(userRoleName(r), int(r));
    m_status = new QComboBox(this);
    for (UserStatus s : { UserStatus::Active, UserStatus::Pending, UserStatus::Disabled })
        m_status->addItem(userStatusName(s), int(s));
    m_pass = new QLineEdit(this);
    m_pass->setEchoMode(QLineEdit::Password);
    m_pass->setPlaceholderText(m_create ? tr("8+ characters, letters and digits") : tr("leave empty to keep"));
    m_mustChange = new QCheckBox(tr("User must change the password at next sign-in"), this);
    m_mustChange->setChecked(true);
    form->addRow(tr("User name"), m_user);
    form->addRow(tr("Full name"), m_name);
    form->addRow(tr("Role"), m_role);
    form->addRow(tr("Status"), m_status);
    form->addRow(m_create ? tr("Password") : tr("New password"), m_pass);
    form->addRow(m_mustChange);
    v->addLayout(form);

    auto* roles = makeLabel(tr("Viewer: watch video, analytics and events.\n"
                               "Operator: + start/stop analysis, acknowledge alerts, snapshots and recordings.\n"
                               "Administrator: + cameras, zones, settings and users."),
                            "muted", this);
    roles->setWordWrap(true);
    v->addWidget(roles);
    m_msg = messageLabel(this);
    m_msg->setStyleSheet(QStringLiteral("color:#ff8a8a;"));
    v->addWidget(m_msg);

    auto* row = new QHBoxLayout;
    row->addStretch(1);
    auto* cancel = new QPushButton(tr("Cancel"), this);
    auto* ok = primary(m_create ? tr("Add user") : tr("Save"), this);
    row->addWidget(cancel);
    row->addWidget(ok);
    v->addLayout(row);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(ok, &QPushButton::clicked, this, &UserEditDialog::accept);

    if (existing) {
        m_id = existing->id;
        m_user->setText(existing->username);
        m_user->setReadOnly(true);
        m_name->setText(existing->displayName);
        m_role->setCurrentIndex(m_role->findData(int(existing->role)));
        m_status->setCurrentIndex(m_status->findData(int(existing->status)));
        m_mustChange->setChecked(false);
        const bool self = existing->id == Session::instance().user().id;
        // An administrator cannot lock themselves out by accident.
        m_role->setEnabled(!self);
        m_status->setEnabled(!self);
    }
}

void UserEditDialog::accept()
{
    UserManager& um = UserManager::instance();
    QString err;
    const UserRole role = UserRole(m_role->currentData().toInt());
    const UserStatus status = UserStatus(m_status->currentData().toInt());
    bool ok = true;
    if (m_create) {
        ok = um.createUser(m_user->text(), m_name->text(), m_pass->text(), role, status, &err, m_mustChange->isChecked()) > 0;
    } else {
        ok = um.setDisplayName(m_id, m_name->text(), &err) && um.setRole(m_id, role, &err) &&
             um.setStatus(m_id, status, &err);
        if (ok && !m_pass->text().isEmpty())
            ok = um.resetPassword(m_id, m_pass->text(), m_mustChange->isChecked(), &err);
    }
    if (!ok) {
        m_msg->setText(err.isEmpty() ? tr("Could not save the user.") : err);
        m_msg->show();
        return;
    }
    QDialog::accept();
}
