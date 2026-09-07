// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Radek Bucek

#pragma once

#include <QIcon>
#include <QString>

extern "C" {
#include <libpurple/blist.h>
#include <libpurple/status.h>
}

namespace konqix {

// Map a libpurple status primitive to the matching tray-style SVG icon.
// Shared by the main-window title bar, the status combo box, the buddy
// list (per-buddy status), and the conversation window header.
inline QIcon iconForStatusPrimitive(int prim)
{
    switch (prim) {
        case PURPLE_STATUS_AWAY:          return QIcon(QStringLiteral(":/icons/konqix-away.svg"));
        case PURPLE_STATUS_EXTENDED_AWAY: return QIcon(QStringLiteral(":/icons/konqix-xaway.svg"));
        case PURPLE_STATUS_INVISIBLE:     return QIcon(QStringLiteral(":/icons/konqix-invisible.svg"));
        case PURPLE_STATUS_OFFLINE:       return QIcon(QStringLiteral(":/icons/konqix-offline.svg"));
        default:                          return QIcon(QStringLiteral(":/icons/konqix.svg"));
    }
}

// The primitive behind a buddy's active status, for icon lookup via
// iconForStatusPrimitive() above. Offline buddies have no active status
// object worth inspecting, so that case is resolved explicitly.
inline int statusPrimitiveForBuddy(PurpleBuddy *b)
{
    if (!b || !PURPLE_BUDDY_IS_ONLINE(b))
        return PURPLE_STATUS_OFFLINE;
    PurplePresence *p = purple_buddy_get_presence(b);
    PurpleStatus *s = p ? purple_presence_get_active_status(p) : nullptr;
    PurpleStatusType *t = s ? purple_status_get_type(s) : nullptr;
    return t ? purple_status_type_get_primitive(t) : PURPLE_STATUS_AVAILABLE;
}

} // namespace konqix
