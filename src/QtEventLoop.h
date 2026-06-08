// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Radek Bucek

#pragma once

#include <glib.h>

extern "C" {
#include <libpurple/eventloop.h>
}

namespace konqix {

PurpleEventLoopUiOps *qtEventLoopUiOps();

}
