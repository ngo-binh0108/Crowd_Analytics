#pragma once

#include "core/UserManager.h"
#include "ui/AppCore.h"

#include <QWidget>

class QLabel;
class QPushButton;
class QTableWidget;

// Administrator page: approve sign-ups, add / edit / disable / delete users.
class UsersPage : public QWidget {
    Q_OBJECT
public:
    UsersPage(const AppCore& core, QWidget* parent = nullptr);
    void refresh();
    int pendingCount() const { return m_pending; }

signals:
    void statusMessage(const QString& text);
    void usersChanged();

protected:
    void showEvent(QShowEvent* e) override;

private:
    int selectedId() const;
    bool selected(UserAccount* out) const;
    void updateButtons();
    void audit(const QString& message);

    AppCore m_core;
    QVector<UserAccount> m_users;
    int m_pending = 0;
    QLabel* m_subtitle = nullptr;
    QTableWidget* m_table = nullptr;
    QPushButton* m_edit = nullptr;
    QPushButton* m_approve = nullptr;
    QPushButton* m_toggle = nullptr;
    QPushButton* m_unlock = nullptr;
    QPushButton* m_delete = nullptr;
};
