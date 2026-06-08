// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Radek Bucek

#include "AccountEditDialog.h"
#include "PurpleCore.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QScrollArea>
#include <QSpinBox>
#include <QVBoxLayout>

extern "C" {
#include <libpurple/account.h>
#include <libpurple/accountopt.h>
#include <libpurple/plugin.h>
#include <libpurple/prpl.h>
}

namespace konqix {

AccountEditDialog::AccountEditDialog(PurpleAccount *account, QWidget *parent)
    : QDialog(parent), m_account(account)
{
    setWindowTitle(account ? tr("Edit Account") : tr("Add Account"));
    resize(520, 600);

    auto *layout = new QVBoxLayout(this);

    auto *basic = new QGroupBox(tr("Basic"), this);
    m_basicForm = new QFormLayout(basic);

    m_protocolCombo = new QComboBox(basic);
    GList *protocols = purple_plugins_get_protocols();
    for (GList *l = protocols; l; l = l->next) {
        auto *plug = static_cast<PurplePlugin *>(l->data);
        QString name = QString::fromUtf8(purple_plugin_get_name(plug));
        QString id = QString::fromUtf8(purple_plugin_get_id(plug));
        m_protocolCombo->addItem(name, id);
    }
    m_basicForm->addRow(tr("Protocol:"), m_protocolCombo);

    m_usernameEdit = new QLineEdit(basic);
    m_basicForm->addRow(tr("Username:"), m_usernameEdit);

    // User-split rows (protocol-specific, e.g. Domain/Resource/Server) are
    // inserted between Username and Password by rebuildUserSplits().

    m_passwordEdit = new QLineEdit(basic);
    m_passwordEdit->setEchoMode(QLineEdit::Password);
    m_basicForm->addRow(tr("Password:"), m_passwordEdit);

    m_aliasEdit = new QLineEdit(basic);
    m_basicForm->addRow(tr("Local alias:"), m_aliasEdit);

    m_rememberPassword = new QCheckBox(tr("Remember password"), basic);
    m_basicForm->addRow(QString(), m_rememberPassword);

    layout->addWidget(basic);

    auto *advBox = new QGroupBox(tr("Protocol options"), this);
    auto *advBoxLayout = new QVBoxLayout(advBox);
    auto *scroll = new QScrollArea(advBox);
    scroll->setWidgetResizable(true);
    m_protocolOptionsWidget = new QWidget(scroll);
    m_protocolOptionsLayout = new QFormLayout(m_protocolOptionsWidget);
    scroll->setWidget(m_protocolOptionsWidget);
    advBoxLayout->addWidget(scroll);
    layout->addWidget(advBox, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    connect(m_protocolCombo, &QComboBox::currentIndexChanged,
            this, &AccountEditDialog::protocolChanged);

    if (m_account) {
        const char *protoId = purple_account_get_protocol_id(m_account);
        if (protoId) {
            int idx = m_protocolCombo->findData(QString::fromUtf8(protoId));
            if (idx >= 0)
                m_protocolCombo->setCurrentIndex(idx);
        }
        // Note: rebuildUserSplits() parses the stored username and sets
        // the base username and split-field values, so we don't pre-fill
        // m_usernameEdit here from the full address.
        const char *alias = purple_account_get_alias(m_account);
        if (alias) m_aliasEdit->setText(QString::fromUtf8(alias));
        const char *pw = purple_account_get_password(m_account);
        if (pw) m_passwordEdit->setText(QString::fromUtf8(pw));
        m_rememberPassword->setChecked(purple_account_get_remember_password(m_account));
    } else {
        m_rememberPassword->setChecked(true);
    }

    rebuildUserSplits();
    rebuildProtocolOptions();
    applyPasswordVisibility();
}

void AccountEditDialog::protocolChanged()
{
    rebuildUserSplits();
    rebuildProtocolOptions();
    applyPasswordVisibility();
}

void AccountEditDialog::applyPasswordVisibility()
{
    // Some prpls authenticate without an account password (Telegram tdlib
    // uses a phone code; Bonjour is link-local; etc.) and advertise this
    // via OPT_PROTO_NO_PASSWORD. Hide the password + remember-password
    // rows entirely in that case so they don't confuse the user.
    bool needsPassword = true;
    QString protoId = m_protocolCombo->currentData().toString();
    if (!protoId.isEmpty()) {
        if (PurplePlugin *plugin = purple_find_prpl(protoId.toUtf8().constData())) {
            if (PurplePluginProtocolInfo *prpl = PURPLE_PLUGIN_PROTOCOL_INFO(plugin)) {
                if (prpl->options & OPT_PROTO_NO_PASSWORD)
                    needsPassword = false;
            }
        }
    }

    int pwRow = -1;
    QFormLayout::ItemRole role;
    m_basicForm->getWidgetPosition(m_passwordEdit, &pwRow, &role);
    if (pwRow >= 0)
        m_basicForm->setRowVisible(pwRow, needsPassword);

    int rememberRow = -1;
    m_basicForm->getWidgetPosition(m_rememberPassword, &rememberRow, &role);
    if (rememberRow >= 0)
        m_basicForm->setRowVisible(rememberRow, needsPassword);
}

void AccountEditDialog::rebuildUserSplits()
{
    // Tear down existing split widgets
    for (const auto &sw : m_splitWidgets)
        m_basicForm->removeRow(sw.edit); // also deletes the label
    m_splitWidgets.clear();

    // Reset username placeholder
    m_usernameEdit->setPlaceholderText({});

    QString protoId = m_protocolCombo->currentData().toString();
    if (protoId.isEmpty()) return;

    PurplePlugin *plugin = purple_find_prpl(protoId.toUtf8().constData());
    if (!plugin) return;
    PurplePluginProtocolInfo *prpl = PURPLE_PLUGIN_PROTOCOL_INFO(plugin);
    if (!prpl) return;

    // Protocols that don't have user_splits but use a non-obvious username
    // format (e.g. Telegram → phone number) can supply a hint via
    // get_account_text_table()["login_label"]. Use it as the placeholder.
    if (PURPLE_PROTOCOL_PLUGIN_HAS_FUNC(prpl, get_account_text_table)) {
        GHashTable *table = prpl->get_account_text_table(nullptr);
        if (table) {
            const char *label = static_cast<const char *>(
                g_hash_table_lookup(table, (gpointer)"login_label"));
            if (label && *label)
                m_usernameEdit->setPlaceholderText(QString::fromUtf8(label));
            g_hash_table_destroy(table);
        }
    }

    GList *splits = prpl->user_splits;
    int splitCount = splits ? (int)g_list_length(splits) : 0;
    QVector<QString> values(splitCount);

    // For existing accounts, parse the stored username right-to-left
    // (last split first) — matches libpurple's documented behaviour.
    QByteArray base;
    if (m_account) {
        const char *fullUsername = purple_account_get_username(m_account);
        if (fullUsername) base = QByteArray(fullUsername);
    }

    if (!base.isEmpty() && splitCount > 0) {
        char *work = g_strdup(base.constData());
        int idx = splitCount - 1;
        for (GList *l = g_list_last(splits); l && idx >= 0; l = l->prev, --idx) {
            auto *split = static_cast<PurpleAccountUserSplit *>(l->data);
            char sep = purple_account_user_split_get_separator(split);
            char *c = purple_account_user_split_get_reverse(split)
                      ? strrchr(work, sep)
                      : strchr(work, sep);
            if (c) {
                *c = '\0';
                values[idx] = QString::fromUtf8(c + 1);
            }
        }
        m_usernameEdit->setText(QString::fromUtf8(work));
        g_free(work);
    } else if (!base.isEmpty()) {
        m_usernameEdit->setText(QString::fromUtf8(base));
    }
    // For new accounts (no m_account) we leave m_usernameEdit alone.

    // Username is at row 1 (Protocol=0); splits go right after.
    int insertAt = 2;
    int idx = 0;
    for (GList *l = splits; l; l = l->next, ++idx, ++insertAt) {
        auto *split = static_cast<PurpleAccountUserSplit *>(l->data);
        QString label = QString::fromUtf8(purple_account_user_split_get_text(split));
        auto *edit = new QLineEdit;
        QString value = values.value(idx);
        if (value.isEmpty()) {
            const char *def = purple_account_user_split_get_default_value(split);
            if (def) value = QString::fromUtf8(def);
        }
        edit->setText(value);
        m_basicForm->insertRow(insertAt, label + QStringLiteral(":"), edit);
        m_splitWidgets.append({split, edit});
    }
}

QString AccountEditDialog::assembleUsername() const
{
    QByteArray result = m_usernameEdit->text().toUtf8();
    for (const auto &sw : m_splitWidgets) {
        char sep = purple_account_user_split_get_separator(sw.split);
        QByteArray value = sw.edit->text().toUtf8();
        if (value.isEmpty()) {
            const char *def = purple_account_user_split_get_default_value(sw.split);
            if (def) value = QByteArray(def);
        }
        result.append(sep);
        result.append(value);
    }
    return QString::fromUtf8(result);
}

void AccountEditDialog::rebuildProtocolOptions()
{
    while (m_protocolOptionsLayout->rowCount() > 0)
        m_protocolOptionsLayout->removeRow(0);
    m_optionWidgets.clear();

    QString protoId = m_protocolCombo->currentData().toString();
    if (protoId.isEmpty()) return;

    PurplePlugin *plugin = purple_find_prpl(protoId.toUtf8().constData());
    if (!plugin) return;
    PurplePluginProtocolInfo *prpl = PURPLE_PLUGIN_PROTOCOL_INFO(plugin);
    if (!prpl) return;

    for (GList *l = prpl->protocol_options; l; l = l->next) {
        auto *opt = static_cast<PurpleAccountOption *>(l->data);
        QString text = QString::fromUtf8(purple_account_option_get_text(opt));
        const char *setting = purple_account_option_get_setting(opt);
        if (!setting) continue;
        PurplePrefType type = purple_account_option_get_type(opt);
        QWidget *widget = nullptr;

        switch (type) {
        case PURPLE_PREF_BOOLEAN: {
            auto *cb = new QCheckBox(m_protocolOptionsWidget);
            bool def = purple_account_option_get_default_bool(opt);
            bool val = m_account
                       ? purple_account_get_bool(m_account, setting, def)
                       : def;
            cb->setChecked(val);
            widget = cb;
            m_protocolOptionsLayout->addRow(text, cb);
            break;
        }
        case PURPLE_PREF_INT: {
            auto *sb = new QSpinBox(m_protocolOptionsWidget);
            sb->setRange(-1000000, 1000000);
            int def = purple_account_option_get_default_int(opt);
            int val = m_account
                      ? purple_account_get_int(m_account, setting, def)
                      : def;
            sb->setValue(val);
            widget = sb;
            m_protocolOptionsLayout->addRow(text, sb);
            break;
        }
        case PURPLE_PREF_STRING: {
            auto *le = new QLineEdit(m_protocolOptionsWidget);
            const char *def = purple_account_option_get_default_string(opt);
            const char *val = m_account
                              ? purple_account_get_string(m_account, setting, def)
                              : def;
            if (val) le->setText(QString::fromUtf8(val));
            if (purple_account_option_get_masked(opt))
                le->setEchoMode(QLineEdit::Password);
            widget = le;
            m_protocolOptionsLayout->addRow(text, le);
            break;
        }
        case PURPLE_PREF_STRING_LIST: {
            auto *combo = new QComboBox(m_protocolOptionsWidget);
            const char *def = purple_account_option_get_default_list_value(opt);
            QString currentValue;
            if (m_account) {
                currentValue = QString::fromUtf8(
                    purple_account_get_string(m_account, setting, def ? def : ""));
            } else if (def) {
                currentValue = QString::fromUtf8(def);
            }
            // New-account UX nudge: telegram-tdlib defaults "File downloads"
            // to "discard" (just throws away the file). We prefer the
            // "Inline (hyperlinks in chat)" mode — received files appear
            // as a clickable hyperlink that opens locally — so the user
            // doesn't have to remember to flip this option after creating
            // each Telegram account.
            if (!m_account
                && QByteArray(setting) == "download-behaviour"
                && m_protocolCombo->currentData().toString()
                       == QStringLiteral("telegram-tdlib")) {
                currentValue = QStringLiteral("hyperlink");
            }
            for (GList *item = purple_account_option_get_list(opt); item; item = item->next) {
                auto *kvp = static_cast<PurpleKeyValuePair *>(item->data);
                if (!kvp) continue;
                QString label = QString::fromUtf8(kvp->key ? kvp->key : "");
                QString val = QString::fromUtf8(static_cast<const char *>(kvp->value));
                combo->addItem(label, val);
                if (val == currentValue)
                    combo->setCurrentIndex(combo->count() - 1);
            }
            widget = combo;
            m_protocolOptionsLayout->addRow(text, combo);
            break;
        }
        default:
            break;
        }

        if (widget) {
            m_optionWidgets.append({widget, type, setting});
        }
    }
}

void AccountEditDialog::accept()
{
    QString protoId = m_protocolCombo->currentData().toString();
    if (m_usernameEdit->text().trimmed().isEmpty() || protoId.isEmpty()) {
        QMessageBox::warning(this, tr("Missing data"),
                             tr("Username and protocol are required."));
        return;
    }

    QString username = assembleUsername();

    PurpleAccount *acc = m_account;
    bool isNew = false;
    if (!acc) {
        acc = purple_account_new(username.toUtf8().constData(),
                                 protoId.toUtf8().constData());
        purple_accounts_add(acc);
        isNew = true;
    } else {
        purple_account_set_username(acc, username.toUtf8().constData());
        purple_account_set_protocol_id(acc, protoId.toUtf8().constData());
    }

    if (m_passwordEdit->isVisible()) {
        purple_account_set_password(acc, m_passwordEdit->text().toUtf8().constData());
        purple_account_set_remember_password(acc, m_rememberPassword->isChecked() ? TRUE : FALSE);
    } else {
        // Protocols with OPT_PROTO_NO_PASSWORD (e.g. Telegram tdlib) don't
        // use the field — clear any stale stored password so libpurple
        // doesn't try to authenticate with it.
        purple_account_set_password(acc, nullptr);
        purple_account_set_remember_password(acc, FALSE);
    }

    QString alias = m_aliasEdit->text().trimmed();
    purple_account_set_alias(acc, alias.isEmpty() ? nullptr : alias.toUtf8().constData());

    for (const auto &ow : m_optionWidgets) {
        switch (ow.type) {
        case PURPLE_PREF_BOOLEAN: {
            auto *cb = qobject_cast<QCheckBox *>(ow.widget);
            if (cb)
                purple_account_set_bool(acc, ow.settingName, cb->isChecked() ? TRUE : FALSE);
            break;
        }
        case PURPLE_PREF_INT: {
            auto *sb = qobject_cast<QSpinBox *>(ow.widget);
            if (sb)
                purple_account_set_int(acc, ow.settingName, sb->value());
            break;
        }
        case PURPLE_PREF_STRING: {
            auto *le = qobject_cast<QLineEdit *>(ow.widget);
            if (le)
                purple_account_set_string(acc, ow.settingName, le->text().toUtf8().constData());
            break;
        }
        case PURPLE_PREF_STRING_LIST: {
            auto *combo = qobject_cast<QComboBox *>(ow.widget);
            if (combo)
                purple_account_set_string(acc, ow.settingName,
                                          combo->currentData().toString().toUtf8().constData());
            break;
        }
        default:
            break;
        }
    }

    if (isNew) {
        purple_account_set_enabled(acc, PurpleCore::UI_ID, TRUE);
    }

    QDialog::accept();
}

} // namespace konqix
