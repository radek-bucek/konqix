// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Radek Bucek

#pragma once

#include <QDialog>

extern "C" {
#include <libpurple/account.h>
}

class QListWidget;

namespace konqix {

class AccountsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit AccountsDialog(QWidget *parent = nullptr);

private slots:
    void refresh();
    void addAccount();
    void editAccount();
    void removeAccount();
    void toggleEnabled();

private:
    PurpleAccount *currentAccount() const;

    QListWidget *m_list = nullptr;
};

} // namespace konqix
