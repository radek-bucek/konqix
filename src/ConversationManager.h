// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Radek Bucek

#pragma once

#include <QObject>
#include <QHash>
#include <QPointer>

extern "C" {
#include <libpurple/conversation.h>
}

namespace konqix {

class ConversationWindow;

class ConversationManager : public QObject
{
    Q_OBJECT
public:
    static ConversationManager *instance();
    static PurpleConversationUiOps *uiOps();

    explicit ConversationManager(QObject *parent = nullptr);
    ~ConversationManager() override;

    ConversationWindow *windowFor(PurpleConversation *conv, bool createIfMissing = true);
    void presentConversation(PurpleConversation *conv);

    void onCreate(PurpleConversation *conv);
    void onDestroy(PurpleConversation *conv);
    void onWrite(PurpleConversation *conv, const QString &who, const QString &alias,
                 const QString &message, PurpleMessageFlags flags, time_t mtime);
    void onChatAddUsers(PurpleConversation *conv, GList *cbuddies, bool newArrivals);
    void onChatRemoveUsers(PurpleConversation *conv, GList *users);

    // libpurple's "buddy-typing(-stopped)" signals route through here so
    // we can flip the typing indicator on the matching conv window.
    void onBuddyTyping(PurpleAccount *acc, const QString &name, bool typing);

    // Hooks libpurple signals to the routing above. Must run after
    // PurpleCore has initialised the libpurple core (signal infra ready).
    void connectLibpurpleSignals();
    void disconnectLibpurpleSignals();

signals:
    void unreadCountChanged(int total);
    // Fires for every conv write (RECV, SEND, system) so the buddy list
    // can re-sort by "Last conversation" without waiting for a status
    // change or unread-count event.
    void conversationActivity(PurpleConversation *conv);

public slots:
    void notifyWindowClosed(PurpleConversation *conv);

private:
    QHash<PurpleConversation *, QPointer<ConversationWindow>> m_windows;
};

} // namespace konqix
