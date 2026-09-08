// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Radek Bucek

#include "AutostartManager.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTextStream>

namespace konqix {

static QString autostartFilePath()
{
    const QString dir = QStandardPaths::writableLocation(
                             QStandardPaths::GenericConfigLocation)
                         + QStringLiteral("/autostart");
    return dir + QStringLiteral("/com.konqix.Konqix.desktop");
}

bool AutostartManager::isEnabled()
{
    QFile file(autostartFilePath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;

    // Desktop environments that let users disable individual autostart
    // entries from their own settings UI (e.g. GNOME's Startup Applications)
    // do so by adding this key rather than deleting the file, so honour it
    // if present instead of only checking for the file's existence.
    while (!file.atEnd()) {
        if (file.readLine().trimmed() == "X-GNOME-Autostart-enabled=false")
            return false;
    }
    return true;
}

void AutostartManager::setEnabled(bool enabled)
{
    const QString path = autostartFilePath();
    if (!enabled) {
        QFile::remove(path);
        return;
    }

    QDir().mkpath(QFileInfo(path).absolutePath());

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return;

    QTextStream out(&file);
    out << "[Desktop Entry]\n"
           "Type=Application\n"
           "Name=Konqix\n"
           "Comment=Qt 6 instant messenger built on libpurple\n"
           // --minimized skips the initial window.show() so login doesn't
           // pop the buddy list open; the tray icon still appears normally.
           "Exec=konqix --minimized\n"
           "Icon=konqix\n"
           "Terminal=false\n"
           "X-GNOME-Autostart-enabled=true\n"
           "Hidden=false\n";
}

} // namespace konqix
