// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Radek Bucek

#pragma once

#include <QImage>
#include <QWidget>
#include <ctime>

extern "C" {
#include <libpurple/conversation.h>
}

class QTextBrowser;
class QTextEdit;
class QListWidget;
class QLabel;
class QFrame;
class QHBoxLayout;

namespace konqix {

class ConversationWindow : public QWidget
{
    Q_OBJECT
public:
    explicit ConversationWindow(PurpleConversation *conv, QWidget *parent = nullptr);
    ~ConversationWindow() override;

    PurpleConversation *conversation() const { return m_conv; }

    void appendMessage(const QString &who, const QString &alias,
                       const QString &message, PurpleMessageFlags flags, time_t mtime);
    void addChatUsers(GList *cbuddies, bool newArrivals);
    void removeChatUsers(GList *users);
    // Display / hide the "<name> is typing…" footer below the history.
    // No-op for chat windows (libpurple only fires the typing signal
    // for IMs).
    void setTypingState(bool typing);

signals:
    void closed();

protected:
    void closeEvent(QCloseEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void changeEvent(QEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private slots:
    void sendCurrent();
    void attachFile();

private:
    void loadHistory();
    void markRead();
    void showHistory();
    void showInfo();
    // Resize the input text edit to fit the current document, capped at
    // half the window height. Called on every textChanged and resize.
    void updateInputHeight();
    // Add a pasted image (path = our temp PNG, ownership ours so we
    // unlink on cancel/send).
    void addPastedImage(const QString &path, const QImage &img);
    // Add a user-picked or dropped file. Auto-detects whether to render
    // an image thumbnail or a generic file icon + filename tile.
    void addAttachment(const QString &path);
    void removePendingAttachment(const QString &path);
    // Refresh the peer name + status icon in the menu bar's corner
    // widget from m_peerBuddy's current presence. No-op for chats /
    // conversations with no matching blist entry.
    void updatePeerStatus();

    PurpleConversation *m_conv;
    QTextBrowser *m_history = nullptr;
    QTextEdit *m_input = nullptr;
    QListWidget *m_userList = nullptr;
    QLabel *m_topic = nullptr;

    // IM peer name + status icon, shown in the menu bar's top-right
    // corner. Null for chats (no single peer) or when the conversation
    // name doesn't resolve to a blist buddy. m_peerWidget wraps the
    // icon + label together so we can hide/show the whole thing in
    // step with the View → Show status icons toggle.
    struct _PurpleBuddy *m_peerBuddy = nullptr;
    QWidget *m_peerWidget = nullptr;
    QLabel *m_peerStatusIcon = nullptr;
    QLabel *m_peerNameLabel = nullptr;
    // Held only when HAVE_HUNSPELL was defined at compile time, but a
    // void* placeholder lets us reach into it from non-spell-aware code
    // without sprinkling #ifdefs all over the header.
    QObject *m_spell = nullptr;

    // Typing indicator state.
    QLabel *m_typingLabel = nullptr;
    class QTimer *m_typingHideTimer = nullptr;   // auto-clear if prpl never sends stop
    class QTimer *m_outTypingTimer = nullptr;    // re-send / time out our own typing
    bool m_sentTyping = false;                   // currently announcing PURPLE_TYPING?

    // Last live-rendered message — used to drop the immediate duplicate
    // some prpls deliver when a message from us reaches the account from
    // another device (e.g. tdlib-purple echoes our own sends twice on
    // the receiver side until the conv is reopened).
    struct LastMessage {
        qint64 ts = 0;
        QString sender;
        QString body;
        qint64 wallMs = 0;
    } m_lastMessage;

    QWidget *m_attachmentsBar = nullptr;
    QHBoxLayout *m_attachmentsLayout = nullptr;
    struct Pending {
        QString path;
        QFrame *thumb;
        bool removeOnDiscard;   // true for our temp paste files;
                                // false for user-picked / dropped files
                                // (we must NOT unlink those).
    };
    QList<Pending> m_pending;
};

} // namespace konqix
