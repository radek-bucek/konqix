// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Radek Bucek

#pragma once

#include <QObject>
#include <QPointer>

class QSystemTrayIcon;
class QTimer;
class QIcon;

namespace konqix {

class Notifier : public QObject
{
    Q_OBJECT
public:
    static Notifier *instance();
    explicit Notifier(QObject *parent = nullptr);
    ~Notifier() override;

    void setTrayIcon(QSystemTrayIcon *tray);

    // Trigger configured notifications for an incoming message.
    void notifyIncoming(const QString &who, const QString &message);

    // Reset the tray icon to its normal state (e.g. when user opens conv).
    void stopFlashing();

    // Swap the tray "base" icon to one matching this PurpleStatusPrimitive
    // — called when the user toggles their status combo.
    void setStatusPrimitive(int primitive);

    // Play the configured notification sound — exposed so the
    // settings UI can offer a "Test sound" action.
    void playSound();

private:
    void flashTray();
    QIcon *baseIcon() const;

    QPointer<QSystemTrayIcon> m_tray;
    QTimer *m_flashTimer = nullptr;
    bool m_flashAlt = false;
    int m_status = 2;             // PURPLE_STATUS_AVAILABLE
    QIcon *m_iconAvailable = nullptr;
    QIcon *m_iconAway = nullptr;
    QIcon *m_iconXAway = nullptr;
    QIcon *m_iconInvisible = nullptr;
    QIcon *m_iconOffline = nullptr;
    QIcon *m_iconAlt = nullptr;
};

} // namespace konqix
