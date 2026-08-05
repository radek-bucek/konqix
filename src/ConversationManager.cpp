// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Radek Bucek

#include "ConversationManager.h"
#include "ConversationWindow.h"
#include "LogIndex.h"
#include "MessageState.h"
#include "Notifier.h"

#include <QString>

extern "C" {
#include <libpurple/prefs.h>
#include <libpurple/signals.h>
#include <libpurple/server.h>
}

namespace konqix {

namespace {

ConversationManager *g_instance = nullptr;

void cmCreate(PurpleConversation *conv)
{
    if (g_instance) g_instance->onCreate(conv);
}

void cmDestroy(PurpleConversation *conv)
{
    if (g_instance) g_instance->onDestroy(conv);
}

void cmWriteConv(PurpleConversation *conv, const char *name, const char *alias,
                 const char *message, PurpleMessageFlags flags, time_t mtime)
{
    if (!g_instance) return;
    g_instance->onWrite(conv,
                        QString::fromUtf8(name ? name : ""),
                        QString::fromUtf8(alias ? alias : ""),
                        QString::fromUtf8(message ? message : ""),
                        flags, mtime);
}

void cmChatAddUsers(PurpleConversation *conv, GList *cbuddies, gboolean newArrivals)
{
    if (g_instance) g_instance->onChatAddUsers(conv, cbuddies, newArrivals);
}

void cmChatRenameUser(PurpleConversation *, const char *, const char *, const char *)
{
}

void cmChatRemoveUsers(PurpleConversation *conv, GList *users)
{
    if (g_instance) g_instance->onChatRemoveUsers(conv, users);
}

void cmChatUpdateUser(PurpleConversation *, const char *)
{
}

void cmPresent(PurpleConversation *conv)
{
    if (g_instance) g_instance->presentConversation(conv);
}

gboolean cmHasFocus(PurpleConversation *conv)
{
    if (!g_instance) return FALSE;
    auto *w = g_instance->windowFor(conv, false);
    return (w && w->isActiveWindow()) ? TRUE : FALSE;
}

PurpleConversationUiOps g_ops = {
    cmCreate,
    cmDestroy,
    nullptr,         // write_chat — fall back to write_conv
    nullptr,         // write_im — fall back to write_conv
    cmWriteConv,
    cmChatAddUsers,
    cmChatRenameUser,
    cmChatRemoveUsers,
    cmChatUpdateUser,
    cmPresent,
    cmHasFocus,
    nullptr, nullptr, nullptr, // custom smileys
    nullptr,         // send_confirm
    nullptr, nullptr, nullptr, nullptr
};

} // namespace

ConversationManager *ConversationManager::instance()
{
    return g_instance;
}

PurpleConversationUiOps *ConversationManager::uiOps()
{
    return &g_ops;
}

// Signal trampolines. The data pointer is the ConversationManager
// instance — we use it as the registration handle too, so a single
// purple_signals_disconnect_by_handle(g_instance) in shutdown removes
// every listener.
static void cmBuddyTyping(PurpleAccount *acc, const char *name, gpointer data)
{
    auto *self = static_cast<ConversationManager *>(data);
    if (self) self->onBuddyTyping(acc, QString::fromUtf8(name ? name : ""), true);
}
static void cmBuddyTypingStopped(PurpleAccount *acc, const char *name, gpointer data)
{
    auto *self = static_cast<ConversationManager *>(data);
    if (self) self->onBuddyTyping(acc, QString::fromUtf8(name ? name : ""), false);
}

ConversationManager::ConversationManager(QObject *parent) : QObject(parent)
{
    g_instance = this;
}

ConversationManager::~ConversationManager()
{
    disconnectLibpurpleSignals();
    if (g_instance == this)
        g_instance = nullptr;
}

void ConversationManager::connectLibpurpleSignals()
{
    void *handle = purple_conversations_get_handle();
    purple_signal_connect(handle, "buddy-typing", this,
                          PURPLE_CALLBACK(cmBuddyTyping), this);
    purple_signal_connect(handle, "buddy-typing-stopped", this,
                          PURPLE_CALLBACK(cmBuddyTypingStopped), this);
    // "buddy-typed" (paused, not stopped) — treat as stop visually so the
    // indicator clears when the buddy stops typing for a moment.
    purple_signal_connect(handle, "buddy-typed", this,
                          PURPLE_CALLBACK(cmBuddyTypingStopped), this);
}

void ConversationManager::disconnectLibpurpleSignals()
{
    purple_signals_disconnect_by_handle(this);
}

void ConversationManager::onBuddyTyping(PurpleAccount *acc, const QString &name,
                                        bool typing)
{
    if (!acc || name.isEmpty()) return;
    PurpleConversation *conv = purple_find_conversation_with_account(
        PURPLE_CONV_TYPE_IM, name.toUtf8().constData(), acc);
    if (!conv) return;
    auto *w = windowFor(conv, /*createIfMissing=*/false);
    if (w) w->setTypingState(typing);
}

ConversationWindow *ConversationManager::windowFor(PurpleConversation *conv, bool createIfMissing)
{
    auto it = m_windows.find(conv);
    if (it != m_windows.end() && it.value())
        return it.value();
    if (!createIfMissing)
        return nullptr;
    auto *w = new ConversationWindow(conv);
    m_windows.insert(conv, w);
    connect(w, &ConversationWindow::closed, this, [this, conv]() {
        notifyWindowClosed(conv);
    });
    return w;
}

void ConversationManager::onCreate(PurpleConversation *conv)
{
    // Lazily create window on first present()
    Q_UNUSED(conv);
}

void ConversationManager::onDestroy(PurpleConversation *conv)
{
    auto it = m_windows.find(conv);
    if (it != m_windows.end()) {
        if (it.value())
            it.value()->deleteLater();
        m_windows.erase(it);
    }
    // Also drop the conv from MessageState — otherwise its raw pointer
    // lingers in the unread map and a later tray click routes through
    // presentConversation() to a freed PurpleConversation, dereferencing
    // freed memory inside libpurple.
    if (MessageState *ms = MessageState::instance())
        ms->forget(conv);
}

void ConversationManager::onWrite(PurpleConversation *conv, const QString &who,
                                  const QString &alias, const QString &message,
                                  PurpleMessageFlags flags, time_t mtime)
{
    const bool incoming = (flags & PURPLE_MESSAGE_RECV) != 0;
    const bool openConv = purple_prefs_get_bool("/konqix/notify/open_conv");

    // Don't auto-create a window for outgoing/system writes; only for incoming
    // when the user opted in, or when one already exists.
    bool create = !incoming || openConv;
    auto *w = windowFor(conv, create);
    if (w)
        w->appendMessage(who, alias, message, flags, mtime);

    // Any write — incoming, outgoing or system — refreshes the "Last
    // conversation" sort. Logger has written by now so the log dir mtime
    // is up-to-date.
    emit conversationActivity(conv);

    // Refresh the SQLite log index for this conv so HistoryDialog opened
    // right after this message sees the new line.
    if (LogIndex *li = LogIndex::instance()) {
        PurpleAccount *acc = purple_conversation_get_account(conv);
        PurpleLogType lt   = (purple_conversation_get_type(conv) == PURPLE_CONV_TYPE_CHAT)
                           ? PURPLE_LOG_CHAT : PURPLE_LOG_IM;
        li->touchConv(LogIndex::accountKey(acc), lt,
                      QString::fromUtf8(purple_conversation_get_name(conv)));
    }

    if (incoming) {
        bool focused = (w && w->isActiveWindow());
        if (!focused) {
            if (MessageState::instance())
                MessageState::instance()->messageReceived(conv);
            if (Notifier::instance()) {
                QString name = alias.isEmpty() ? who : alias;
                Notifier::instance()->notifyIncoming(name, message);
            }
        }
    }
}

void ConversationManager::onChatAddUsers(PurpleConversation *conv, GList *cbuddies, bool newArrivals)
{
    auto *w = windowFor(conv);
    if (w)
        w->addChatUsers(cbuddies, newArrivals);
}

void ConversationManager::onChatRemoveUsers(PurpleConversation *conv, GList *users)
{
    auto *w = windowFor(conv);
    if (w)
        w->removeChatUsers(users);
}

void ConversationManager::presentConversation(PurpleConversation *conv)
{
    auto *w = windowFor(conv);
    if (w) {
        w->show();
        w->raise();
        w->activateWindow();
    }
    if (MessageState::instance())
        MessageState::instance()->markRead(conv);
    // Only kill the tray flash when there are no other unread chats waiting,
    // so the user keeps the hint for the rest.
    if (Notifier::instance() && MessageState::instance()
        && MessageState::instance()->totalUnread() == 0)
        Notifier::instance()->stopFlashing();
}

void ConversationManager::notifyWindowClosed(PurpleConversation *conv)
{
    auto it = m_windows.find(conv);
    if (it != m_windows.end())
        m_windows.erase(it);
}

} // namespace konqix
