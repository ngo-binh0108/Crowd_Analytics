#include "core/Database.h"
#include "core/Session.h"
#include "core/Settings.h"
#include "core/Types.h"
#include "ui/AccountDialogs.h"
#include "ui/MainWindow.h"
#include "ui/Theme.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QIcon>
#include <QTimer>
#include <QLockFile>
#include <QMessageBox>

#include <cstdio>

int main(int argc, char* argv[])
{
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
    QApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("SurveillanceSystem"));
    QApplication::setOrganizationName(QStringLiteral("SurveillanceSystem"));
    QApplication::setApplicationDisplayName(QStringLiteral("Surveillance System"));
    QApplication::setApplicationVersion(QStringLiteral(APP_VERSION));

    // Developer options (smoke tests / documentation screenshots):
    //   --page <0..7>            open a page on start
    //   --user/--password        sign in for a --screenshot run (ignored otherwise);
    //                            with --screenshot alone the sign-in window is captured
    //   --screenshot <file.png>  save the window after --delay ms, then quit
    QCommandLineParser cli;
    const QCommandLineOption pageOpt(QStringLiteral("page"), QStringLiteral("Page to open (0-6)."), QStringLiteral("index"));
    const QCommandLineOption shotOpt(QStringLiteral("screenshot"), QStringLiteral("Save a window screenshot and quit."),
                                     QStringLiteral("file"));
    const QCommandLineOption delayOpt(QStringLiteral("delay"), QStringLiteral("Screenshot delay in ms."), QStringLiteral("ms"),
                                      QStringLiteral("6000"));
    const QCommandLineOption userOpt(QStringLiteral("user"), QStringLiteral("Test sign-in (only with --screenshot)."),
                                     QStringLiteral("name"));
    const QCommandLineOption passOpt(QStringLiteral("password"), QStringLiteral("Test sign-in (only with --screenshot)."),
                                     QStringLiteral("password"));
    cli.addOptions({ pageOpt, shotOpt, delayOpt, userOpt, passOpt });
    cli.process(app);

    registerMetaTypes();
    AppSettings& settings = AppSettings::instance();
    settings.load();

    Theme::apply(app);
    QIcon appIcon;
    for (int s : { 16, 24, 32, 48, 64, 128, 256 })
        appIcon.addPixmap(Theme::pixmap(QStringLiteral("logo"), QColor(), s));
    QApplication::setWindowIcon(appIcon);

    // One instance per data folder: two instances would fight over cameras and the database.
    QLockFile lock(settings.dataDir + QStringLiteral("/instance.lock"));
    lock.setStaleLockTime(0);
    if (!lock.tryLock(200)) {
        QMessageBox::information(nullptr, QObject::tr("Surveillance System"),
                                 QObject::tr("The application is already running."));
        return 0;
    }

    // OpenCV's FFmpeg backend reads these when a stream is opened.
    if (settings.rtspOverTcp)
        qputenv("OPENCV_FFMPEG_CAPTURE_OPTIONS", "rtsp_transport;tcp");
    qputenv("OPENCV_LOG_LEVEL", "ERROR");
    qputenv("OPENCV_VIDEOIO_PRIORITY_MSMF", "0");   // prefer DirectShow/FFmpeg: faster camera start-up

    QString error;
    if (!Database::instance().open(settings.dataDir + QStringLiteral("/surveillance.db"), &error) ||
        !UserManager::instance().init(QStringLiteral("main"), &error)) {
        // User accounts live in the database: without it nobody can be signed in.
        QMessageBox::critical(nullptr, QObject::tr("Database"), QObject::tr("Could not open the database:\n%1").arg(error));
        return 1;
    }
    Database::instance().purgeOlderThan(settings.retentionDays);

    // ---- sign in ----------------------------------------------------------
    const bool screenshot = cli.isSet(shotOpt);
    const int shotDelay = qMax(500, cli.value(delayOpt).toInt());
    UserAccount user;
    if (screenshot && cli.isSet(userOpt)) {
        if (UserManager::instance().authenticate(cli.value(userOpt), cli.value(passOpt), &user) !=
            UserManager::AuthResult::Ok) {
            std::fprintf(stderr, "test sign-in failed\n");
            return 2;
        }
    } else {
        LoginDialog login;
        if (screenshot) {
            const QString file = cli.value(shotOpt);
            QTimer::singleShot(shotDelay, &login, [&login, file] {
                login.grab().save(file);
                login.reject();
            });
        }
        if (login.exec() != QDialog::Accepted) {
            Database::instance().close();
            return 0;
        }
        user = login.user();
        if (user.mustChangePassword) {
            ChangePasswordDialog pw(user.id, true);
            pw.exec();
        }
    }
    Session::instance().signIn(user);

    int rc = 0;
    {
        MainWindow w;
        w.showMaximized();
        if (cli.isSet(pageOpt))
            w.showPage(cli.value(pageOpt).toInt());
        if (screenshot) {
            const QString file = cli.value(shotOpt);
            QTimer::singleShot(shotDelay, &w, [&w, file] {
                w.grab().save(file);
                w.quitWithoutPrompt();
            });
        }
        rc = app.exec();
    }
    Database::instance().close();
    return rc;
}
