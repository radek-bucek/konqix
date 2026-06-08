// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Radek Bucek

#include "TrayIcon.h"
#include "ConversationManager.h"
#include "MainWindow.h"
#include "MessageState.h"

#include <QAction>
#include <QApplication>
#include <QIcon>
#include <QMenu>
#include <QStyle>
#include <QWindow>

#ifdef HAVE_KWINDOWSYSTEM
#include <KWindowSystem>
#endif

extern "C" {
#include <glib.h>
#include <libpurple/purple.h>
}

namespace konqix {

TrayIcon::TrayIcon(MainWindow *window, QObject *parent)
    : QObject(parent), m_window(window)
{
    m_icon = new QSystemTrayIcon(this);
    m_icon->setIcon(QIcon(QStringLiteral(":/icons/konqix.svg")));
    m_icon->setToolTip(QStringLiteral("Konqix"));

    buildMenu();

    connect(m_icon, &QSystemTrayIcon::activated, this, &TrayIcon::onActivated);
}

void TrayIcon::buildMenu()
{
    m_menu = new QMenu;

    auto *toggleAct = m_menu->addAction(QObject::tr("Show/Hide Buddy List"));
    connect(toggleAct, &QAction::triggered, this, &TrayIcon::toggleMainWindow);

    m_menu->addSeparator();

    auto *availAct = m_menu->addAction(QObject::tr("Available"));
    connect(availAct, &QAction::triggered, m_window, [this]() {
        m_window->changeStatus(PURPLE_STATUS_AVAILABLE);
    });
    auto *awayAct = m_menu->addAction(QObject::tr("Away"));
    connect(awayAct, &QAction::triggered, m_window, [this]() {
        m_window->changeStatus(PURPLE_STATUS_AWAY);
    });
    auto *invisibleAct = m_menu->addAction(QObject::tr("Invisible"));
    connect(invisibleAct, &QAction::triggered, m_window, [this]() {
        m_window->changeStatus(PURPLE_STATUS_INVISIBLE);
    });
    auto *offlineAct = m_menu->addAction(QObject::tr("Offline"));
    connect(offlineAct, &QAction::triggered, m_window, [this]() {
        m_window->changeStatus(PURPLE_STATUS_OFFLINE);
    });

    m_menu->addSeparator();

    auto *accountsAct = m_menu->addAction(QObject::tr("Accounts…"));
    connect(accountsAct, &QAction::triggered, m_window, &MainWindow::showAccountsDialog);

    auto *addBuddyAct = m_menu->addAction(QObject::tr("Add Buddy…"));
    connect(addBuddyAct, &QAction::triggered, m_window, &MainWindow::showAddBuddyDialog);

    m_menu->addSeparator();

    auto *quitAct = m_menu->addAction(QObject::tr("Quit"));
    connect(quitAct, &QAction::triggered, qApp, &QCoreApplication::quit);

    m_icon->setContextMenu(m_menu);
}

void TrayIcon::show()
{
    m_icon->show();
}

void TrayIcon::onActivated(QSystemTrayIcon::ActivationReason reason)
{
    if (reason != QSystemTrayIcon::Trigger && reason != QSystemTrayIcon::DoubleClick)
        return;

    // If there are unread messages, surface those conversations instead of
    // (just) toggling the buddy list.
    if (auto *ms = MessageState::instance()) {
        auto convs = ms->unreadConversations();
        if (!convs.isEmpty()) {
            auto *cm = ConversationManager::instance();
            if (cm) {
                for (PurpleConversation *c : convs)
                    cm->presentConversation(c);
            }
            return;
        }
    }

    toggleMainWindow();
}

void TrayIcon::toggleMainWindow()
{
    if (m_window->isVisible() && !m_window->isMinimized()) {
        m_window->hide();
        return;
    }
    if (m_window->isMinimized())
        m_window->setWindowState(m_window->windowState() & ~Qt::WindowMinimized);
    m_window->show();

#ifdef HAVE_KWINDOWSYSTEM
    // KWin throttles plain QWidget::activateWindow() under focus-stealing
    // prevention (~1 s delay on XWayland). KWindowSystem::activateWindow
    // ships a fresh xdg-activation token / _NET_WM_USER_TIME so KWin grants
    // focus immediately.
    if (QWindow *h = m_window->windowHandle())
        KWindowSystem::activateWindow(h);
    else {
        m_window->raise();
        m_window->activateWindow();
    }
#else
    m_window->raise();
    m_window->activateWindow();
#endif
}

} // namespace konqix
