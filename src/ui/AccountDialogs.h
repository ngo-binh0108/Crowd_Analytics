#pragma once

#include "core/UserManager.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QStackedWidget;

// Sign-in window shown at start-up and after "Sign out". Has a Sign up tab
// (new accounts wait for administrator approval) and, when no account exists
// yet, a first-run mode that creates the administrator.
class LoginDialog : public QDialog {
    Q_OBJECT
public:
    explicit LoginDialog(QWidget* parent = nullptr, const QString& lockedMessage = QString());
    UserAccount user() const { return m_user; }

private:
    QWidget* buildSignIn();
    QWidget* buildSignUp();
    QWidget* buildFirstRun();
    void showPage(int page);
    void doSignIn();
    void doSignUp();
    void doFirstRun();
    void setMessage(QLabel* label, const QString& text, bool error);

    UserAccount m_user;
    QStackedWidget* m_stack = nullptr;
    QPushButton* m_tabIn = nullptr;
    QPushButton* m_tabUp = nullptr;

    QLineEdit* m_inUser = nullptr;
    QLineEdit* m_inPass = nullptr;
    QLabel* m_inMsg = nullptr;

    QLineEdit* m_upUser = nullptr;
    QLineEdit* m_upName = nullptr;
    QLineEdit* m_upPass = nullptr;
    QLineEdit* m_upPass2 = nullptr;
    QLabel* m_upMsg = nullptr;

    QLineEdit* m_frUser = nullptr;
    QLineEdit* m_frName = nullptr;
    QLineEdit* m_frPass = nullptr;
    QLineEdit* m_frPass2 = nullptr;
    QLabel* m_frMsg = nullptr;
};

// Change own password (also forced after an administrator reset it).
class ChangePasswordDialog : public QDialog {
    Q_OBJECT
public:
    ChangePasswordDialog(int userId, bool forced, QWidget* parent = nullptr);

protected:
    void accept() override;

private:
    int m_userId;
    QLineEdit* m_old = nullptr;
    QLineEdit* m_new = nullptr;
    QLineEdit* m_new2 = nullptr;
    QLabel* m_msg = nullptr;
};

// Administrator: create a user, or edit name / role / status / password.
class UserEditDialog : public QDialog {
    Q_OBJECT
public:
    UserEditDialog(const UserAccount* existing, QWidget* parent = nullptr);

protected:
    void accept() override;

private:
    bool m_create;
    int m_id = 0;
    QLineEdit* m_user = nullptr;
    QLineEdit* m_name = nullptr;
    QComboBox* m_role = nullptr;
    QComboBox* m_status = nullptr;
    QLineEdit* m_pass = nullptr;
    QCheckBox* m_mustChange = nullptr;
    QLabel* m_msg = nullptr;
};
