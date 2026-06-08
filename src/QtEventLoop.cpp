// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Radek Bucek

#include "QtEventLoop.h"

#include <glib.h>

namespace konqix {

namespace {

#define KONQIX_READ_COND  (G_IO_IN | G_IO_HUP | G_IO_ERR)
#define KONQIX_WRITE_COND (G_IO_OUT | G_IO_HUP | G_IO_ERR | G_IO_NVAL)

struct IoClosure {
    PurpleInputFunction function;
    guint result;
    gpointer data;
};

void ioDestroy(gpointer data)
{
    g_free(data);
}

gboolean ioInvoke(GIOChannel *source, GIOCondition condition, gpointer data)
{
    IoClosure *closure = static_cast<IoClosure *>(data);
    PurpleInputCondition purpleCond = static_cast<PurpleInputCondition>(0);

    if (condition & KONQIX_READ_COND)
        purpleCond = static_cast<PurpleInputCondition>(purpleCond | PURPLE_INPUT_READ);
    if (condition & KONQIX_WRITE_COND)
        purpleCond = static_cast<PurpleInputCondition>(purpleCond | PURPLE_INPUT_WRITE);

    closure->function(closure->data, g_io_channel_unix_get_fd(source), purpleCond);
    return TRUE;
}

guint inputAdd(gint fd, PurpleInputCondition condition,
               PurpleInputFunction function, gpointer data)
{
    IoClosure *closure = g_new0(IoClosure, 1);
    closure->function = function;
    closure->data = data;

    GIOCondition cond = static_cast<GIOCondition>(0);
    if (condition & PURPLE_INPUT_READ)
        cond = static_cast<GIOCondition>(cond | KONQIX_READ_COND);
    if (condition & PURPLE_INPUT_WRITE)
        cond = static_cast<GIOCondition>(cond | KONQIX_WRITE_COND);

    GIOChannel *channel = g_io_channel_unix_new(fd);
    closure->result = g_io_add_watch_full(channel, G_PRIORITY_DEFAULT, cond,
                                          ioInvoke, closure, ioDestroy);
    g_io_channel_unref(channel);
    return closure->result;
}

PurpleEventLoopUiOps g_ops = {
    g_timeout_add,
    g_source_remove,
    inputAdd,
    g_source_remove,
    nullptr,
    g_timeout_add_seconds,
    nullptr, nullptr, nullptr
};

} // namespace

PurpleEventLoopUiOps *qtEventLoopUiOps()
{
    return &g_ops;
}

} // namespace konqix
