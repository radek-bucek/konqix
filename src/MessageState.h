// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Radek Bucek

#pragma once

#include <QObject>
#include <QHash>

extern "C" {
#include <libpurple/blist.h>
#include <libpurple/conversation.h>
}

namespace konqix {

// Tracks per-conversation unread counts plus knows how to map buddy/blist nodes
// to those counts so the buddy list can surface unread indicators.
class MessageState : public QObject
{
    Q_OBJECT
public:
    static MessageState *instance();
    explicit MessageState(QObject *parent = nullptr);
    ~MessageState() override;

    // Bookkeeping
    void messageReceived(PurpleConversation *conv);
    void markRead(PurpleConversation *conv);
    // Drop any bookkeeping for a conversation that is about to be freed by
    // libpurple. Called from ConversationManager::onDestroy so unread hooks
    // (tray click, buddy-list indicator) never dereference a dangling ptr.
    void forget(PurpleConversation *conv);

    // Queries
    int unreadFor(PurpleConversation *conv) const;
    int totalUnread() const;
    QList<PurpleConversation *> unreadConversations() const;

    int unreadForBuddyName(PurpleAccount *acc, const char *name) const;
    bool nodeHasUnread(PurpleBlistNode *node) const;

signals:
    void unreadChanged();

private:
    static QByteArray keyFor(PurpleAccount *acc, const char *name);
    static QByteArray keyForConv(PurpleConversation *conv);

    // Indexed both ways so we can answer "unread for buddy" without an active
    // conversation, and "stop flashing for these convs" with the conv pointer.
    QHash<PurpleConversation *, int> m_byConv;
    QHash<QByteArray, int> m_byKey;
};

} // namespace konqix
