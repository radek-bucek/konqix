// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Radek Bucek

#include "NotifyDispatcher.h"

#include <QDesktopServices>
#include <QDialog>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSet>
#include <QTextBrowser>
#include <QUrl>
#include <QVBoxLayout>

extern "C" {
#include <libpurple/notify.h>
}

namespace konqix {

namespace {

// Tracks notify dialogs that are still alive. libpurple keeps the raw
// pointer it gets back from our notify_* callbacks in an internal handle
// table; on shutdown it calls close_notify on every handle. Without this
// guard, dialogs the user already closed (WA_DeleteOnClose → destroyed)
// would be deref'd as dangling pointers at quit time → SIGSEGV.
QSet<void *> g_aliveHandles;

void registerHandle(QWidget *w)
{
    g_aliveHandles.insert(w);
    QObject::connect(w, &QObject::destroyed, [w]() {
        g_aliveHandles.remove(w);
    });
}

void *notifyMessage(PurpleNotifyMsgType type, const char *title,
                    const char *primary, const char *secondary)
{
    auto *box = new QMessageBox;
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->setWindowTitle(QString::fromUtf8(title ? title : "Konqix"));
    box->setText(QString::fromUtf8(primary ? primary : ""));
    if (secondary)
        box->setInformativeText(QString::fromUtf8(secondary));
    switch (type) {
    case PURPLE_NOTIFY_MSG_ERROR:   box->setIcon(QMessageBox::Critical); break;
    case PURPLE_NOTIFY_MSG_WARNING: box->setIcon(QMessageBox::Warning);  break;
    case PURPLE_NOTIFY_MSG_INFO:    box->setIcon(QMessageBox::Information); break;
    }
    registerHandle(box);
    box->show();
    return box;
}

void *notifyEmail(PurpleConnection *, const char *subject, const char *from,
                  const char *to, const char *url)
{
    auto *box = new QMessageBox;
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->setWindowTitle(QObject::tr("New mail"));
    box->setIcon(QMessageBox::Information);
    box->setText(QObject::tr("You have new mail."));
    QString info;
    if (from)    info += QObject::tr("From: %1\n").arg(QString::fromUtf8(from));
    if (to)      info += QObject::tr("To: %1\n").arg(QString::fromUtf8(to));
    if (subject) info += QObject::tr("Subject: %1\n").arg(QString::fromUtf8(subject));
    if (url)     info += QObject::tr("URL: %1").arg(QString::fromUtf8(url));
    box->setInformativeText(info);
    registerHandle(box);
    box->show();
    return box;
}

void *notifyEmails(PurpleConnection *gc, size_t count, gboolean detailed,
                   const char **subjects, const char **froms,
                   const char **tos, const char **urls)
{
    Q_UNUSED(gc); Q_UNUSED(detailed);
    Q_UNUSED(subjects); Q_UNUSED(froms); Q_UNUSED(tos); Q_UNUSED(urls);
    auto *box = new QMessageBox;
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->setIcon(QMessageBox::Information);
    box->setWindowTitle(QObject::tr("New mail"));
    box->setText(QObject::tr("You have %n new message(s).", "", (int)count));
    registerHandle(box);
    box->show();
    return box;
}

void *notifyFormatted(const char *title, const char *primary,
                      const char *secondary, const char *text)
{
    auto *dlg = new QDialog;
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowTitle(QString::fromUtf8(title ? title : "Konqix"));
    dlg->resize(480, 380);
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
    auto *tb = new QTextBrowser(dlg);
    tb->setOpenExternalLinks(true);
    if (text) tb->setHtml(QString::fromUtf8(text));
    layout->addWidget(tb, 1);
    auto *close = new QPushButton(QObject::tr("Close"), dlg);
    QObject::connect(close, &QPushButton::clicked, dlg, &QDialog::accept);
    layout->addWidget(close);
    registerHandle(dlg);
    dlg->show();
    return dlg;
}

void *notifySearchresults(PurpleConnection *, const char *title, const char *primary,
                          const char *secondary, PurpleNotifySearchResults *,
                          gpointer)
{
    return notifyFormatted(title ? title : "Search results", primary, secondary, nullptr);
}

void notifySearchresultsNewRows(PurpleConnection *, PurpleNotifySearchResults *, void *)
{
}

void *notifyUserinfo(PurpleConnection *, const char *who, PurpleNotifyUserInfo *userInfo)
{
    char *text = purple_notify_user_info_get_text_with_newline(userInfo, "<br>");
    auto *dlg = static_cast<QDialog *>(
        notifyFormatted("User info", who, nullptr, text ? text : ""));
    g_free(text);
    return dlg;
}

void *notifyUri(const char *uri)
{
    if (uri)
        QDesktopServices::openUrl(QUrl(QString::fromUtf8(uri)));
    return nullptr;
}

void closeNotify(PurpleNotifyType, void *uiHandle)
{
    if (!uiHandle || !g_aliveHandles.contains(uiHandle))
        return;
    g_aliveHandles.remove(uiHandle);
    static_cast<QWidget *>(uiHandle)->close();
}

PurpleNotifyUiOps g_ops = {
    notifyMessage,
    notifyEmail,
    notifyEmails,
    notifyFormatted,
    notifySearchresults,
    notifySearchresultsNewRows,
    notifyUserinfo,
    notifyUri,
    closeNotify,
    nullptr, nullptr, nullptr, nullptr
};

} // namespace

PurpleNotifyUiOps *NotifyDispatcher::uiOps()
{
    return &g_ops;
}

} // namespace konqix
