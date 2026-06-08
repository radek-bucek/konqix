// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Radek Bucek

#include "RequestDispatcher.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QSet>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QWidget>

extern "C" {
#include <libpurple/request.h>
#include <libpurple/account.h>
}

namespace konqix {

namespace {

// See NotifyDispatcher for the rationale — same shutdown crash applied here.
QSet<void *> g_aliveHandles;

void registerHandle(QWidget *w)
{
    g_aliveHandles.insert(w);
    QObject::connect(w, &QObject::destroyed, [w]() {
        g_aliveHandles.remove(w);
    });
}

typedef void (*PurpleRequestInputCbType)(void *, const char *);
typedef void (*PurpleRequestChoiceCbType)(void *, int);
typedef void (*PurpleRequestActionCbType)(void *, int);
typedef void (*PurpleRequestFieldsCbType)(void *, PurpleRequestFields *);
typedef void (*PurpleRequestFileCbType)(void *, const char *);

// GTK marks the accelerator letter of a button label with a leading
// underscore ("_Accept" → Alt+A). Qt uses '&' for the same purpose, so
// without translation the underscore shows up as visible noise on every
// libpurple-supplied button. Also: GTK escapes a literal '_' as '__',
// and Qt escapes literal '&' as '&&'.
QString gtkMnemonicToQt(const char *raw)
{
    if (!raw) return {};
    QString in = QString::fromUtf8(raw);
    QString out;
    out.reserve(in.size());
    bool foundAccel = false;
    for (int i = 0; i < in.size(); ++i) {
        QChar c = in.at(i);
        if (c == QLatin1Char('&')) {
            out.append(QStringLiteral("&&"));
        } else if (c == QLatin1Char('_')) {
            if (i + 1 < in.size() && in.at(i + 1) == QLatin1Char('_')) {
                out.append(QLatin1Char('_'));
                ++i;
            } else if (!foundAccel) {
                out.append(QLatin1Char('&'));
                foundAccel = true;
            }
            // Subsequent unescaped underscores: drop (matches GTK behavior).
        } else {
            out.append(c);
        }
    }
    return out;
}

void *requestInput(const char *title, const char *primary, const char *secondary,
                   const char *defaultValue, gboolean multiline, gboolean masked,
                   gchar *hint, const char *okText, GCallback okCb,
                   const char *cancelText, GCallback cancelCb,
                   PurpleAccount *, const char *, PurpleConversation *,
                   void *userData)
{
    Q_UNUSED(hint);

    auto *dlg = new QDialog;
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowTitle(QString::fromUtf8(title ? title : "Konqix"));

    auto *layout = new QVBoxLayout(dlg);
    if (primary && *primary) {
        auto *l = new QLabel(QString::fromUtf8(primary), dlg);
        l->setWordWrap(true);
        QFont f = l->font();
        f.setBold(true);
        l->setFont(f);
        layout->addWidget(l);
    }
    if (secondary && *secondary) {
        auto *l = new QLabel(QString::fromUtf8(secondary), dlg);
        l->setWordWrap(true);
        layout->addWidget(l);
    }

    QPointer<QLineEdit> singleEdit;
    QPointer<QPlainTextEdit> multiEdit;
    if (multiline) {
        multiEdit = new QPlainTextEdit(dlg);
        if (defaultValue)
            multiEdit->setPlainText(QString::fromUtf8(defaultValue));
        layout->addWidget(multiEdit, 1);
    } else {
        singleEdit = new QLineEdit(dlg);
        if (masked)
            singleEdit->setEchoMode(QLineEdit::Password);
        if (defaultValue)
            singleEdit->setText(QString::fromUtf8(defaultValue));
        layout->addWidget(singleEdit);
    }

    auto *btns = new QDialogButtonBox(dlg);
    auto *okBtn = btns->addButton(gtkMnemonicToQt(okText ? okText : "OK"),
                                  QDialogButtonBox::AcceptRole);
    btns->addButton(gtkMnemonicToQt(cancelText ? cancelText : "Cancel"),
                    QDialogButtonBox::RejectRole);
    Q_UNUSED(okBtn);
    layout->addWidget(btns);

    QObject::connect(btns, &QDialogButtonBox::accepted, dlg, &QDialog::accept);
    QObject::connect(btns, &QDialogButtonBox::rejected, dlg, &QDialog::reject);

    auto okCallback = reinterpret_cast<PurpleRequestInputCbType>(okCb);
    auto cancelCallback = reinterpret_cast<PurpleRequestInputCbType>(cancelCb);

    QObject::connect(dlg, &QDialog::accepted, dlg,
        [dlg, singleEdit, multiEdit, okCallback, userData]() {
            if (!okCallback) return;
            QByteArray data;
            if (singleEdit)
                data = singleEdit->text().toUtf8();
            else if (multiEdit)
                data = multiEdit->toPlainText().toUtf8();
            okCallback(userData, data.constData());
        });
    QObject::connect(dlg, &QDialog::rejected, dlg,
        [cancelCallback, userData]() {
            if (cancelCallback)
                cancelCallback(userData, nullptr);
        });

    registerHandle(dlg); dlg->show();
    return dlg;
}

void *requestChoice(const char *title, const char *primary, const char *secondary,
                    int defaultValue, const char *okText, GCallback okCb,
                    const char *cancelText, GCallback cancelCb,
                    PurpleAccount *, const char *, PurpleConversation *,
                    void *userData, va_list choices)
{
    Q_UNUSED(okText); Q_UNUSED(cancelText);

    auto *dlg = new QDialog;
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowTitle(QString::fromUtf8(title ? title : "Konqix"));

    auto *layout = new QVBoxLayout(dlg);
    if (primary) {
        auto *l = new QLabel(QString::fromUtf8(primary), dlg);
        QFont f = l->font(); f.setBold(true); l->setFont(f);
        l->setWordWrap(true);
        layout->addWidget(l);
    }
    if (secondary) {
        auto *l = new QLabel(QString::fromUtf8(secondary), dlg);
        l->setWordWrap(true);
        layout->addWidget(l);
    }

    auto *combo = new QComboBox(dlg);
    QList<int> values;
    while (true) {
        const char *label = va_arg(choices, const char *);
        if (!label) break;
        int value = va_arg(choices, int);
        combo->addItem(QString::fromUtf8(label));
        values << value;
        if (value == defaultValue)
            combo->setCurrentIndex(combo->count() - 1);
    }
    layout->addWidget(combo);

    auto *btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dlg);
    layout->addWidget(btns);
    QObject::connect(btns, &QDialogButtonBox::accepted, dlg, &QDialog::accept);
    QObject::connect(btns, &QDialogButtonBox::rejected, dlg, &QDialog::reject);

    auto okCallback = reinterpret_cast<PurpleRequestChoiceCbType>(okCb);
    auto cancelCallback = reinterpret_cast<PurpleRequestChoiceCbType>(cancelCb);

    QObject::connect(dlg, &QDialog::accepted, dlg, [combo, values, okCallback, userData]() {
        if (!okCallback) return;
        int idx = combo->currentIndex();
        int v = (idx >= 0 && idx < values.size()) ? values.at(idx) : -1;
        okCallback(userData, v);
    });
    QObject::connect(dlg, &QDialog::rejected, dlg, [cancelCallback, userData]() {
        if (cancelCallback) cancelCallback(userData, -1);
    });

    registerHandle(dlg); dlg->show();
    return dlg;
}

void *requestAction(const char *title, const char *primary, const char *secondary,
                    int defaultAction, PurpleAccount *, const char *,
                    PurpleConversation *, void *userData,
                    size_t actionCount, va_list actions)
{
    auto *dlg = new QDialog;
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowTitle(QString::fromUtf8(title ? title : "Konqix"));

    auto *layout = new QVBoxLayout(dlg);
    if (primary) {
        auto *l = new QLabel(QString::fromUtf8(primary), dlg);
        QFont f = l->font(); f.setBold(true); l->setFont(f);
        l->setWordWrap(true);
        layout->addWidget(l);
    }
    if (secondary) {
        auto *l = new QLabel(QString::fromUtf8(secondary), dlg);
        l->setWordWrap(true);
        layout->addWidget(l);
    }

    auto *btnBox = new QDialogButtonBox(dlg);
    layout->addWidget(btnBox);

    QList<QPushButton *> buttons;
    QList<GCallback> callbacks;
    for (size_t i = 0; i < actionCount; ++i) {
        const char *label = va_arg(actions, const char *);
        GCallback cb = va_arg(actions, GCallback);
        auto *b = btnBox->addButton(gtkMnemonicToQt(label),
                                    QDialogButtonBox::ActionRole);
        buttons << b;
        callbacks << cb;
        QObject::connect(b, &QPushButton::clicked, dlg, [dlg, i, callbacks, userData]() {
            auto cb = reinterpret_cast<PurpleRequestActionCbType>(callbacks.at(i));
            if (cb) cb(userData, (int)i);
            dlg->accept();
        });
    }
    if (defaultAction >= 0 && defaultAction < buttons.size())
        buttons.at(defaultAction)->setDefault(true);

    registerHandle(dlg); dlg->show();
    return dlg;
}

QWidget *buildFieldWidget(PurpleRequestField *field, QWidget *parent)
{
    PurpleRequestFieldType type = purple_request_field_get_type(field);
    switch (type) {
    case PURPLE_REQUEST_FIELD_STRING: {
        if (purple_request_field_string_is_multiline(field)) {
            auto *te = new QPlainTextEdit(parent);
            const char *v = purple_request_field_string_get_value(field);
            if (v) te->setPlainText(QString::fromUtf8(v));
            return te;
        }
        auto *le = new QLineEdit(parent);
        if (purple_request_field_string_is_masked(field))
            le->setEchoMode(QLineEdit::Password);
        const char *v = purple_request_field_string_get_value(field);
        if (v) le->setText(QString::fromUtf8(v));
        return le;
    }
    case PURPLE_REQUEST_FIELD_INTEGER: {
        auto *sb = new QSpinBox(parent);
        sb->setRange(-1000000, 1000000);
        sb->setValue(purple_request_field_int_get_value(field));
        return sb;
    }
    case PURPLE_REQUEST_FIELD_BOOLEAN: {
        auto *cb = new QCheckBox(QString::fromUtf8(purple_request_field_get_label(field)), parent);
        cb->setChecked(purple_request_field_bool_get_value(field));
        return cb;
    }
    case PURPLE_REQUEST_FIELD_CHOICE: {
        auto *combo = new QComboBox(parent);
        int idx = 0;
        for (GList *l = purple_request_field_choice_get_labels(field); l; l = l->next, ++idx) {
            combo->addItem(QString::fromUtf8(static_cast<const char *>(l->data)));
        }
        combo->setCurrentIndex(purple_request_field_choice_get_value(field));
        return combo;
    }
    case PURPLE_REQUEST_FIELD_LABEL:
        return new QLabel(QString::fromUtf8(purple_request_field_get_label(field)), parent);
    default:
        return new QLabel(QStringLiteral("[unsupported field]"), parent);
    }
}

void readFieldWidget(PurpleRequestField *field, QWidget *widget)
{
    PurpleRequestFieldType type = purple_request_field_get_type(field);
    switch (type) {
    case PURPLE_REQUEST_FIELD_STRING: {
        if (purple_request_field_string_is_multiline(field)) {
            auto *te = qobject_cast<QPlainTextEdit *>(widget);
            if (te)
                purple_request_field_string_set_value(field, te->toPlainText().toUtf8().constData());
        } else {
            auto *le = qobject_cast<QLineEdit *>(widget);
            if (le)
                purple_request_field_string_set_value(field, le->text().toUtf8().constData());
        }
        break;
    }
    case PURPLE_REQUEST_FIELD_INTEGER: {
        auto *sb = qobject_cast<QSpinBox *>(widget);
        if (sb) purple_request_field_int_set_value(field, sb->value());
        break;
    }
    case PURPLE_REQUEST_FIELD_BOOLEAN: {
        auto *cb = qobject_cast<QCheckBox *>(widget);
        if (cb) purple_request_field_bool_set_value(field, cb->isChecked() ? TRUE : FALSE);
        break;
    }
    case PURPLE_REQUEST_FIELD_CHOICE: {
        auto *combo = qobject_cast<QComboBox *>(widget);
        if (combo) purple_request_field_choice_set_value(field, combo->currentIndex());
        break;
    }
    default:
        break;
    }
}

void *requestFields(const char *title, const char *primary, const char *secondary,
                    PurpleRequestFields *fields, const char *okText, GCallback okCb,
                    const char *cancelText, GCallback cancelCb,
                    PurpleAccount *, const char *, PurpleConversation *,
                    void *userData)
{
    auto *dlg = new QDialog;
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowTitle(QString::fromUtf8(title ? title : "Konqix"));
    dlg->resize(420, 360);

    auto *outer = new QVBoxLayout(dlg);
    if (primary && *primary) {
        auto *l = new QLabel(QString::fromUtf8(primary), dlg);
        QFont f = l->font(); f.setBold(true); l->setFont(f);
        l->setWordWrap(true);
        outer->addWidget(l);
    }
    if (secondary && *secondary) {
        auto *l = new QLabel(QString::fromUtf8(secondary), dlg);
        l->setWordWrap(true);
        outer->addWidget(l);
    }

    auto *scroll = new QScrollArea(dlg);
    scroll->setWidgetResizable(true);
    auto *holder = new QWidget(scroll);
    auto *holderLayout = new QVBoxLayout(holder);
    scroll->setWidget(holder);
    outer->addWidget(scroll, 1);

    struct FieldEntry { PurpleRequestField *field; QWidget *widget; };
    auto *entries = new QList<FieldEntry>;

    for (GList *gl = purple_request_fields_get_groups(fields); gl; gl = gl->next) {
        auto *group = static_cast<PurpleRequestFieldGroup *>(gl->data);
        auto *box = new QGroupBox(holder);
        const char *gtitle = group->title;
        if (gtitle && *gtitle) box->setTitle(QString::fromUtf8(gtitle));
        auto *form = new QFormLayout(box);
        for (GList *fl = purple_request_field_group_get_fields(group); fl; fl = fl->next) {
            auto *f = static_cast<PurpleRequestField *>(fl->data);
            QWidget *w = buildFieldWidget(f, box);
            entries->append({f, w});
            if (purple_request_field_get_type(f) == PURPLE_REQUEST_FIELD_BOOLEAN
                || purple_request_field_get_type(f) == PURPLE_REQUEST_FIELD_LABEL) {
                form->addRow(w);
            } else {
                const char *lbl = purple_request_field_get_label(f);
                form->addRow(QString::fromUtf8(lbl ? lbl : ""), w);
            }
        }
        holderLayout->addWidget(box);
    }
    holderLayout->addStretch();

    auto *btns = new QDialogButtonBox(dlg);
    btns->addButton(QString::fromUtf8(okText ? okText : "OK"), QDialogButtonBox::AcceptRole);
    btns->addButton(QString::fromUtf8(cancelText ? cancelText : "Cancel"),
                    QDialogButtonBox::RejectRole);
    outer->addWidget(btns);
    QObject::connect(btns, &QDialogButtonBox::accepted, dlg, &QDialog::accept);
    QObject::connect(btns, &QDialogButtonBox::rejected, dlg, &QDialog::reject);

    auto okCallback = reinterpret_cast<PurpleRequestFieldsCbType>(okCb);
    auto cancelCallback = reinterpret_cast<PurpleRequestFieldsCbType>(cancelCb);

    QObject::connect(dlg, &QDialog::accepted, dlg, [entries, fields, okCallback, userData]() {
        for (const auto &e : *entries)
            readFieldWidget(e.field, e.widget);
        if (okCallback) okCallback(userData, fields);
    });
    QObject::connect(dlg, &QDialog::rejected, dlg, [cancelCallback, fields, userData]() {
        if (cancelCallback) cancelCallback(userData, fields);
    });
    QObject::connect(dlg, &QDialog::destroyed, [entries]() { delete entries; });

    registerHandle(dlg); dlg->show();
    return dlg;
}

void *requestFile(const char *title, const char *filename, gboolean saveDialog,
                  GCallback okCb, GCallback cancelCb,
                  PurpleAccount *, const char *, PurpleConversation *,
                  void *userData)
{
    QString fn;
    QString t = QString::fromUtf8(title ? title : "");
    QString f = QString::fromUtf8(filename ? filename : "");
    if (saveDialog)
        fn = QFileDialog::getSaveFileName(nullptr, t, f);
    else
        fn = QFileDialog::getOpenFileName(nullptr, t, f);

    auto okCallback = reinterpret_cast<PurpleRequestFileCbType>(okCb);
    auto cancelCallback = reinterpret_cast<PurpleRequestFileCbType>(cancelCb);
    if (fn.isEmpty()) {
        if (cancelCallback) cancelCallback(userData, nullptr);
    } else {
        if (okCallback) okCallback(userData, fn.toUtf8().constData());
    }
    return nullptr;
}

void *requestFolder(const char *title, const char *dirname, GCallback okCb,
                    GCallback cancelCb, PurpleAccount *, const char *,
                    PurpleConversation *, void *userData)
{
    QString t = QString::fromUtf8(title ? title : "");
    QString f = QString::fromUtf8(dirname ? dirname : "");
    QString dir = QFileDialog::getExistingDirectory(nullptr, t, f);

    auto okCallback = reinterpret_cast<PurpleRequestFileCbType>(okCb);
    auto cancelCallback = reinterpret_cast<PurpleRequestFileCbType>(cancelCb);
    if (dir.isEmpty()) {
        if (cancelCallback) cancelCallback(userData, nullptr);
    } else {
        if (okCallback) okCallback(userData, dir.toUtf8().constData());
    }
    return nullptr;
}

void closeRequest(PurpleRequestType, void *uiHandle)
{
    if (!uiHandle || !g_aliveHandles.contains(uiHandle))
        return;
    g_aliveHandles.remove(uiHandle);
    static_cast<QDialog *>(uiHandle)->close();
}

PurpleRequestUiOps g_ops = {
    requestInput,
    requestChoice,
    requestAction,
    requestFields,
    requestFile,
    closeRequest,
    requestFolder,
    nullptr, // request_action_with_icon
    nullptr, // request_screenshare_media
    nullptr, nullptr
};

} // namespace

PurpleRequestUiOps *RequestDispatcher::uiOps()
{
    return &g_ops;
}

} // namespace konqix
