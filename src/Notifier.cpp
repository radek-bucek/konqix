// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Radek Bucek

#include "Notifier.h"

#include "MessageState.h"

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
// How long after startup we suppress per-message alerts, in ms. Tuned
// to cover the typical prpl replay burst (Telegram tdlib in particular
// can take a good half-minute to catch up on a busy account).
constexpr int STARTUP_QUIET_MS = 30 * 1000;
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
    QTimer::singleShot(STARTUP_QUIET_MS, this, &Notifier::endStartupQuiet);
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
    // Startup silence: absorb the initial replay burst so the user isn't
    // buried in per-message pings for stuff that already happened on
    // another device. We only count here; endStartupQuiet() surfaces a
    // single summary balloon (and flips the tray to attention if any
    // conversation ended up unread).
    if (m_startupQuiet) {
        ++m_startupMissed;
        return;
    }

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

void Notifier::endStartupQuiet()
{
    m_startupQuiet = false;

    const int missed = m_startupMissed;
    m_startupMissed = 0;

    // If any conversation is still unread after the replay settled,
    // flip the tray to the attention icon so the user has a persistent
    // visual cue matching the per-buddy indicators in the list.
    if (MessageState::instance() && MessageState::instance()->totalUnread() > 0
        && purple_prefs_get_bool("/konqix/notify/flash_tray")) {
        flashTray();
    }

    if (missed > 0 && m_tray && QSystemTrayIcon::supportsMessages()
        && purple_prefs_get_bool("/konqix/notify/balloon")) {
        m_tray->showMessage(
            tr("konqix"),
            tr("%n new message(s) received while starting up.", "", missed),
            QSystemTrayIcon::Information, 5000);
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
