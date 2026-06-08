// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Radek Bucek

#include <QApplication>
#include <QDialog>
#include <QIcon>
#include <QLabel>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLocale>
#include <QMessageBox>
#include <QPainter>
#include <QPointer>
#include <QProgressBar>
#include <QProxyStyle>
#include <QStyleOption>
#include <QSystemTrayIcon>
#include <QVBoxLayout>

#ifdef HAVE_KWINDOWSYSTEM
#include <KWindowSystem>
#endif

#include "PurpleCore.h"
#include "MainWindow.h"
#include "TrayIcon.h"
#include "ConversationManager.h"
#include "LogIndex.h"
#include "MessageState.h"
#include "Notifier.h"

#include <clocale>

extern "C" {
#include <libpurple/savedstatuses.h>
}

namespace {
// Breeze draws a 1 px separator above the status bar via the
// PE_PanelStatusBar style primitive that QSS `border-top: none` doesn't
// touch. Skipping the primitive removes the line cleanly while leaving the
// menubar separator above the buddy list intact.
class FlatStatusBarStyle : public QProxyStyle
{
public:
    using QProxyStyle::QProxyStyle;
    void drawPrimitive(PrimitiveElement el, const QStyleOption *opt,
                       QPainter *p, const QWidget *w) const override
    {
        if (el == PE_PanelStatusBar)
            return;
        QProxyStyle::drawPrimitive(el, opt, p, w);
    }
};
}

int main(int argc, char *argv[])
{
    // Force the entire stack into English. We do this BEFORE constructing
    // QApplication / initializing libpurple so:
    //   - libpurple's gettext returns English strings ("Available" not "Přítomen")
    //   - Qt's standard dialog buttons stay English ("Cancel" not "Zrušit")
    //   - QLocale-driven names (months in HistoryDialog) come out English
    qputenv("LANGUAGE", "en_US:en");
    qputenv("LC_ALL", "C.UTF-8");
    qputenv("LANG", "C.UTF-8");
    std::setlocale(LC_ALL, "C.UTF-8");
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));

    // Force XCB (X11 via XWayland on Wayland sessions) so we can persist the
    // window position — pure Wayland never exposes the absolute x/y of our
    // own surface to us. Users can override with QT_QPA_PLATFORM=wayland if
    // they don't care about position memory.
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "xcb");

    // Honour fractional desktop scaling (e.g. KWin's 1.25x). Default Qt 6
    // policy is RoundPreferFloor → on a 1.25-scale desktop we'd render at
    // 1.0 and let the compositor bilinear-upscale, which murders image
    // quality. PassThrough lets Qt render at the true scale so images and
    // text stay crisp.
    QApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("konqix"));
    QApplication::setOrganizationName(QStringLiteral("konqix"));
    QApplication::setQuitOnLastWindowClosed(false);
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/konqix.svg")));
    QApplication::setStyle(new FlatStatusBarStyle(QApplication::style()->objectName()));

    // Single-instance lock via a per-user Unix domain socket. If another
    // konqix is already listening, ask it to surface its window and
    // exit ourselves; the running instance handles the "raise" message
    // in the QLocalServer block below.
    const QString singleInstanceName = QStringLiteral("konqix-") +
        QString::fromLocal8Bit(qgetenv("USER"));
    {
        QLocalSocket sock;
        sock.connectToServer(singleInstanceName);
        if (sock.waitForConnected(300)) {
            sock.write("raise");
            sock.waitForBytesWritten(300);
            sock.disconnectFromServer();
            return 0;
        }
    }

    konqix::Notifier notifier;
    konqix::MessageState msgState;
    konqix::ConversationManager convManager;
    konqix::PurpleCore core;
    if (!core.init()) {
        QMessageBox::critical(nullptr, QObject::tr("Konqix"),
            QObject::tr("Failed to initialize libpurple. See terminal for details."));
        return 1;
    }
    // libpurple's signal infra is now up — wire the typing-indicator
    // listeners. (Done here, not in ConversationManager's ctor, because
    // ConversationManager is constructed before libpurple inits.)
    convManager.connectLibpurpleSignals();

    konqix::MainWindow window;
    window.show();

    // Background-build / refresh the SQLite log index. First run on a large
    // archive can take ~30 s; subsequent runs only stat changed files.
    // While indexing is in flight, show a small modeless dialog with an
    // indeterminate progress bar so the user knows konqix is working
    // and any History dialog opened during this time will be partial.
    QPointer<QDialog> indexDlg;
    konqix::LogIndex *li = konqix::LogIndex::instance();
    QObject::connect(li, &konqix::LogIndex::progressChanged, &window,
        [&window, &indexDlg](int done, int total) {
            if (!indexDlg && done < total) {
                indexDlg = new QDialog(&window, Qt::Tool);
                indexDlg->setAttribute(Qt::WA_DeleteOnClose);
                indexDlg->setWindowTitle(
                    QObject::tr("Indexing log archive"));
                auto *lay = new QVBoxLayout(indexDlg);
                // Let the layout shrink-to-fit so the window height tracks
                // the actual label content instead of clipping it.
                lay->setSizeConstraint(QLayout::SetFixedSize);
                auto *lbl = new QLabel(
                    QObject::tr("Building search index…"), indexDlg);
                lay->addWidget(lbl);
                auto *bar = new QProgressBar(indexDlg);
                bar->setRange(0, 0);  // indeterminate "busy" stripe
                bar->setTextVisible(false);
                bar->setMinimumWidth(280);
                lay->addWidget(bar);
                auto *status = new QLabel(indexDlg);
                status->setObjectName(QStringLiteral("statusLabel"));
                status->setAlignment(Qt::AlignCenter);
                status->setStyleSheet(QStringLiteral("color:#777"));
                lay->addWidget(status);
                QObject::connect(konqix::LogIndex::instance(),
                                 &konqix::LogIndex::ready,
                                 indexDlg, &QWidget::close);
                indexDlg->show();
            }
            if (indexDlg) {
                if (auto *st = indexDlg->findChild<QLabel *>(
                        QStringLiteral("statusLabel")))
                    st->setText(QStringLiteral("%1 / %2")
                                    .arg(done).arg(total));
            }
        });
    li->startIndexing();

    konqix::TrayIcon tray(&window);
    if (QSystemTrayIcon::isSystemTrayAvailable())
        tray.show();
    notifier.setTrayIcon(tray.systemTrayIcon());
    // Initial tray icon reflects our persisted pref directly (the saved
    // status returned by libpurple at this point may already have been
    // disturbed by a connecting prpl reporting remote presence).
    notifier.setStatusPrimitive(
        purple_prefs_get_int("/konqix/status/last_primitive"));

    QObject::connect(qApp, &QCoreApplication::aboutToQuit,
                     &window, &konqix::MainWindow::saveGeometryToPrefs);

    // Single-instance listener: any new launch attempts to connect to
    // this socket and sends "raise" — we just surface the main window.
    auto *singleSrv = new QLocalServer(&app);
    QLocalServer::removeServer(singleInstanceName);   // clean up stale
    if (!singleSrv->listen(singleInstanceName)) {
        qWarning("konqix single-instance listen failed: %s",
                 qUtf8Printable(singleSrv->errorString()));
    }
    QObject::connect(singleSrv, &QLocalServer::newConnection, &window,
        [singleSrv, &window]() {
            while (auto *client = singleSrv->nextPendingConnection()) {
                QObject::connect(client, &QLocalSocket::disconnected,
                                 client, &QLocalSocket::deleteLater);
                if (window.isMinimized())
                    window.setWindowState(window.windowState()
                                          & ~Qt::WindowMinimized);
                window.show();
#ifdef HAVE_KWINDOWSYSTEM
                if (QWindow *h = window.windowHandle())
                    KWindowSystem::activateWindow(h);
                else {
                    window.raise();
                    window.activateWindow();
                }
#else
                window.raise();
                window.activateWindow();
#endif
            }
        });

    int rc = app.exec();
    core.shutdown();
    return rc;
}
