// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Radek Bucek

#include "AccountsDialog.h"
#include "AccountEditDialog.h"
#include "PurpleCore.h"

#include <QHBoxLayout>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

extern "C" {
#include <libpurple/account.h>
#include <libpurple/prpl.h>
}

namespace konqix {

AccountsDialog::AccountsDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("Accounts"));
    resize(520, 360);

    auto *layout = new QHBoxLayout(this);

    m_list = new QListWidget(this);
    layout->addWidget(m_list, 1);

    auto *buttons = new QVBoxLayout;
    auto *addBtn = new QPushButton(tr("Add…"), this);
    auto *editBtn = new QPushButton(tr("Edit…"), this);
    auto *removeBtn = new QPushButton(tr("Remove"), this);
    auto *toggleBtn = new QPushButton(tr("Enable/Disable"), this);
    auto *closeBtn = new QPushButton(tr("Close"), this);
    buttons->addWidget(addBtn);
    buttons->addWidget(editBtn);
    buttons->addWidget(removeBtn);
    buttons->addWidget(toggleBtn);
    buttons->addStretch();
    buttons->addWidget(closeBtn);
    layout->addLayout(buttons);

    connect(addBtn, &QPushButton::clicked, this, &AccountsDialog::addAccount);
    connect(editBtn, &QPushButton::clicked, this, &AccountsDialog::editAccount);
    connect(removeBtn, &QPushButton::clicked, this, &AccountsDialog::removeAccount);
    connect(toggleBtn, &QPushButton::clicked, this, &AccountsDialog::toggleEnabled);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_list, &QListWidget::itemDoubleClicked, this, &AccountsDialog::editAccount);

    refresh();
}

void AccountsDialog::refresh()
{
    m_list->clear();
    for (GList *l = purple_accounts_get_all(); l; l = l->next) {
        auto *acc = static_cast<PurpleAccount *>(l->data);
        const char *username = purple_account_get_username(acc);
        const char *protocol = purple_account_get_protocol_name(acc);
        bool enabled = purple_account_get_enabled(acc, PurpleCore::UI_ID);
        QString text = QStringLiteral("%1 [%2] %3")
                       .arg(QString::fromUtf8(username ? username : ""),
                            QString::fromUtf8(protocol ? protocol : ""),
                            enabled ? QStringLiteral("✓") : QString());
        auto *item = new QListWidgetItem(text, m_list);
        item->setData(Qt::UserRole, QVariant::fromValue<void *>(acc));
    }
}

PurpleAccount *AccountsDialog::currentAccount() const
{
    auto *item = m_list->currentItem();
    if (!item) return nullptr;
    return static_cast<PurpleAccount *>(item->data(Qt::UserRole).value<void *>());
}

void AccountsDialog::addAccount()
{
    AccountEditDialog dlg(nullptr, this);
    if (dlg.exec() == QDialog::Accepted) {
        refresh();
    }
}

void AccountsDialog::editAccount()
{
    PurpleAccount *acc = currentAccount();
    if (!acc) return;
    AccountEditDialog dlg(acc, this);
    if (dlg.exec() == QDialog::Accepted) {
        refresh();
    }
}

void AccountsDialog::removeAccount()
{
    PurpleAccount *acc = currentAccount();
    if (!acc) return;

    const char *username = purple_account_get_username(acc);
    if (QMessageBox::question(this, tr("Remove Account"),
            tr("Really remove account %1?").arg(QString::fromUtf8(username ? username : "")))
        != QMessageBox::Yes)
        return;

    purple_account_set_enabled(acc, PurpleCore::UI_ID, FALSE);
    purple_accounts_delete(acc);
    refresh();
}

void AccountsDialog::toggleEnabled()
{
    PurpleAccount *acc = currentAccount();
    if (!acc) return;
    bool enabled = purple_account_get_enabled(acc, PurpleCore::UI_ID);
    purple_account_set_enabled(acc, PurpleCore::UI_ID, enabled ? FALSE : TRUE);
    refresh();
}

} // namespace konqix
