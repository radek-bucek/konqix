// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Radek Bucek

#include "PurpleCore.h"

#include "QtEventLoop.h"
#include "BuddyListModel.h"
#include "ConversationManager.h"
#include "RequestDispatcher.h"
#include "NotifyDispatcher.h"

#include <QDir>
#include <QStandardPaths>
#include <QDebug>

#include <cstdio>

extern "C" {
#include <libpurple/purple.h>
}

namespace konqix {

static PurpleCore *g_instance = nullptr;

PurpleCore *PurpleCore::instance()
{
    return g_instance;
}

PurpleCore::PurpleCore(QObject *parent) : QObject(parent)
{
    g_instance = this;
}

PurpleCore::~PurpleCore()
{
    shutdown();
    if (g_instance == this)
        g_instance = nullptr;
}

void PurpleCore::coreUiInit()
{
    purple_blist_set_ui_ops(BuddyListModel::uiOps());
    purple_conversations_set_ui_ops(ConversationManager::uiOps());
    purple_request_set_ui_ops(RequestDispatcher::uiOps());
    purple_notify_set_ui_ops(NotifyDispatcher::uiOps());

    // Clean up legacy pref. coreUiInit runs after purple_prefs_load(), so by
    // now the pref is in the in-memory tree if it was in prefs.xml.
    purple_prefs_remove("/konqix/blist/sort_mode");

    // Mirror /konqix/logging/enabled into libpurple's logging prefs so
    // the bundled txt logger writes IM/chat history automatically.
    bool log = purple_prefs_get_bool("/konqix/logging/enabled");
    purple_prefs_set_bool("/purple/logging/log_ims", log ? TRUE : FALSE);
    purple_prefs_set_bool("/purple/logging/log_chats", log ? TRUE : FALSE);
    // Ensure a logger format is set; default to txt.
    const char *fmt = purple_prefs_get_string("/purple/logging/format");
    if (!fmt || !*fmt)
        purple_prefs_set_string("/purple/logging/format", "txt");
}

static GHashTable *uiInfo()
{
    static GHashTable *info = nullptr;
    if (!info) {
        info = g_hash_table_new(g_str_hash, g_str_equal);
        g_hash_table_insert(info, (gpointer)"name", (gpointer)"Konqix");
        g_hash_table_insert(info, (gpointer)"version", (gpointer)"0.1");
        g_hash_table_insert(info, (gpointer)"website",
                            (gpointer)"https://github.com/radek-bucek/konqix");
        g_hash_table_insert(info, (gpointer)"dev_website",
                            (gpointer)"https://github.com/radek-bucek/konqix");
        g_hash_table_insert(info, (gpointer)"client_type", (gpointer)"pc");
    }
    return info;
}

extern "C" void konqix_core_ui_init(void)
{
    PurpleCore::coreUiInit();
}

// Registered as PurpleCoreUiOps.ui_prefs_init — called *before* prefs.xml is
// loaded, so any defaults we declare here become the seed values.
extern "C" void konqix_prefs_init(void)
{
    purple_prefs_add_none("/konqix");

    purple_prefs_add_none("/konqix/blist");
    purple_prefs_add_bool("/konqix/blist/show_offline", FALSE);
    purple_prefs_add_bool("/konqix/blist/show_away", TRUE);
    // Show the small per-buddy status icon in the buddy list and the
    // peer status icon in IM conversation windows. Toggled from
    // View → Show status icons.
    purple_prefs_add_bool("/konqix/blist/show_status_icons", TRUE);
    // Append a smaller grey "(seen …)" tag next to offline buddies.
    // "off" (default), "approx" ("5m ago" / "3h ago" / "2d ago" / date),
    // or "exact" (HH:MM today, YYYY-MM-DD HH:MM otherwise). Selected
    // via View → Show last seen.
    purple_prefs_add_string("/konqix/blist/last_seen_display", "off");
    // Primary sort key (toggle): if true, status comes first.
    purple_prefs_add_bool("/konqix/blist/sort_by_status", TRUE);
    // Secondary sort key (radio): "name" (default) or "activity".
    purple_prefs_add_string("/konqix/blist/sort_secondary", "name");
    // Drop the old combined pref if it lingers from earlier builds. We don't
    // check exists() because it returns false for prefs loaded from XML but
    // not registered via add_*. remove() is safe on absent keys.
    purple_prefs_remove("/konqix/blist/sort_mode");

    purple_prefs_add_none("/konqix/notify");
    purple_prefs_add_bool("/konqix/notify/sound", TRUE);
    purple_prefs_add_bool("/konqix/notify/flash_tray", TRUE);
    // Popup balloon over the system tray icon ("X sent you Y…").
    purple_prefs_add_bool("/konqix/notify/balloon", TRUE);
    purple_prefs_add_bool("/konqix/notify/open_conv", FALSE);
    // Empty = use built-in chime resource. Otherwise an absolute path to
    // a sound file paplay can decode (.wav / .ogg / .oga / .flac / .mp3).
    purple_prefs_add_string("/konqix/notify/sound_file", "");

    // Spell-check on the conversation input. Honoured only when the
    // optional KF6Sonnet dependency is linked in (HAVE_SONNET).
    purple_prefs_add_none("/konqix/input");
    purple_prefs_add_bool("/konqix/input/spellcheck", TRUE);
    // Empty = autodetect from QLocale (then fall back to en_US). Set
    // via View → Spelling submenu.
    purple_prefs_add_string("/konqix/input/spellcheck_lang", "");

    // Last selected status primitive — restored on launch so the user
    // doesn't always come back online by default.
    purple_prefs_add_none("/konqix/status");
    purple_prefs_add_int("/konqix/status/last_primitive",
                         int(PURPLE_STATUS_AVAILABLE));

    // Message history; defaults to ON. We mirror to libpurple's
    // /purple/logging/log_ims and log_chats from coreUiInit so the built-in
    // logger writes ~/.purple/logs/<proto>/<acct>/<buddy>/*.txt.
    purple_prefs_add_none("/konqix/logging");
    purple_prefs_add_bool("/konqix/logging/enabled", TRUE);

    // Persisted main-window geometry. Stored both as a Qt-serialized blob
    // (base64) so we get Qt's full geometry state where possible, and as
    // separate ints so Wayland sessions — where x/y are clamped to 0 by the
    // compositor — at least keep the size across restarts.
    purple_prefs_add_none("/konqix/window");
    purple_prefs_add_string("/konqix/window/geometry", "");
    purple_prefs_add_int("/konqix/window/width", 0);
    purple_prefs_add_int("/konqix/window/height", 0);

    // Old combined pref from earlier builds, replaced by sort_by_status +
    // sort_secondary. We try to remove it from the in-memory tree here; the
    // actual XML-loaded copy gets cleared again from coreUiInit after load.
    purple_prefs_remove("/konqix/blist/sort_mode");
}

bool PurpleCore::init()
{
    if (m_initialized)
        return true;

    // Use libpurple's standard config dir (~/.purple by default) so accounts,
    // blist and plugin config files (telegram-tdlib-api.conf, etc.) end up
    // in the conventional place. Override with KONQIX_CONFIG_DIR if you
    // need an isolated profile.
    QByteArray override = qgetenv("KONQIX_CONFIG_DIR");
    if (!override.isEmpty()) {
        QDir().mkpath(QString::fromLocal8Bit(override));
        purple_util_set_user_dir(override.constData());
    }

    purple_debug_set_enabled(qEnvironmentVariableIsSet("KONQIX_DEBUG") ? TRUE : FALSE);

    // libpurple automatically probes the system plugin dir
    // (e.g. /usr/lib64/purple-2). We add the per-user plugin path so
    // user-installed protocols (~/.purple/plugins/*) get loaded too.
    {
        char *userPlugins = g_build_filename(purple_user_dir(), "plugins", nullptr);
        g_mkdir_with_parents(userPlugins, 0700);
        purple_plugins_add_search_path(userPlugins);
        g_free(userPlugins);
    }

    // Configure core ops with C-linkage callbacks
    static PurpleCoreUiOps coreOps = {
        konqix_prefs_init,
        nullptr,
        konqix_core_ui_init,
        nullptr,
        uiInfo,
        nullptr, nullptr, nullptr
    };

    purple_core_set_ui_ops(&coreOps);
    purple_eventloop_set_ui_ops(qtEventLoopUiOps());

    if (!purple_core_init(UI_ID)) {
        qCritical() << "Failed to initialize libpurple core";
        return false;
    }

    purple_set_blist(purple_blist_new());
    purple_blist_load();

    purple_prefs_load();
    purple_plugins_load_saved("/purple/konqix/plugins/loaded");
    purple_pounces_load();

    m_initialized = true;
    emit uiInitialized();

    // Restore the last-selected status from our own pref. We can't reuse
    // libpurple's /purple/savedstatus/startup because the combo creates
    // transient saved statuses (no title) that don't survive in status.xml.
    {
        PurpleStatusPrimitive prim = static_cast<PurpleStatusPrimitive>(
            purple_prefs_get_int("/konqix/status/last_primitive"));
        PurpleSavedStatus *st =
            purple_savedstatus_find_transient_by_type_and_message(prim, nullptr);
        if (!st)
            st = purple_savedstatus_new(nullptr, prim);
        purple_savedstatus_activate(st);
    }
    purple_accounts_restore_current_statuses();

    return true;
}

void PurpleCore::shutdown()
{
    if (!m_initialized)
        return;
    purple_core_quit();
    m_initialized = false;
}

} // namespace konqix
