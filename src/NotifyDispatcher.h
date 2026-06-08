// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Radek Bucek

#pragma once

#include <QObject>

extern "C" {
#include <libpurple/notify.h>
}

namespace konqix {

class NotifyDispatcher : public QObject
{
    Q_OBJECT
public:
    static PurpleNotifyUiOps *uiOps();
};

} // namespace konqix
