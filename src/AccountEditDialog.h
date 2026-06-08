// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Radek Bucek

#pragma once

#include <QDialog>
#include <QHash>

extern "C" {
#include <libpurple/account.h>
#include <libpurple/accountopt.h>
}

class QComboBox;
class QLineEdit;
class QCheckBox;
class QFormLayout;
class QSpinBox;
class QWidget;

namespace konqix {

class AccountEditDialog : public QDialog
{
    Q_OBJECT
public:
    explicit AccountEditDialog(PurpleAccount *account, QWidget *parent = nullptr);

private slots:
    void protocolChanged();
    void accept() override;

private:
    void rebuildProtocolOptions();
    void rebuildUserSplits();
    void applyPasswordVisibility();
    QString assembleUsername() const;

    PurpleAccount *m_account = nullptr; // null when adding
    QComboBox *m_protocolCombo = nullptr;
    QLineEdit *m_usernameEdit = nullptr;
    QLineEdit *m_passwordEdit = nullptr;
    QLineEdit *m_aliasEdit = nullptr;
    QCheckBox *m_rememberPassword = nullptr;

    QFormLayout *m_basicForm = nullptr;

    QWidget *m_protocolOptionsWidget = nullptr;
    QFormLayout *m_protocolOptionsLayout = nullptr;

    // map setting name -> {QWidget *, PurplePrefType}
    struct OptWidget {
        QWidget *widget;
        PurplePrefType type;
        const char *settingName;
    };
    QList<OptWidget> m_optionWidgets;

    // Protocol-defined splits of the username (e.g. XMPP domain/resource, IRC server)
    struct SplitWidget {
        PurpleAccountUserSplit *split;
        QLineEdit *edit;
    };
    QList<SplitWidget> m_splitWidgets;
};

} // namespace konqix
