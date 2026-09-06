// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Radek Bucek

#pragma once

#include <QIcon>
#include <QString>

extern "C" {
#include <libpurple/status.h>
}

namespace konqix {

// Map a libpurple status primitive to the matching tray-style SVG icon.
// Shared by the main-window title bar, the status combo box, and the
// buddy list (per-buddy status).
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

} // namespace konqix
