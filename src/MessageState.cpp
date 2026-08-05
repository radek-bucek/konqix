// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Radek Bucek

#include "MessageState.h"

extern "C" {
#include <libpurple/account.h>
#include <libpurple/blist.h>
}

namespace konqix {

namespace {
MessageState *g_instance = nullptr;
}

MessageState *MessageState::instance() { return g_instance; }

MessageState::MessageState(QObject *parent) : QObject(parent)
{
    g_instance = this;
}

MessageState::~MessageState()
{
    if (g_instance == this)
        g_instance = nullptr;
}

QByteArray MessageState::keyFor(PurpleAccount *acc, const char *name)
{
    if (!acc || !name) return {};
    QByteArray k;
    k.append(purple_account_get_protocol_id(acc));
    k.append('|');
    k.append(purple_account_get_username(acc));
    k.append('|');
    k.append(name);
    return k;
}

QByteArray MessageState::keyForConv(PurpleConversation *conv)
{
    if (!conv) return {};
    return keyFor(purple_conversation_get_account(conv),
                  purple_conversation_get_name(conv));
}

void MessageState::messageReceived(PurpleConversation *conv)
{
    if (!conv) return;
    QByteArray k = keyForConv(conv);
    m_byConv[conv]++;
    m_byKey[k]++;
    emit unreadChanged();
}

void MessageState::markRead(PurpleConversation *conv)
{
    if (!conv) return;
    auto it = m_byConv.find(conv);
    if (it == m_byConv.end() || it.value() == 0)
        return;
    m_byConv.erase(it);
    m_byKey.remove(keyForConv(conv));
    emit unreadChanged();
}

void MessageState::forget(PurpleConversation *conv)
{
    if (!conv) return;
    // Read the by-key entry BEFORE we lose the ability to derive it (we still
    // can, since we haven't freed conv ourselves — libpurple is only about
    // to). But if a caller passes an already-freed pointer, this would
    // dereference garbage. onDestroy fires before purple frees the conv, so
    // keyForConv() is still valid at call time.
    auto it = m_byConv.find(conv);
    if (it == m_byConv.end())
        return;
    m_byKey.remove(keyForConv(conv));
    m_byConv.erase(it);
    emit unreadChanged();
}

int MessageState::unreadFor(PurpleConversation *conv) const
{
    return m_byConv.value(conv, 0);
}

int MessageState::totalUnread() const
{
    int t = 0;
    for (int v : m_byConv) t += v;
    return t;
}

QList<PurpleConversation *> MessageState::unreadConversations() const
{
    QList<PurpleConversation *> out;
    for (auto it = m_byConv.begin(); it != m_byConv.end(); ++it)
        if (it.value() > 0)
            out.append(it.key());
    return out;
}

int MessageState::unreadForBuddyName(PurpleAccount *acc, const char *name) const
{
    return m_byKey.value(keyFor(acc, name), 0);
}

bool MessageState::nodeHasUnread(PurpleBlistNode *node) const
{
    if (!node) return false;
    switch (purple_blist_node_get_type(node)) {
    case PURPLE_BLIST_BUDDY_NODE: {
        PurpleBuddy *b = reinterpret_cast<PurpleBuddy *>(node);
        return unreadForBuddyName(purple_buddy_get_account(b),
                                  purple_buddy_get_name(b)) > 0;
    }
    case PURPLE_BLIST_CONTACT_NODE: {
        for (PurpleBlistNode *c = purple_blist_node_get_first_child(node);
             c; c = purple_blist_node_get_sibling_next(c)) {
            if (nodeHasUnread(c)) return true;
        }
        return false;
    }
    case PURPLE_BLIST_CHAT_NODE: {
        PurpleChat *chat = reinterpret_cast<PurpleChat *>(node);
        PurpleAccount *acc = purple_chat_get_account(chat);
        if (!acc) return false;
        // messageReceived() stores unread under the conv's internal name
        // (e.g. "chat-487155921"), while purple_chat_get_name() returns the
        // user-visible alias — they don't necessarily match. Scan the
        // by-key map for this account and ask libpurple to resolve each
        // candidate name back to a PurpleChat; if any resolves to us, we
        // have an unread message.
        QByteArray prefix;
        prefix.append(purple_account_get_protocol_id(acc));
        prefix.append('|');
        prefix.append(purple_account_get_username(acc));
        prefix.append('|');
        for (auto it = m_byKey.cbegin(); it != m_byKey.cend(); ++it) {
            if (it.value() <= 0) continue;
            if (!it.key().startsWith(prefix)) continue;
            QByteArray convName = it.key().mid(prefix.size());
            if (purple_blist_find_chat(acc, convName.constData()) == chat)
                return true;
        }
        return false;
    }
    case PURPLE_BLIST_GROUP_NODE: {
        for (PurpleBlistNode *c = purple_blist_node_get_first_child(node);
             c; c = purple_blist_node_get_sibling_next(c)) {
            if (nodeHasUnread(c)) return true;
        }
        return false;
    }
    default:
        return false;
    }
}

} // namespace konqix
