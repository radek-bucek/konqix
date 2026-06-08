// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Radek Bucek

#pragma once

#include <QObject>
#include <QSystemTrayIcon>

class QMenu;

namespace konqix {

class MainWindow;

class TrayIcon : public QObject
{
    Q_OBJECT
public:
    explicit TrayIcon(MainWindow *window, QObject *parent = nullptr);

    void show();
    QSystemTrayIcon *systemTrayIcon() const { return m_icon; }

private slots:
    void onActivated(QSystemTrayIcon::ActivationReason reason);
    void toggleMainWindow();

private:
    void buildMenu();
    void rebuildStatusActions();

    QSystemTrayIcon *m_icon = nullptr;
    QMenu *m_menu = nullptr;
    MainWindow *m_window = nullptr;
};

} // namespace konqix
