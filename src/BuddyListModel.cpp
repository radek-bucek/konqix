// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Radek Bucek

#include "BuddyListModel.h"
#include "ConversationManager.h"
#include "MessageState.h"

#include <QBrush>
#include <QColor>
#include <QFileInfo>
#include <QFont>
#include <QIcon>
#include <QTimer>

extern "C" {
#include <libpurple/log.h>
#include <libpurple/notify.h>
#include <libpurple/prpl.h>
}

#include <QHash>
#include <ctime>

namespace konqix {

namespace {

BuddyListModel *g_instance = nullptr;

void blistNewList(PurpleBuddyList *)
{
}

void blistNewNode(PurpleBlistNode *)
{
}

void blistShow(PurpleBuddyList *)
{
    if (g_instance)
        g_instance->rebuild();
}

void blistUpdate(PurpleBuddyList *, PurpleBlistNode *node)
{
    if (g_instance)
        g_instance->nodeUpdated(node);
}

void blistRemove(PurpleBuddyList *, PurpleBlistNode *node)
{
    if (g_instance)
        g_instance->nodeRemoved(node);
}

void blistDestroy(PurpleBuddyList *)
{
}

void blistSetVisible(PurpleBuddyList *, gboolean)
{
}

void blistRequestAddBuddy(PurpleAccount *, const char *, const char *, const char *)
{
}

void blistRequestAddChat(PurpleAccount *, PurpleGroup *, const char *, const char *)
{
}

void blistRequestAddGroup(void)
{
}

PurpleBlistUiOps g_ops = {
    blistNewList,
    blistNewNode,
    blistShow,
    blistUpdate,
    blistRemove,
    blistDestroy,
    blistSetVisible,
    blistRequestAddBuddy,
    blistRequestAddChat,
    blistRequestAddGroup,
    nullptr, // save_node — use default
    nullptr, // remove_node — use default
    nullptr, // save_account — use default
    nullptr
};

} // namespace

BuddyListModel *BuddyListModel::instance()
{
    return g_instance;
}

PurpleBlistUiOps *BuddyListModel::uiOps()
{
    return &g_ops;
}

BuddyListModel::BuddyListModel(QObject *parent) : QAbstractItemModel(parent)
{
    g_instance = this;
    if (MessageState::instance()) {
        connect(MessageState::instance(), &MessageState::unreadChanged,
                this, &BuddyListModel::rebuild);
    }
    if (auto *cm = ConversationManager::instance()) {
        // New conv activity (RECV/SEND/system) refreshes Last-conversation
        // sort order and the unread badge state.
        connect(cm, &ConversationManager::conversationActivity,
                this, [this](PurpleConversation *) { rebuild(); });
    }
}

BuddyListModel::~BuddyListModel()
{
    if (g_instance == this)
        g_instance = nullptr;
}

bool BuddyListModel::nodeVisible(PurpleBlistNode *node) const
{
    if (!node)
        return false;
    // Anything with unread messages stays visible regardless of filters.
    if (MessageState::instance() && MessageState::instance()->nodeHasUnread(node))
        return true;
    switch (purple_blist_node_get_type(node)) {
    case PURPLE_BLIST_GROUP_NODE:
        return true;
    case PURPLE_BLIST_CONTACT_NODE: {
        PurpleContact *contact = reinterpret_cast<PurpleContact *>(node);
        PurpleBuddy *buddy = purple_contact_get_priority_buddy(contact);
        if (!buddy)
            return false;
        return buddyVisible(buddy);
    }
    case PURPLE_BLIST_BUDDY_NODE:
        return buddyVisible(reinterpret_cast<PurpleBuddy *>(node));
    case PURPLE_BLIST_CHAT_NODE:
        return true;
    default:
        return false;
    }
}

bool BuddyListModel::buddyVisible(PurpleBuddy *buddy) const
{
    if (!buddy) return false;
    if (!PURPLE_BUDDY_IS_ONLINE(buddy))
        return m_showOffline;
    PurplePresence *p = purple_buddy_get_presence(buddy);
    bool available = p ? purple_presence_is_available(p) : true;
    if (!available)
        return m_showAway;
    return true;
}

static int statusRank(PurpleBuddy *b)
{
    if (!b) return 99;
    if (!PURPLE_BUDDY_IS_ONLINE(b)) return 30;
    PurplePresence *p = purple_buddy_get_presence(b);
    if (p && !purple_presence_is_available(p)) return 20;
    return 10;
}

static PurpleBuddy *nodeBuddy(PurpleBlistNode *n)
{
    auto t = purple_blist_node_get_type(n);
    if (t == PURPLE_BLIST_BUDDY_NODE)
        return reinterpret_cast<PurpleBuddy *>(n);
    if (t == PURPLE_BLIST_CONTACT_NODE)
        return purple_contact_get_priority_buddy(reinterpret_cast<PurpleContact *>(n));
    return nullptr;
}

static QString nodeSortName(PurpleBlistNode *n)
{
    if (PurpleBuddy *b = nodeBuddy(n)) {
        const char *a = purple_buddy_get_alias(b);
        const char *name = a ? a : purple_buddy_get_name(b);
        return QString::fromUtf8(name ? name : "").toCaseFolded();
    }
    if (purple_blist_node_get_type(n) == PURPLE_BLIST_CHAT_NODE) {
        PurpleChat *c = reinterpret_cast<PurpleChat *>(n);
        return QString::fromUtf8(purple_chat_get_name(c) ? purple_chat_get_name(c) : "")
               .toCaseFolded();
    }
    return {};
}

// Approximate "last activity" timestamp for a contact by stat'ing the log
// directory. The dir's mtime moves whenever a new log file is created or an
// old one updated, so we get recency without iterating the (potentially
// thousands of) log files inside — purple_log_get_logs on a multi-year
// archive would block the UI for seconds and spike CPU.
static int latestLogTime(PurpleLogType type, const char *name, PurpleAccount *acc)
{
    char *dir = purple_log_get_log_dir(type, name, acc);
    if (!dir) return 0;
    QFileInfo info(QString::fromUtf8(dir));
    g_free(dir);
    if (!info.exists())
        return 0;
    return (int)info.lastModified().toSecsSinceEpoch();
}

static PurpleBuddy *nodeAsBuddy(PurpleBlistNode *n)
{
    auto t = purple_blist_node_get_type(n);
    if (t == PURPLE_BLIST_BUDDY_NODE)
        return reinterpret_cast<PurpleBuddy *>(n);
    if (t == PURPLE_BLIST_CONTACT_NODE)
        return purple_contact_get_priority_buddy(reinterpret_cast<PurpleContact *>(n));
    return nullptr;
}

// Last *conversation* activity — when we most recently wrote a log file
// for this contact. Cheap (single stat on the log dir).
static int nodeLastConversation(PurpleBlistNode *n)
{
    if (PurpleBuddy *b = nodeAsBuddy(n)) {
        int latest = latestLogTime(PURPLE_LOG_IM,
            purple_buddy_get_name(b), purple_buddy_get_account(b));
        if (latest > 0) return latest;
        PurplePresence *p = purple_buddy_get_presence(b);
        if (p) {
            time_t tt = purple_presence_get_login_time(p);
            if (tt) return (int)tt;
        }
        return 0;
    }
    if (purple_blist_node_get_type(n) == PURPLE_BLIST_CHAT_NODE) {
        PurpleChat *c = reinterpret_cast<PurpleChat *>(n);
        return latestLogTime(PURPLE_LOG_CHAT,
            purple_chat_get_name(c), purple_chat_get_account(c));
    }
    return 0;
}

// telegram-tdlib doesn't expose its per-user "last online" timestamp through
// either purple_presence_get_login_time or any persistent setting — it only
// writes a human-readable string into the user-info entries when a UI
// calls tgprpl_tooltip_text. We invoke the same callback here, scrape the
// "Last online" entry and parse its value.
static time_t parseLastOnlineLabel(const char *val)
{
    if (!val || !*val) return 0;
    time_t now = time(nullptr);
    if (std::strcmp(val, "now") == 0)        return now;
    if (std::strcmp(val, "recently") == 0)   return now - 60 * 60;
    if (std::strcmp(val, "last week") == 0)  return now - 7  * 24 * 3600;
    if (std::strcmp(val, "last month") == 0) return now - 30 * 24 * 3600;
    // Otherwise it's ctime() output: "Sun May 24 13:08:33 2026\n"
    struct tm tm = {};
    if (strptime(val, "%a %b %d %H:%M:%S %Y", &tm))
        return mktime(&tm);
    return 0;
}

static int lookupLastOnlineViaTooltip(PurpleBuddy *b)
{
    if (!b) return 0;
    PurpleAccount *acc = purple_buddy_get_account(b);
    if (!acc) return 0;
    PurplePlugin *plugin = purple_find_prpl(purple_account_get_protocol_id(acc));
    if (!plugin) return 0;
    PurplePluginProtocolInfo *prpl = PURPLE_PLUGIN_PROTOCOL_INFO(plugin);
    if (!prpl || !prpl->tooltip_text) return 0;

    PurpleNotifyUserInfo *info = purple_notify_user_info_new();
    prpl->tooltip_text(b, info, FALSE);
    time_t result = 0;
    for (GList *l = purple_notify_user_info_get_entries(info); l; l = l->next) {
        auto *e = static_cast<PurpleNotifyUserInfoEntry *>(l->data);
        const char *label = purple_notify_user_info_entry_get_label(e);
        if (!label) continue;
        if (std::strcmp(label, "Last online") == 0) {
            const char *val = purple_notify_user_info_entry_get_value(e);
            result = parseLastOnlineLabel(val);
            break;
        }
    }
    purple_notify_user_info_destroy(info);
    return (int)result;
}

// Cache so the comparator doesn't re-invoke tooltip_text + strptime on every
// pairwise compare. The cache is keyed by PurpleBuddy* and reset whenever
// the model rebuilds (which happens on status changes via MessageState too,
// and on add/remove via blist ui_ops).
static QHash<PurpleBuddy *, int> &lastSeenCache()
{
    static QHash<PurpleBuddy *, int> c;
    return c;
}

// Last *seen* online — protocol-supplied via tooltip_text, falling back to
// presence info for protocols that don't override the callback.
static int nodeLastSeen(PurpleBlistNode *n)
{
    PurpleBuddy *b = nodeAsBuddy(n);
    if (!b) return 0;
    auto &c = lastSeenCache();
    auto it = c.find(b);
    if (it != c.end()) return it.value();
    int v = lookupLastOnlineViaTooltip(b);
    if (v == 0) {
        // Plugins without a tooltip_text — fall back to presence timestamps.
        PurplePresence *p = purple_buddy_get_presence(b);
        if (p) {
            if (purple_presence_is_available(p))
                v = (int)time(nullptr);
            else {
                time_t t = purple_presence_get_login_time(p);
                if (!t) t = purple_presence_get_idle_time(p);
                v = (int)t;
            }
        }
    }
    c.insert(b, v);
    return v;
}

QList<PurpleBlistNode *> BuddyListModel::visibleChildren(PurpleBlistNode *parent) const
{
    QList<PurpleBlistNode *> result;
    PurpleBlistNode *node = parent
                            ? purple_blist_node_get_first_child(parent)
                            : (purple_get_blist() ? purple_get_blist()->root : nullptr);
    for (; node; node = purple_blist_node_get_sibling_next(node)) {
        // Hide groups with no visible descendants
        if (purple_blist_node_get_type(node) == PURPLE_BLIST_GROUP_NODE) {
            bool any = false;
            for (PurpleBlistNode *child = purple_blist_node_get_first_child(node);
                 child; child = purple_blist_node_get_sibling_next(child)) {
                if (nodeVisible(child)) { any = true; break; }
            }
            if (!any) continue;
        }
        if (nodeVisible(node))
            result.append(node);
    }

    // Sort within a non-root parent only; keep top-level groups in xml order.
    if (parent != nullptr) {
        std::stable_sort(result.begin(), result.end(),
            [this](PurpleBlistNode *a, PurpleBlistNode *b) {
                if (m_sortByStatus) {
                    int ra = statusRank(nodeBuddy(a));
                    int rb = statusRank(nodeBuddy(b));
                    if (ra != rb) return ra < rb;
                }
                switch (m_secondarySort) {
                case SecondarySort::Name:
                    return nodeSortName(a) < nodeSortName(b);
                case SecondarySort::ActivityDesc:
                    return nodeLastConversation(a) > nodeLastConversation(b);
                case SecondarySort::LastSeen:
                    return nodeLastSeen(a) > nodeLastSeen(b);
                }
                return false;
            });
    }
    return result;
}

int BuddyListModel::childRow(PurpleBlistNode *node) const
{
    if (!node)
        return -1;
    PurpleBlistNode *parent = purple_blist_node_get_parent(node);
    auto siblings = visibleChildren(parent);
    return siblings.indexOf(node);
}

QModelIndex BuddyListModel::index(int row, int column, const QModelIndex &parent) const
{
    if (column != 0 || row < 0)
        return {};
    PurpleBlistNode *parentNode = parent.isValid()
                                  ? static_cast<PurpleBlistNode *>(parent.internalPointer())
                                  : nullptr;
    auto siblings = visibleChildren(parentNode);
    if (row >= siblings.size())
        return {};
    return createIndex(row, column, siblings.at(row));
}

QModelIndex BuddyListModel::parent(const QModelIndex &child) const
{
    if (!child.isValid())
        return {};
    PurpleBlistNode *node = static_cast<PurpleBlistNode *>(child.internalPointer());
    PurpleBlistNode *parentNode = node ? purple_blist_node_get_parent(node) : nullptr;
    if (!parentNode)
        return {};
    int row = childRow(parentNode);
    if (row < 0)
        return {};
    return createIndex(row, 0, parentNode);
}

int BuddyListModel::rowCount(const QModelIndex &parent) const
{
    if (!purple_get_blist())
        return 0;
    PurpleBlistNode *parentNode = parent.isValid()
                                  ? static_cast<PurpleBlistNode *>(parent.internalPointer())
                                  : nullptr;
    // Buddies and chats are leaf-ish from the view's perspective
    if (parentNode) {
        auto type = purple_blist_node_get_type(parentNode);
        if (type == PURPLE_BLIST_BUDDY_NODE || type == PURPLE_BLIST_CHAT_NODE)
            return 0;
    }
    return visibleChildren(parentNode).size();
}

int BuddyListModel::columnCount(const QModelIndex &) const
{
    return 1;
}

static QString statusText(PurpleBuddy *buddy)
{
    PurplePresence *p = purple_buddy_get_presence(buddy);
    if (!p)
        return {};
    PurpleStatus *s = purple_presence_get_active_status(p);
    if (!s)
        return {};
    const char *name = purple_status_get_name(s);
    return name ? QString::fromUtf8(name) : QString();
}

QVariant BuddyListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return {};
    PurpleBlistNode *node = static_cast<PurpleBlistNode *>(index.internalPointer());
    if (!node)
        return {};

    PurpleBlistNodeType type = purple_blist_node_get_type(node);
    const bool hasUnread = MessageState::instance()
                           && MessageState::instance()->nodeHasUnread(node);

    if (role == Qt::DisplayRole) {
        switch (type) {
        case PURPLE_BLIST_GROUP_NODE: {
            PurpleGroup *g = reinterpret_cast<PurpleGroup *>(node);
            // Both counters via manual walk — libpurple caches them
            // and the cache lags / counts phantoms after remove. For a
            // chat-only group online has no meaning, so we drop it
            // from the header when there are no buddies at all.
            int online = 0, totalBuddies = 0, totalChats = 0;
            auto countBuddy = [&](PurpleBlistNode *bn) {
                if (!PURPLE_BLIST_NODE_IS_BUDDY(bn)) return;
                ++totalBuddies;
                PurpleBuddy *b = reinterpret_cast<PurpleBuddy *>(bn);
                if (PURPLE_BUDDY_IS_ONLINE(b)) ++online;
            };
            for (PurpleBlistNode *cn = reinterpret_cast<PurpleBlistNode *>(g)->child;
                 cn; cn = cn->next) {
                if (PURPLE_BLIST_NODE_IS_CONTACT(cn)) {
                    for (PurpleBlistNode *bn = cn->child; bn; bn = bn->next)
                        countBuddy(bn);
                } else if (PURPLE_BLIST_NODE_IS_BUDDY(cn)) {
                    countBuddy(cn);
                } else if (PURPLE_BLIST_NODE_IS_CHAT(cn)) {
                    ++totalChats;
                }
            }
            const int total = totalBuddies + totalChats;
            const QString name = QString::fromUtf8(purple_group_get_name(g));
            if (totalBuddies == 0)
                return QStringLiteral("%1 (%2)").arg(name).arg(total);
            return QStringLiteral("%1 (%2/%3)").arg(name)
                   .arg(online).arg(total);
        }
        case PURPLE_BLIST_CONTACT_NODE: {
            PurpleContact *c = reinterpret_cast<PurpleContact *>(node);
            PurpleBuddy *b = purple_contact_get_priority_buddy(c);
            const char *alias = purple_contact_get_alias(c);
            QString name = (alias && *alias)
                ? QString::fromUtf8(alias)
                : QString::fromUtf8(b ? purple_buddy_get_name(b)
                                      : "(empty contact)");
            // Show the priority buddy's status here too, so the list stays
            // informative even when contacts are collapsed by default.
            if (b) {
                QString st = statusText(b);
                if (!st.isEmpty() && st != QLatin1String("available"))
                    return QStringLiteral("%1  [%2]").arg(name, st);
            }
            return name;
        }
        case PURPLE_BLIST_BUDDY_NODE: {
            PurpleBuddy *b = reinterpret_cast<PurpleBuddy *>(node);
            const char *alias = purple_buddy_get_alias(b);
            QString name = QString::fromUtf8(alias ? alias : purple_buddy_get_name(b));
            QString st = statusText(b);
            return (!st.isEmpty() && st != QLatin1String("available"))
                ? QStringLiteral("%1  [%2]").arg(name, st)
                : name;
        }
        case PURPLE_BLIST_CHAT_NODE: {
            PurpleChat *c = reinterpret_cast<PurpleChat *>(node);
            return QString::fromUtf8(purple_chat_get_name(c));
        }
        default:
            return {};
        }
    }

    if (role == Qt::DecorationRole && hasUnread) {
        // Speech-bubble attention icon next to the name signals new messages
        // without changing the text colour.
        static const QIcon attnIcon(QStringLiteral(":/icons/konqix-attention.svg"));
        return attnIcon;
    }

    if (role == Qt::ForegroundRole) {
        PurpleBuddy *b = nullptr;
        if (type == PURPLE_BLIST_BUDDY_NODE)
            b = reinterpret_cast<PurpleBuddy *>(node);
        else if (type == PURPLE_BLIST_CONTACT_NODE)
            b = purple_contact_get_priority_buddy(
                reinterpret_cast<PurpleContact *>(node));
        if (b) {
            if (!PURPLE_BUDDY_IS_ONLINE(b))
                return QBrush(QColor(140, 140, 140));
            PurplePresence *p = purple_buddy_get_presence(b);
            if (p && purple_presence_is_idle(p))
                return QBrush(QColor(160, 120, 90));
        }
    }

    if (role == Qt::FontRole) {
        if (type == PURPLE_BLIST_GROUP_NODE || hasUnread) {
            QFont f;
            f.setBold(true);
            return f;
        }
    }

    return {};
}

QVariant BuddyListModel::headerData(int, Qt::Orientation, int) const
{
    return {};
}

PurpleBlistNode *BuddyListModel::nodeFromIndex(const QModelIndex &index) const
{
    return index.isValid() ? static_cast<PurpleBlistNode *>(index.internalPointer())
                           : nullptr;
}

QModelIndex BuddyListModel::indexFromNode(PurpleBlistNode *node) const
{
    if (!node)
        return {};
    int row = childRow(node);
    if (row < 0)
        return {};
    return createIndex(row, 0, node);
}

void BuddyListModel::setShowOffline(bool show)
{
    if (m_showOffline == show)
        return;
    m_showOffline = show;
    rebuild();
}

void BuddyListModel::setShowAway(bool show)
{
    if (m_showAway == show)
        return;
    m_showAway = show;
    rebuild();
}

void BuddyListModel::setSortByStatus(bool on)
{
    if (m_sortByStatus == on)
        return;
    m_sortByStatus = on;
    rebuild();
}

void BuddyListModel::setSecondarySort(SecondarySort mode)
{
    if (m_secondarySort == mode)
        return;
    m_secondarySort = mode;
    rebuild();
}

BuddyListModel::SecondarySort BuddyListModel::parseSecondarySort(const QString &s)
{
    if (s == QLatin1String("activity"))  return SecondarySort::ActivityDesc;
    if (s == QLatin1String("last_seen")) return SecondarySort::LastSeen;
    return SecondarySort::Name;
}

QString BuddyListModel::secondarySortToString(SecondarySort m)
{
    switch (m) {
    case SecondarySort::ActivityDesc: return QStringLiteral("activity");
    case SecondarySort::LastSeen:     return QStringLiteral("last_seen");
    case SecondarySort::Name:         break;
    }
    return QStringLiteral("name");
}

void BuddyListModel::rebuild()
{
    lastSeenCache().clear();
    beginResetModel();
    endResetModel();
    emit modelChanged();
}

void BuddyListModel::nodeUpdated(PurpleBlistNode *node)
{
    Q_UNUSED(node);
    static bool pending = false;
    if (pending) return;
    pending = true;
    QTimer::singleShot(0, this, [this]() {
        pending = false;
        rebuild();
    });
}

void BuddyListModel::nodeRemoved(PurpleBlistNode *)
{
    rebuild();
}

} // namespace konqix
