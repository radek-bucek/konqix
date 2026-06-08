// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Radek Bucek

#pragma once

#include <QObject>

extern "C" {
#include <libpurple/purple.h>
}

namespace konqix {

class PurpleCore : public QObject
{
    Q_OBJECT
public:
    static PurpleCore *instance();
    explicit PurpleCore(QObject *parent = nullptr);
    ~PurpleCore() override;

    bool init();
    void shutdown();

    static constexpr const char *UI_ID = "konqix";

    static void coreUiInit();

signals:
    void uiInitialized();

private:
    bool m_initialized = false;
};

} // namespace konqix
