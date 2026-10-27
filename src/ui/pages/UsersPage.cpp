#include "UsersPage.h"
#include "core/EventManager.h"
#include "core/Session.h"
#include "ui/AccountDialogs.h"
#include "ui/Theme.h"
#include "ui/widgets/Common.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {
enum Col { ColUser, ColName, ColRole, ColStatus, ColLastLogin, ColCreated, ColCount };

QColor statusColor(const UserAccount& u)
{
    if (u.lockedUntil.isValid() && u.lockedUntil > QDateTime::currentDateTime())
        return Theme::critical();
    switch (u.status) {
    case UserStatus::Active:   return Theme::good();
    case UserStatus::Pending:  return Theme::warning();
    case UserStatus::Disabled: return Theme::text3();
    }
    return Theme::text3();
}
} // namespace

UsersPage::UsersPage(const AppCore& core, QWidget* parent) : QWidget(parent), m_core(core)
{
    setObjectName(QStringLiteral("Page"));
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(18, 14, 18, 14);
    root->setSpacing(12);

    auto* header = new QHBoxLayout;
    auto* titleCol = new QVBoxLayout;
    titleCol->setSpacing(0);
    titleCol->addWidget(makeLabel(tr("Users"), "pageTitle", this));
    m_subtitle = makeLabel(QString(), "secondary", this);
    titleCol->addWidget(m_subtitle);
    header->addLayout(titleCol);
    header->addStretch(1);
    auto* add = new QPushButton(Theme::icon(QStringLiteral("plus"), Qt::white), tr("Add User"), this);
    add->setProperty("variant", "primary");
    m_approve = new QPushButton(Theme::icon(QStringLiteral("check")), tr("Approve"), this);
    m_edit = new QPushButton(Theme::icon(QStringLiteral("edit")), tr("Edit / Reset Password"), this);
    m_toggle = new QPushButton(tr("Disable"), this);
    m_unlock = new QPushButton(Theme::icon(QStringLiteral("lock")), tr("Unlock"), this);
    m_delete = new QPushButton(Theme::icon(QStringLiteral("trash"), QColor("#ffb3b3")), tr("Delete"), this);
    m_delete->setProperty("variant", "danger");
    for (QPushButton* b : { add, m_approve, m_edit, m_toggle, m_unlock, m_delete })
        header->addWidget(b);
    root->addLayout(header);

    auto* card = new Card(QString(), QString(), this);
    m_table = new QTableWidget(0, ColCount, card);
    m_table->setHorizontalHeaderLabels({ tr("User name"), tr("Full name"), tr("Role"), tr("Status"), tr("Last sign-in"),
                                         tr("Created") });
    m_table->verticalHeader()->hide();
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setAlternatingRowColors(true);
    m_table->setShowGrid(false);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(ColName, QHeaderView::Stretch);
    card->body()->addWidget(m_table);
    root->addWidget(card, 1);

    auto* hint = makeLabel(tr("Viewer: watch video, analytics and events.   Operator: + start/stop analysis, acknowledge "
                              "alerts, snapshots and recordings, reset counters.   Administrator: + cameras, zones, "
                              "settings and users.   New sign-ups wait here for approval. After 5 wrong passwords an "
                              "account is locked for 5 minutes."),
                           "hint", this);
    hint->setWordWrap(true);
    root->addWidget(hint);

    connect(m_table, &QTableWidget::itemSelectionChanged, this, &UsersPage::updateButtons);
    connect(m_table, &QTableWidget::cellDoubleClicked, m_edit, &QPushButton::click);
    connect(add, &QPushButton::clicked, this, [this] {
        UserEditDialog dlg(nullptr, this);
        if (dlg.exec() == QDialog::Accepted) {
            refresh();
            audit(tr("User account created by %1").arg(Session::instance().user().username));
        }
    });
    connect(m_edit, &QPushButton::clicked, this, [this] {
        UserAccount u;
        if (!selected(&u))
            return;
        UserEditDialog dlg(&u, this);
        if (dlg.exec() == QDialog::Accepted) {
            refresh();
            audit(tr("User \"%1\" updated by %2").arg(u.username, Session::instance().user().username));
            emit usersChanged();
        }
    });
    connect(m_approve, &QPushButton::clicked, this, [this] {
        UserAccount u;
        QString err;
        if (!selected(&u))
            return;
        if (!UserManager::instance().setStatus(u.id, UserStatus::Active, &err)) {
            QMessageBox::warning(this, tr("Users"), err);
            return;
        }
        refresh();
        audit(tr("User \"%1\" approved by %2 (role %3)")
                  .arg(u.username, Session::instance().user().username, userRoleName(u.role)));
        emit statusMessage(tr("\"%1\" can now sign in as %2. Use Edit to change the role.").arg(u.username, userRoleName(u.role)));
    });
    connect(m_toggle, &QPushButton::clicked, this, [this] {
        UserAccount u;
        QString err;
        if (!selected(&u))
            return;
        const UserStatus next = u.status == UserStatus::Disabled ? UserStatus::Active : UserStatus::Disabled;
        if (!UserManager::instance().setStatus(u.id, next, &err)) {
            QMessageBox::warning(this, tr("Users"), err);
            return;
        }
        refresh();
        audit(tr("User \"%1\" %2 by %3")
                  .arg(u.username, next == UserStatus::Disabled ? tr("disabled") : tr("enabled"),
                       Session::instance().user().username));
    });
    connect(m_unlock, &QPushButton::clicked, this, [this] {
        UserAccount u;
        if (!selected(&u))
            return;
        UserManager::instance().unlock(u.id);
        refresh();
        audit(tr("User \"%1\" unlocked by %2").arg(u.username, Session::instance().user().username));
    });
    connect(m_delete, &QPushButton::clicked, this, [this] {
        UserAccount u;
        QString err;
        if (!selected(&u))
            return;
        if (QMessageBox::question(this, tr("Delete user"), tr("Delete the account \"%1\"?").arg(u.username)) !=
            QMessageBox::Yes)
            return;
        if (!UserManager::instance().deleteUser(u.id, &err)) {
            QMessageBox::warning(this, tr("Users"), err);
            return;
        }
        refresh();
        audit(tr("User \"%1\" deleted by %2").arg(u.username, Session::instance().user().username));
    });
    refresh();
}

void UsersPage::showEvent(QShowEvent* e)
{
    QWidget::showEvent(e);
    refresh();
}

void UsersPage::audit(const QString& message)
{
    if (m_core.events)
        m_core.events->account(message);
    emit usersChanged();
}

void UsersPage::refresh()
{
    const int keep = selectedId();
    m_users = UserManager::instance().users();
    m_pending = 0;
    m_table->setRowCount(m_users.size());
    const QDateTime now = QDateTime::currentDateTime();
    for (int r = 0; r < m_users.size(); ++r) {
        const UserAccount& u = m_users[r];
        if (u.status == UserStatus::Pending)
            ++m_pending;
        auto set = [&](int c, const QString& t) {
            auto* it = new QTableWidgetItem(t);
            it->setData(Qt::UserRole, u.id);
            m_table->setItem(r, c, it);
            return it;
        };
        auto* user = set(ColUser, u.username);
        user->setIcon(Theme::icon(u.role == UserRole::Admin ? QStringLiteral("shield") : QStringLiteral("user"),
                                  u.role == UserRole::Admin ? Theme::accent2() : Theme::text2()));
        if (u.id == Session::instance().user().id)
            user->setText(u.username + tr("  (you)"));
        set(ColName, u.displayName);
        set(ColRole, userRoleName(u.role));
        const bool locked = u.lockedUntil.isValid() && u.lockedUntil > now;
        auto* st = set(ColStatus, locked ? tr("Locked until %1").arg(u.lockedUntil.toString(QStringLiteral("HH:mm")))
                                         : userStatusName(u.status));
        st->setForeground(statusColor(u));
        set(ColLastLogin, u.lastLogin.isValid() ? u.lastLogin.toString(QStringLiteral("yyyy-MM-dd HH:mm")) : tr("never"));
        set(ColCreated, u.created.toString(QStringLiteral("yyyy-MM-dd HH:mm")));
        if (u.id == keep)
            m_table->selectRow(r);
    }
    m_subtitle->setText(tr("%1 users  |  %2 administrators active  |  %3 waiting for approval")
                            .arg(m_users.size())
                            .arg(UserManager::instance().activeAdminCount())
                            .arg(m_pending));
    updateButtons();
}

int UsersPage::selectedId() const
{
    const int row = m_table->currentRow();
    if (row < 0 || !m_table->item(row, 0) || !m_table->item(row, 0)->isSelected())
        return -1;
    return m_table->item(row, 0)->data(Qt::UserRole).toInt();
}

bool UsersPage::selected(UserAccount* out) const
{
    const int id = selectedId();
    for (const UserAccount& u : m_users)
        if (u.id == id) {
            *out = u;
            return true;
        }
    return false;
}

void UsersPage::updateButtons()
{
    UserAccount u;
    const bool has = selected(&u);
    const bool self = has && u.id == Session::instance().user().id;
    m_edit->setEnabled(has);
    m_approve->setEnabled(has && u.status == UserStatus::Pending);
    m_toggle->setEnabled(has && !self && u.status != UserStatus::Pending);
    m_toggle->setText(has && u.status == UserStatus::Disabled ? tr("Enable") : tr("Disable"));
    m_unlock->setEnabled(has && u.lockedUntil.isValid() && u.lockedUntil > QDateTime::currentDateTime());
    m_delete->setEnabled(has && !self);
}
