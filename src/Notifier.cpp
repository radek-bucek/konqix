// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Radek Bucek

#include "Notifier.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QProcess>
#include <QStandardPaths>
#include <QStyle>
#include <QSystemTrayIcon>
#include <QTimer>

extern "C" {
#include <libpurple/prefs.h>
}

namespace konqix {

namespace {
Notifier *g_instance = nullptr;
constexpr int FLASH_INTERVAL_MS = 500;
} // namespace

Notifier *Notifier::instance() { return g_instance; }

Notifier::Notifier(QObject *parent) : QObject(parent)
{
    g_instance = this;
    m_iconAvailable = new QIcon(QStringLiteral(":/icons/konqix.svg"));
    m_iconAway      = new QIcon(QStringLiteral(":/icons/konqix-away.svg"));
    m_iconXAway     = new QIcon(QStringLiteral(":/icons/konqix-xaway.svg"));
    m_iconInvisible = new QIcon(QStringLiteral(":/icons/konqix-invisible.svg"));
    m_iconOffline   = new QIcon(QStringLiteral(":/icons/konqix-offline.svg"));
    m_iconAlt       = new QIcon(QStringLiteral(":/icons/konqix-attention.svg"));
}

Notifier::~Notifier()
{
    delete m_iconAvailable;
    delete m_iconAway;
    delete m_iconXAway;
    delete m_iconInvisible;
    delete m_iconOffline;
    delete m_iconAlt;
    if (g_instance == this)
        g_instance = nullptr;
}

QIcon *Notifier::baseIcon() const
{
    // Map libpurple primitives: AVAILABLE=2, AWAY=5, EXTENDED_AWAY=6,
    // INVISIBLE=4, OFFLINE=1.
    switch (m_status) {
        case 5: return m_iconAway;
        case 6: return m_iconXAway;
        case 4: return m_iconInvisible;
        case 1: return m_iconOffline;
        default: return m_iconAvailable;
    }
}

void Notifier::setStatusPrimitive(int primitive)
{
    m_status = primitive;
    if (!m_tray) return;
    if (m_flashTimer && m_flashTimer->isActive())
        return;     // mid-flash; new base picked up on next stopFlashing
    m_tray->setIcon(*baseIcon());
}

void Notifier::setTrayIcon(QSystemTrayIcon *tray)
{
    m_tray = tray;
}

void Notifier::notifyIncoming(const QString &who, const QString &message)
{
    if (purple_prefs_get_bool("/konqix/notify/sound"))
        playSound();
    if (purple_prefs_get_bool("/konqix/notify/flash_tray"))
        flashTray();

    if (m_tray && QSystemTrayIcon::supportsMessages()
        && purple_prefs_get_bool("/konqix/notify/balloon")) {
        QString preview = message;
        if (preview.length() > 120)
            preview = preview.left(117) + QStringLiteral("…");
        m_tray->showMessage(who, preview, QSystemTrayIcon::Information, 5000);
    }
}

void Notifier::playSound()
{
    // User-supplied sound file wins over the built-in chime.
    QString custom = QString::fromUtf8(
        purple_prefs_get_string("/konqix/notify/sound_file"));
    if (!custom.isEmpty() && QFileInfo::exists(custom)) {
        if (QProcess::startDetached(QStringLiteral("paplay"), {custom}))
            return;
    }

    // Built-in bell-like chime — extracted once per session from Qt
    // resources to a tmp file (paplay can't read qrc:// paths). The
    // file is regenerated on first play; subsequent plays reuse it.
    static QString cachedFile;
    if (cachedFile.isEmpty()) {
        QString dir = QStandardPaths::writableLocation(
                          QStandardPaths::TempLocation);
        QDir().mkpath(dir);
        QString out = dir + QStringLiteral("/konqix-chime.wav");
        QFile src(QStringLiteral(":/sounds/chime.wav"));
        if (src.open(QIODevice::ReadOnly)) {
            QFile dst(out);
            if (dst.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                dst.write(src.readAll());
                dst.close();
                cachedFile = out;
            }
            src.close();
        }
    }
    if (!cachedFile.isEmpty() &&
        QProcess::startDetached(QStringLiteral("paplay"), {cachedFile}))
        return;

    // Fallback: freedesktop sound theme via paplay → canberra.
    static const QStringList fallbacks = {
        QStringLiteral("/usr/share/sounds/freedesktop/stereo/message-new-instant.oga"),
        QStringLiteral("/usr/share/sounds/Oxygen-Im-Message-In.ogg"),
        QStringLiteral("/usr/share/sounds/KDE-Im-Message-In.ogg"),
    };
    for (const QString &c : fallbacks) {
        if (QFileInfo::exists(c)) {
            if (QProcess::startDetached(QStringLiteral("paplay"), {c}))
                return;
        }
    }
    QProcess::startDetached(QStringLiteral("canberra-gtk-play"),
                            {QStringLiteral("-i"),
                             QStringLiteral("message-new-instant")});
}

void Notifier::flashTray()
{
    if (!m_tray)
        return;
    if (!m_flashTimer) {
        m_flashTimer = new QTimer(this);
        m_flashTimer->setInterval(FLASH_INTERVAL_MS);
        connect(m_flashTimer, &QTimer::timeout, this, [this]() {
            if (!m_tray) return;
            m_flashAlt = !m_flashAlt;
            m_tray->setIcon(m_flashAlt ? *m_iconAlt : *baseIcon());
        });
    }
    m_flashAlt = true;
    m_tray->setIcon(*m_iconAlt);
    m_flashTimer->start();
    // No auto-stop — the blink persists until the user actually opens the
    // affected conv window, at which point ConversationWindow::markRead()
    // drains MessageState and calls stopFlashing once nothing remains
    // unread.
}

void Notifier::stopFlashing()
{
    if (m_flashTimer && m_flashTimer->isActive())
        m_flashTimer->stop();
    if (m_tray)
        m_tray->setIcon(*baseIcon());
    m_flashAlt = false;
}

} // namespace konqix
