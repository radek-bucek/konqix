// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Radek Bucek

#pragma once

#include <QObject>

extern "C" {
#include <libpurple/request.h>
}

namespace konqix {

class RequestDispatcher : public QObject
{
    Q_OBJECT
public:
    static PurpleRequestUiOps *uiOps();
};

} // namespace konqix
