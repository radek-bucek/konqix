// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Radek Bucek

#pragma once

namespace konqix {

// Manages the per-user XDG autostart entry
// (~/.config/autostart/com.konqix.Konqix.desktop) that makes Konqix launch
// automatically at login. Toggled from MainWindow's File menu.
class AutostartManager
{
public:
    static bool isEnabled();
    static void setEnabled(bool enabled);
};

} // namespace konqix
