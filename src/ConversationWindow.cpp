// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Radek Bucek

#include "ConversationWindow.h"
#include "HistoryDialog.h"
#include "LogFormat.h"
#include "MessageState.h"
#include "Notifier.h"
#include "SmoothBrowser.h"

#ifdef HAVE_HUNSPELL
#include "SpellChecker.h"
#include <QContextMenuEvent>
#endif

#include <QAction>
#include <QActionGroup>
#include <QBrush>
#include <QCloseEvent>
#include <QColor>
#include <QDateTime>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QFontMetrics>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QScrollBar>
#include <QLabel>
#include <QListWidget>
#include <QShowEvent>
#include <QSplitter>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QTextBrowser>
#include <QTextEdit>
#include <QVBoxLayout>

extern "C" {
#include <libpurple/imgstore.h>
#include <libpurple/log.h>
#include <libpurple/prefs.h>
#include <libpurple/blist.h>
#include <libpurple/server.h>
}

#include <QRegularExpression>
#include <QUrl>
#include <QClipboard>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QMimeData>
#include <QPixmap>
#include <QStandardPaths>
#include <QTextCursor>
#include <QToolButton>

#include <functional>

namespace konqix {

namespace {

// QTextEdit subclass that hands an inline image off to a callback when the
// clipboard paste (or drag&drop) carries one. Falls back to default text
// paste otherwise.
class PasteAwareTextEdit : public QTextEdit
{
public:
    using QTextEdit::QTextEdit;
    std::function<void(const QImage &)> onImagePaste;
#ifdef HAVE_HUNSPELL
    SpellHighlighter *spell = nullptr;
#endif
protected:
    bool canInsertFromMimeData(const QMimeData *src) const override
    {
        return (src->hasImage() && onImagePaste) || QTextEdit::canInsertFromMimeData(src);
    }
    void insertFromMimeData(const QMimeData *src) override
    {
        if (src->hasImage() && onImagePaste) {
            QImage img = qvariant_cast<QImage>(src->imageData());
            if (!img.isNull()) {
                onImagePaste(img);
                return;
            }
        }
        // Plain-text paste only — drop any HTML/rich formatting the clipboard
        // brought along (font, colour, size) so the input keeps the widget's
        // configured font. This matches every other IM client's paste-into-
        // compose behaviour and stops "why does my Telegram message suddenly
        // have Times New Roman 8pt" surprises.
        if (src->hasText()) {
            insertPlainText(src->text());
            return;
        }
        QTextEdit::insertFromMimeData(src);
    }
#ifdef HAVE_HUNSPELL
    void contextMenuEvent(QContextMenuEvent *event) override
    {
        QMenu *menu = createStandardContextMenu();
        if (spell && spell->hasDictionary()) {
            QTextCursor cur = cursorForPosition(event->pos());
            cur.select(QTextCursor::WordUnderCursor);
            QString word = cur.selectedText();
            if (!word.isEmpty() && spell->isMisspelled(word)) {
                QAction *firstAct = menu->actions().isEmpty()
                                    ? nullptr : menu->actions().first();
                QStringList suggestions = spell->suggest(word);
                if (!suggestions.isEmpty()) {
                    auto *sub = new QMenu(QObject::tr("Spelling: %1").arg(word),
                                          menu);
                    for (int i = 0; i < suggestions.size() && i < 8; ++i) {
                        const QString s = suggestions.at(i);
                        QAction *act = sub->addAction(s);
                        QTextCursor cc = cur;
                        QObject::connect(act, &QAction::triggered, this,
                            [cc, s]() mutable { cc.insertText(s); });
                    }
                    sub->addSeparator();
                    QAction *ignore = sub->addAction(QObject::tr("Add to dictionary"));
                    QString w = word;
                    SpellHighlighter *sp = spell;
                    QObject::connect(ignore, &QAction::triggered, this,
                        [sp, w]() { sp->ignoreWord(w); });
                    menu->insertMenu(firstAct, sub);
                    menu->insertSeparator(firstAct);
                }
            }
        }
        menu->exec(event->globalPos());
        delete menu;
    }
#endif
};

} // namespace

using konqix::colorForSender;
using konqix::convertNewlines;
using konqix::isTransferNoise;
using konqix::reformatLogMessages;
using konqix::resolveImgIds;
using konqix::resolveImgPaths;
using konqix::rewriteImageLinks;
using konqix::styleQuotes;


ConversationWindow::ConversationWindow(PurpleConversation *conv, QWidget *parent)
    : QWidget(parent, Qt::Window), m_conv(conv)
{
    const char *title = purple_conversation_get_title(conv);
    setWindowTitle(title ? QString::fromUtf8(title) : QStringLiteral("Conversation"));
    resize(700, 500);

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(4, 4, 4, 4);

    bool isChat = purple_conversation_get_type(conv) == PURPLE_CONV_TYPE_CHAT;

    // Menu bar with conversation-scoped actions.
    auto *menuBar = new QMenuBar(this);
    auto *convMenu = menuBar->addMenu(tr("&Conversation"));
    convMenu->addAction(tr("Show &history…"), this, &ConversationWindow::showHistory);
    if (!isChat) {
        // serv_get_info is per-buddy; for chats the right target would be a
        // selected participant — out of scope here, hide the action.
        convMenu->addAction(tr("Get &info"), this, &ConversationWindow::showInfo);
    }
    convMenu->addSeparator();
    convMenu->addAction(tr("&Close"), this, &QWidget::close);

#ifdef HAVE_HUNSPELL
    // Per-conversation language override. Stored as an account string
    // setting keyed by the conv name so it survives restarts and works
    // for both buddies and chats (we don't need a blist entry).
    if (purple_prefs_get_bool("/konqix/input/spellcheck")) {
        auto *spellMenu = menuBar->addMenu(tr("&Spelling"));
        auto *langGroup = new QActionGroup(this);
        langGroup->setExclusive(true);

        auto storedSetting = [conv]() -> QString {
            PurpleAccount *acc = purple_conversation_get_account(conv);
            if (!acc) return {};
            QByteArray key = QByteArrayLiteral("konqix-spell-lang:") +
                             QByteArray(purple_conversation_get_name(conv));
            return QString::fromUtf8(
                purple_account_get_string(acc, key.constData(), ""));
        };
        auto saveSetting = [conv](const QString &v) {
            PurpleAccount *acc = purple_conversation_get_account(conv);
            if (!acc) return;
            QByteArray key = QByteArrayLiteral("konqix-spell-lang:") +
                             QByteArray(purple_conversation_get_name(conv));
            purple_account_set_string(acc, key.constData(),
                                      v.toUtf8().constData());
        };
        auto applyToHighlighter = [this](const QString &storedVal) {
            if (!m_spell) return;
            auto *sh = qobject_cast<SpellHighlighter *>(m_spell);
            if (!sh) return;
            QString effective;
            if (storedVal == QLatin1String("@auto"))
                effective.clear();
            else if (!storedVal.isEmpty())
                effective = storedVal;
            else
                effective = QString::fromUtf8(
                    purple_prefs_get_string("/konqix/input/spellcheck_lang"));
            sh->setLanguage(effective);
        };

        const QString current = storedSetting();
        auto addRadio = [&](const QString &label, const QString &value) {
            auto *act = spellMenu->addAction(label);
            act->setCheckable(true);
            act->setChecked(current == value);
            langGroup->addAction(act);
            connect(act, &QAction::toggled, this,
                [value, saveSetting, applyToHighlighter](bool on) {
                    if (!on) return;
                    saveSetting(value);
                    applyToHighlighter(value);
                });
        };

        addRadio(tr("&Follow global setting"), QString());
        addRadio(tr("&Auto (from locale)"), QStringLiteral("@auto"));
        spellMenu->addSeparator();
        for (const QString &lang : SpellHighlighter::availableLanguages()) {
            QLocale loc(lang);
            QString label = (loc != QLocale::c())
                ? QStringLiteral("%1 (%2)").arg(loc.nativeLanguageName(), lang)
                : lang;
            addRadio(label, lang);
        }
    }
#endif

    outer->setMenuBar(menuBar);

    if (isChat) {
        m_topic = new QLabel(this);
        m_topic->setWordWrap(true);
        m_topic->setStyleSheet(QStringLiteral("QLabel { color: #555; }"));
        outer->addWidget(m_topic);
        QString topicText;
        if (PurpleConvChat *chat = purple_conversation_get_chat_data(conv)) {
            const char *topic = purple_conv_chat_get_topic(chat);
            if (topic) topicText = QString::fromUtf8(topic).trimmed();
        }
        if (topicText.isEmpty()) {
            m_topic->hide();   // empty topic would leave a tall blank band
        } else {
            m_topic->setText(topicText);
        }
    }

    auto *splitter = new QSplitter(Qt::Horizontal, this);
    // Hide the visible drag handle — it's a 1 px vertical line between the
    // history pane and the user list that looked out of place. Drag still
    // works on the boundary; the gap comes from the user list's margin-left.
    splitter->setHandleWidth(0);

    auto *centerWrap = new QWidget(splitter);
    auto *centerLayout = new QVBoxLayout(centerWrap);
    centerLayout->setContentsMargins(0, 0, 0, 0);
    // Tighten the gap between history and input to match the user-list margin.
    centerLayout->setSpacing(3);

    m_history = new QTextBrowser(centerWrap);
    m_history->setDocument(new konqix::SmoothImageDocument(m_history));
    // Let the OS handle clicked URLs (image viewer for *.jpg, browser for
    // http://…). Without this, QTextBrowser would call setSource() and try
    // to render the file inline as HTML, which turns binary files into
    // garbled text.
    m_history->setOpenLinks(false);
    connect(m_history, &QTextBrowser::anchorClicked,
            this, [](const QUrl &url) { QDesktopServices::openUrl(url); });
    centerLayout->addWidget(m_history, 1);

    // Typing footer — slim dim italic line that shows "<name> is
    // typing…" when libpurple's buddy-typing signal fires. Auto-hides
    // after ~10 s in case the prpl never sends a stop event.
    m_typingLabel = new QLabel(centerWrap);
    m_typingLabel->setStyleSheet(QStringLiteral(
        "color: #888; font-style: italic; padding: 0 4px;"));
    m_typingLabel->setText(QString());
    m_typingLabel->hide();
    centerLayout->addWidget(m_typingLabel);
    m_typingHideTimer = new QTimer(this);
    m_typingHideTimer->setSingleShot(true);
    m_typingHideTimer->setInterval(10000);
    connect(m_typingHideTimer, &QTimer::timeout, this,
            [this]() { setTypingState(false); });

    // Attachments bar — appears between history and input when an image
    // has been pasted but not yet sent. Hidden by default.
    m_attachmentsBar = new QWidget(centerWrap);
    m_attachmentsLayout = new QHBoxLayout(m_attachmentsBar);
    m_attachmentsLayout->setContentsMargins(0, 0, 0, 0);
    m_attachmentsLayout->setSpacing(4);
    m_attachmentsLayout->addStretch();
    m_attachmentsBar->hide();
    centerLayout->addWidget(m_attachmentsBar);

    auto *inputEdit = new PasteAwareTextEdit(centerWrap);
    inputEdit->onImagePaste = [this](const QImage &img) {
        // Save the pasted image to a temp file and stage it as a pending
        // attachment; the actual send happens on Enter together with the
        // typed caption.
        QString cacheDir = QStandardPaths::writableLocation(
                               QStandardPaths::CacheLocation)
                           + QStringLiteral("/konqix-paste");
        QDir().mkpath(cacheDir);
        QString fn = cacheDir + QStringLiteral("/paste-%1.png")
            .arg(QDateTime::currentDateTime()
                     .toString(QStringLiteral("yyyyMMdd-hhmmss-zzz")));
        if (!img.save(fn, "PNG")) {
            QMessageBox::warning(this, tr("Save failed"),
                tr("Could not write pasted image."));
            return;
        }
        addPastedImage(fn, img);
    };
    m_input = inputEdit;
#ifdef HAVE_HUNSPELL
    // Attach a Hunspell-backed spell highlighter to the input. We keep
    // a pointer on the PasteAwareTextEdit so its context-menu override
    // can pull suggestions for the misspelled word under the cursor.
    // Per-conversation language: read account-string setting first,
    // then fall back to the global pref, then autodetect.
    if (purple_prefs_get_bool("/konqix/input/spellcheck")) {
        auto convLangSetting = [conv]() -> QString {
            PurpleAccount *acc = purple_conversation_get_account(conv);
            if (!acc) return {};
            QByteArray key = QByteArrayLiteral("konqix-spell-lang:") +
                             QByteArray(purple_conversation_get_name(conv));
            return QString::fromUtf8(
                purple_account_get_string(acc, key.constData(), ""));
        };
        const QString perConv = convLangSetting();
        QString effective;
        if (perConv == QLatin1String("@auto"))
            effective.clear();   // explicit autodetect override
        else if (!perConv.isEmpty())
            effective = perConv;
        else
            effective = QString::fromUtf8(
                purple_prefs_get_string("/konqix/input/spellcheck_lang"));
        auto *spell = new SpellHighlighter(inputEdit->document(), effective);
        inputEdit->spell = spell;
        m_spell = spell;
    }
#endif
    m_input->setPlaceholderText(QStringLiteral(
        "Type a message and press Enter "
        "(paste/drop a file or use the attach button to send any file)…"));
    m_input->installEventFilter(this);
    // Auto-grow the input with typed content, capped at half of the window
    // height by updateInputHeight(). Seed the initial single-line height
    // here so the widget doesn't briefly show at its default (~200 px).
    m_input->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    connect(m_input, &QTextEdit::textChanged,
            this, &ConversationWindow::updateInputHeight);
    QTimer::singleShot(0, this, &ConversationWindow::updateInputHeight);

    // Outgoing typing notifications: tell the prpl we're typing while
    // the input has text. libpurple's serv_send_typing returns the
    // number of seconds before we should re-send the TYPING ping; we
    // also fire NOT_TYPING after a few idle seconds.
    if (!isChat) {
        m_outTypingTimer = new QTimer(this);
        m_outTypingTimer->setSingleShot(true);
        connect(m_outTypingTimer, &QTimer::timeout, this, [this]() {
            if (!m_conv || !m_sentTyping) return;
            PurpleAccount *acc = purple_conversation_get_account(m_conv);
            PurpleConnection *gc = acc ? purple_account_get_connection(acc) : nullptr;
            if (gc) serv_send_typing(gc, purple_conversation_get_name(m_conv),
                                     PURPLE_NOT_TYPING);
            m_sentTyping = false;
        });
        connect(m_input, &QTextEdit::textChanged, this, [this]() {
            if (!m_conv) return;
            PurpleAccount *acc = purple_conversation_get_account(m_conv);
            PurpleConnection *gc = acc ? purple_account_get_connection(acc) : nullptr;
            if (!gc) return;
            const QString name = QString::fromUtf8(
                purple_conversation_get_name(m_conv));
            bool empty = m_input->toPlainText().isEmpty();
            if (empty) {
                if (m_sentTyping) {
                    serv_send_typing(gc, name.toUtf8().constData(),
                                     PURPLE_NOT_TYPING);
                    m_sentTyping = false;
                }
                m_outTypingTimer->stop();
                return;
            }
            // Honour the prpl-suggested re-send interval; default to 4s
            // so prpls that don't expose one still keep the indicator alive.
            unsigned int secs = serv_send_typing(gc, name.toUtf8().constData(),
                                                 PURPLE_TYPING);
            m_sentTyping = true;
            m_outTypingTimer->start(secs > 0 ? int(secs) * 1000 : 4000);
        });
    }

    // Send + Attach buttons to the right of the input. Attach opens a
    // file picker and stages whatever the user chooses; Send dispatches
    // the typed text plus every staged attachment in one go.
    auto *inputRow = new QHBoxLayout();
    inputRow->setContentsMargins(0, 0, 0, 0);
    inputRow->setSpacing(4);
    inputRow->addWidget(m_input, 1);
    auto *attachBtn = new QToolButton(centerWrap);
    attachBtn->setIcon(style()->standardIcon(QStyle::SP_DialogOpenButton));
    attachBtn->setToolTip(tr("Attach file…"));
    attachBtn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    attachBtn->setAutoRaise(false);
    connect(attachBtn, &QToolButton::clicked, this, &ConversationWindow::attachFile);
    inputRow->addWidget(attachBtn);
    auto *sendBtn = new QPushButton(tr("Send"), centerWrap);
    sendBtn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    connect(sendBtn, &QPushButton::clicked, this, &ConversationWindow::sendCurrent);
    inputRow->addWidget(sendBtn);
    centerLayout->addLayout(inputRow);

    // Accept files dropped onto the conversation window — convenient
    // way to send any file without going through the picker.
    setAcceptDrops(true);

    splitter->addWidget(centerWrap);

    if (isChat) {
        m_userList = new QListWidget(splitter);
        m_userList->setSortingEnabled(true);
        // Explicit border via stylesheet — Breeze suppresses the default
        // QListWidget frame inside a QSplitter, so setFrameStyle alone
        // doesn't show up. The stylesheet forces a visible border to match
        // the history (QTextBrowser) and input (QTextEdit) panes.
        m_userList->setStyleSheet(QStringLiteral(
            "QListWidget {"
            "  border: 1px solid palette(mid);"
            "  border-radius: 2px;"
            "  background: palette(base);"
            "  margin-left: 3px;"
            "}"));
        splitter->addWidget(m_userList);
        // Stretch the history pane only — but configure initial sizes so the
        // user list pane equals the widget width. Without this the splitter
        // gave the right pane more space than the listwidget wanted, leaving
        // a visible blank gap between the list and the window edge.
        splitter->setStretchFactor(0, 1);
        splitter->setStretchFactor(1, 0);
        splitter->setSizes({ 500, 180 });
        splitter->setCollapsible(1, false);
    }

    outer->addWidget(splitter, 1);

    m_input->setFocus();

    loadHistory();
}

void ConversationWindow::loadHistory()
{
    if (!m_conv)
        return;
    if (!purple_prefs_get_bool("/konqix/logging/enabled"))
        return;

    PurpleLogType type = (purple_conversation_get_type(m_conv) == PURPLE_CONV_TYPE_CHAT)
                         ? PURPLE_LOG_CHAT : PURPLE_LOG_IM;
    const char *name = purple_conversation_get_name(m_conv);
    PurpleAccount *acc = purple_conversation_get_account(m_conv);
    GList *logs = purple_log_get_logs(type, name, acc);
    if (!logs)
        return;

    // Need the log dir up front: we derive "last activity" from the mtime of
    // the newest log file there, which feeds into the cutoff calculation.
    char *logDir = purple_log_get_log_dir(type, name, acc);
    QString baseDir = logDir ? QString::fromUtf8(logDir) : QString();
    g_free(logDir);

    // Three-rule cascade for the start of the visible window:
    //   1. default = midnight today
    //   2. if today so far covers less than 12h, extend back to now - 12h
    //   3. if the last activity predates midnight today, anchor to it: 12h
    //      before the last message (overrides 1 and 2)
    QDateTime now = QDateTime::currentDateTime();
    QDateTime midnightToday(now.date(), QTime(0, 0));
    QDateTime startQdt = midnightToday;
    if (midnightToday.secsTo(now) < 12 * 3600)
        startQdt = now.addSecs(-12 * 3600);
    QDateTime lastActivity;
    if (!baseDir.isEmpty()) {
        QFileInfoList files = QDir(baseDir).entryInfoList(
            QStringList() << QStringLiteral("*.html"),
            QDir::Files, QDir::Time);
        if (!files.isEmpty())
            lastActivity = files.first().lastModified();
    }
    if (lastActivity.isValid() && lastActivity < midnightToday)
        startQdt = lastActivity.addSecs(-12 * 3600);
    const time_t cutoff = startQdt.toSecsSinceEpoch();

    // Collect logs that touch the window. Library returns newest-first.
    QList<PurpleLog *> wanted;
    for (GList *l = logs; l; l = l->next) {
        auto *log = static_cast<PurpleLog *>(l->data);
        if (log->time < cutoff) {
            // Always include the most recent log even if its start time is
            // older — gives context when chats are infrequent.
            if (wanted.isEmpty())
                wanted.append(log);
            break;
        }
        wanted.append(log);
    }

    // Display chronologically (oldest of the window first). We don't add
    // any session-break separator — each message line already carries its
    // own timestamp, so the visual rhythm is enough.
    const bool isChat = (type == PURPLE_LOG_CHAT);
    QString selfAlias;
    if (isChat) {
        const char *a = purple_account_get_alias(acc);
        if (a && *a) selfAlias = QString::fromUtf8(a);
    }
    for (int i = wanted.size() - 1; i >= 0; --i) {
        PurpleLog *log = wanted.at(i);
        char *content = purple_log_read(log, nullptr);
        if (!content)
            continue;
        QString h = QString::fromUtf8(content);
        h = resolveImgPaths(resolveImgIds(h), baseDir);
        h = rewriteImageLinks(h);
        // Do NOT convertNewlines here — it would collapse the "<br>\n"
        // terminator the logger uses, making in-body <br> indistinguishable
        // from message boundaries. reformatLogMessages splits on the
        // terminator only.
        h = reformatLogMessages(h, log->time, isChat, selfAlias);
        QTextCursor c = m_history->textCursor();
        c.movePosition(QTextCursor::End);
        if (!m_history->document()->isEmpty())
            c.insertBlock();
        c.insertHtml(h);
        g_free(content);
    }

    // libpurple owns the logs returned; the GList itself is ours to free.
    g_list_free(logs);
}

void ConversationWindow::showHistory()
{
    if (!m_conv) return;
    PurpleLogType type = (purple_conversation_get_type(m_conv) == PURPLE_CONV_TYPE_CHAT)
                        ? PURPLE_LOG_CHAT : PURPLE_LOG_IM;
    auto *dlg = new HistoryDialog(type,
        QString::fromUtf8(purple_conversation_get_name(m_conv)),
        purple_conversation_get_account(m_conv), this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
}

namespace {

// Build the small × removal overlay used by every attachment tile.
QToolButton *makeRemoveButton(QFrame *parent, int xOffset)
{
    auto *closeBtn = new QToolButton(parent);
    closeBtn->setText(QStringLiteral("×"));
    closeBtn->setFixedSize(18, 18);
    closeBtn->move(xOffset, -2);
    closeBtn->setStyleSheet(QStringLiteral(
        "QToolButton { background: rgba(0,0,0,160); color: white; "
        "  border-radius: 9px; padding: 0; font-size: 13px; "
        "  border: 1px solid rgba(255,255,255,80); }"));
    return closeBtn;
}

} // namespace

void ConversationWindow::addPastedImage(const QString &path, const QImage &img)
{
    // 72-pixel square tile showing the scaled paste preview. Marked as
    // "owned" so the file is unlinked if the user cancels.
    auto *frame = new QFrame(m_attachmentsBar);
    frame->setFixedSize(72, 72);
    frame->setStyleSheet(QStringLiteral(
        "QFrame { border: 1px solid palette(mid); border-radius: 3px; "
        "         background: palette(base); }"));
    frame->setToolTip(path);

    auto *thumb = new QLabel(frame);
    thumb->setPixmap(QPixmap::fromImage(img.scaled(
        64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
    thumb->setAlignment(Qt::AlignCenter);
    thumb->setGeometry(4, 4, 64, 64);

    auto *closeBtn = makeRemoveButton(frame, 52);
    QString fn = path;
    connect(closeBtn, &QToolButton::clicked, this, [this, fn]() {
        removePendingAttachment(fn);
    });

    m_attachmentsLayout->insertWidget(m_attachmentsLayout->count() - 1, frame);
    m_pending.append({path, frame, /*removeOnDiscard=*/true});
    m_attachmentsBar->show();
}

void ConversationWindow::addAttachment(const QString &path)
{
    QFileInfo fi(path);
    if (!fi.exists() || !fi.isReadable()) {
        QMessageBox::warning(this, tr("Attach failed"),
            tr("Cannot read %1").arg(path));
        return;
    }

    // Image files get the same scaled-preview tile as a clipboard paste,
    // so the user instantly recognises what's queued.
    QImage img;
    if (img.load(path) && !img.isNull()) {
        auto *frame = new QFrame(m_attachmentsBar);
        frame->setFixedSize(72, 72);
        frame->setStyleSheet(QStringLiteral(
            "QFrame { border: 1px solid palette(mid); border-radius: 3px; "
            "         background: palette(base); }"));
        frame->setToolTip(path);
        auto *thumb = new QLabel(frame);
        thumb->setPixmap(QPixmap::fromImage(img.scaled(
            64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
        thumb->setAlignment(Qt::AlignCenter);
        thumb->setGeometry(4, 4, 64, 64);
        auto *closeBtn = makeRemoveButton(frame, 52);
        QString fn = path;
        connect(closeBtn, &QToolButton::clicked, this, [this, fn]() {
            removePendingAttachment(fn);
        });
        m_attachmentsLayout->insertWidget(m_attachmentsLayout->count() - 1, frame);
        // User-picked: never unlink the source on cancel.
        m_pending.append({path, frame, /*removeOnDiscard=*/false});
        m_attachmentsBar->show();
        return;
    }

    // Non-image: vertical tile with a mime icon on top and an elided
    // filename below. Wider than the image tile so the name has room.
    auto *frame = new QFrame(m_attachmentsBar);
    frame->setFixedSize(96, 72);
    frame->setStyleSheet(QStringLiteral(
        "QFrame { border: 1px solid palette(mid); border-radius: 3px; "
        "         background: palette(base); }"));
    frame->setToolTip(path);

    auto *vlay = new QVBoxLayout(frame);
    vlay->setContentsMargins(4, 4, 4, 4);
    vlay->setSpacing(2);

    QFileIconProvider iconProv;
    QIcon icon = iconProv.icon(fi);
    if (icon.isNull())
        icon = style()->standardIcon(QStyle::SP_FileIcon);
    auto *iconLabel = new QLabel(frame);
    iconLabel->setPixmap(icon.pixmap(36, 36));
    iconLabel->setAlignment(Qt::AlignCenter);
    vlay->addWidget(iconLabel);

    auto *nameLabel = new QLabel(frame);
    nameLabel->setAlignment(Qt::AlignCenter);
    QFontMetrics fm(nameLabel->font());
    nameLabel->setText(fm.elidedText(fi.fileName(), Qt::ElideMiddle, 84));
    QFont smallFont = nameLabel->font();
    smallFont.setPointSize(qMax(7, smallFont.pointSize() - 2));
    nameLabel->setFont(smallFont);
    vlay->addWidget(nameLabel);

    auto *closeBtn = makeRemoveButton(frame, 76);
    QString fn = path;
    connect(closeBtn, &QToolButton::clicked, this, [this, fn]() {
        removePendingAttachment(fn);
    });

    m_attachmentsLayout->insertWidget(m_attachmentsLayout->count() - 1, frame);
    m_pending.append({path, frame, /*removeOnDiscard=*/false});
    m_attachmentsBar->show();
}

void ConversationWindow::setTypingState(bool typing)
{
    if (!m_typingLabel) return;

    // Showing/hiding the label above shrinks/grows the QTextBrowser viewport.
    // Qt keeps the scrollbar's `value` fixed while `maximum` changes, which
    // pushes the last line below the new visible area. Detect "was at bottom"
    // now and re-pin after Qt has re-laid out the widgets.
    QScrollBar *bar = m_history ? m_history->verticalScrollBar() : nullptr;
    const bool wasAtBottom = bar && bar->value() >= bar->maximum() - 4;

    if (typing) {
        QString name;
        if (m_conv) {
            const char *raw = purple_conversation_get_name(m_conv);
            if (raw) {
                if (PurpleAccount *acc = purple_conversation_get_account(m_conv)) {
                    if (PurpleBuddy *b = purple_find_buddy(acc, raw)) {
                        const char *alias = purple_buddy_get_alias(b);
                        if (alias && *alias) name = QString::fromUtf8(alias);
                    }
                }
                if (name.isEmpty()) name = QString::fromUtf8(raw);
            }
        }
        m_typingLabel->setText(tr("%1 is typing…").arg(name));
        m_typingLabel->show();
        if (m_typingHideTimer) m_typingHideTimer->start();
    } else {
        m_typingLabel->clear();
        m_typingLabel->hide();
        if (m_typingHideTimer) m_typingHideTimer->stop();
    }

    if (wasAtBottom && bar) {
        QPointer<QTextBrowser> br = m_history;
        QTimer::singleShot(0, this, [br]() {
            if (!br) return;
            if (auto *b = br->verticalScrollBar())
                b->setValue(b->maximum());
        });
    }
}

void ConversationWindow::attachFile()
{
    QString picked = QFileDialog::getOpenFileName(this, tr("Attach file"),
        QStandardPaths::writableLocation(QStandardPaths::HomeLocation),
        tr("All files (*)"));
    if (picked.isEmpty()) return;
    addAttachment(picked);
}

void ConversationWindow::removePendingAttachment(const QString &path)
{
    for (int i = 0; i < m_pending.size(); ++i) {
        if (m_pending[i].path != path) continue;
        m_pending[i].thumb->deleteLater();
        if (m_pending[i].removeOnDiscard)
            QFile::remove(m_pending[i].path);
        m_pending.removeAt(i);
        break;
    }
    if (m_pending.isEmpty())
        m_attachmentsBar->hide();
}

void ConversationWindow::showInfo()
{
    if (!m_conv) return;
    PurpleAccount *acc = purple_conversation_get_account(m_conv);
    if (!acc || !purple_account_is_connected(acc)) {
        QMessageBox::warning(this, tr("Account offline"),
                             tr("Account is not connected."));
        return;
    }
    serv_get_info(purple_account_get_connection(acc),
                  purple_conversation_get_name(m_conv));
}

void ConversationWindow::markRead()
{
    if (!m_conv) return;
    if (MessageState::instance())
        MessageState::instance()->markRead(m_conv);
    // Stop flashing only when this is the last unread conversation —
    // otherwise the user would lose the tray hint for other unread chats.
    if (Notifier::instance() && MessageState::instance()
        && MessageState::instance()->totalUnread() == 0)
        Notifier::instance()->stopFlashing();
    // Make sure the freshly-arrived messages are actually in view —
    // QTextBrowser only auto-scrolls when the user was already at bottom,
    // so after switching back from another window we may otherwise look at
    // the older portion.
    if (m_history) {
        if (auto *bar = m_history->verticalScrollBar())
            bar->setValue(bar->maximum());
    }
}

void ConversationWindow::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    markRead();
    updateInputHeight();
}

void ConversationWindow::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    // Window height changed → the "half the window" cap moved with it.
    updateInputHeight();
}

void ConversationWindow::updateInputHeight()
{
    if (!m_input) return;
    QTextDocument *doc = m_input->document();
    if (!doc) return;

    // Match the document's wrap width to the widget's current viewport so
    // multi-line typed text reports its true rendered height (otherwise
    // QTextDocument uses -1 = single infinite line and the height stays at
    // one row until the widget itself grows).
    const qreal wrap = qMax(1, m_input->viewport()->width());
    if (!qFuzzyCompare(doc->textWidth(), wrap))
        doc->setTextWidth(wrap);

    const int frame = m_input->frameWidth() * 2;
    const QMargins m = m_input->contentsMargins();
    const int chrome = frame + m.top() + m.bottom() + 2;

    // The default (empty / short text) height matches what the input used
    // to be sized at with setMaximumHeight(120) plus an Expanding policy —
    // roughly seven lines. The field only grows *above* that as content
    // exceeds it, up to half of the window.
    const int defaultH = 120;
    const int docH = int(doc->size().height()) + chrome;
    const int cap = qMax(defaultH, height() / 2);
    const int target = qBound(defaultH, docH, cap);

    if (m_input->minimumHeight() != target || m_input->maximumHeight() != target) {
        m_input->setMinimumHeight(target);
        m_input->setMaximumHeight(target);
    }
}

void ConversationWindow::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    // WindowActivate fires reliably when this top-level window gains focus
    // (clicking it, alt-tab, tray click). focusInEvent on QWidget only
    // tracks the inner widget focus, which doesn't fire on window switch.
    if (event->type() == QEvent::ActivationChange && isActiveWindow())
        markRead();
}

ConversationWindow::~ConversationWindow() = default;

void ConversationWindow::closeEvent(QCloseEvent *event)
{
    emit closed();
    if (m_conv) {
        // Tell libpurple the user dismissed the conversation
        purple_conversation_destroy(m_conv);
        m_conv = nullptr;
    }
    event->accept();
}

bool ConversationWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_input && event->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(event);
        if ((ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter)
            && !(ke->modifiers() & Qt::ShiftModifier)) {
            sendCurrent();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void ConversationWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls()) {
        for (const QUrl &u : event->mimeData()->urls()) {
            if (u.isLocalFile()) {
                event->acceptProposedAction();
                return;
            }
        }
    }
    QWidget::dragEnterEvent(event);
}

void ConversationWindow::dropEvent(QDropEvent *event)
{
    if (!event->mimeData()->hasUrls()) {
        QWidget::dropEvent(event);
        return;
    }
    int added = 0;
    for (const QUrl &u : event->mimeData()->urls()) {
        if (!u.isLocalFile()) continue;
        addAttachment(u.toLocalFile());
        ++added;
    }
    if (added > 0) event->acceptProposedAction();
}

void ConversationWindow::sendCurrent()
{
    if (!m_conv) return;
    QString text = m_input->toPlainText();
    if (text.isEmpty() && m_pending.isEmpty())
        return;

    PurpleAccount *acc = purple_conversation_get_account(m_conv);
    if (!acc || !purple_account_is_connected(acc)) {
        QMessageBox::warning(this, tr("Account offline"),
                             tr("Account is not connected."));
        return;
    }
    PurpleConnection *gc = purple_account_get_connection(acc);

    // Send each pending attachment. libpurple's xfer flow writes a
    // "<a href=file://..jpg>" link into the conv when the transfer
    // completes — appendMessage / loadHistory post-processes such image
    // links into <img> tags so they render inline.
    //
    // IM and CHAT need different libpurple APIs: serv_send_file targets a
    // buddy by name and its can_receive_file gate refuses chat names,
    // while serv_chat_send_file targets a chat by numeric id.
    const bool isChat =
        purple_conversation_get_type(m_conv) == PURPLE_CONV_TYPE_CHAT;
    const int chatId = isChat
        ? purple_conv_chat_get_id(purple_conversation_get_chat_data(m_conv))
        : 0;
    for (const auto &item : m_pending) {
        if (isChat) {
            serv_chat_send_file(gc, chatId,
                                item.path.toUtf8().constData());
        } else {
            serv_send_file(gc,
                           purple_conversation_get_name(m_conv),
                           item.path.toUtf8().constData());
        }
        item.thumb->deleteLater();
    }
    m_pending.clear();
    m_attachmentsBar->hide();

    if (!text.isEmpty()) {
        // purple_conv_im_send / purple_conv_chat_send expect HTML — escape
        // and turn newlines into <br>.
        QString html = text.toHtmlEscaped();
        html.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
        html.replace(QChar('\n'), QStringLiteral("<br>"));
        QByteArray utf = html.toUtf8();
        if (purple_conversation_get_type(m_conv) == PURPLE_CONV_TYPE_IM) {
            purple_conv_im_send(purple_conversation_get_im_data(m_conv),
                                utf.constData());
        } else if (purple_conversation_get_type(m_conv) == PURPLE_CONV_TYPE_CHAT) {
            purple_conv_chat_send(purple_conversation_get_chat_data(m_conv),
                                  utf.constData());
        }
    }
    m_input->clear();
}

static QString escapeHtml(const QString &s)
{
    return s.toHtmlEscaped();
}


void ConversationWindow::appendMessage(const QString &who, const QString &alias,
                                       const QString &message, PurpleMessageFlags flags,
                                       time_t mtime)
{
    // Drop libpurple xfer narration; the inline image / outcome already
    // shows what happened. Keep ERROR-flagged system messages — those
    // surface real failures the user needs to see.
    if ((flags & PURPLE_MESSAGE_SYSTEM) && !(flags & PURPLE_MESSAGE_ERROR)
        && isTransferNoise(message))
        return;

    // Drop a back-to-back duplicate of the previous live message.
    // tdlib-purple sometimes delivers the user's own message twice on
    // a sibling device (sync echo + normal recv). The log on disk has
    // only one entry, so close+reopen renders correctly; live we have
    // to dedup ourselves. The 1.5s wall-time window is short enough to
    // never collide with a user retyping the same text.
    {
        const QString senderKey = alias.isEmpty() ? who : alias;
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (m_lastMessage.ts == qint64(mtime)
            && m_lastMessage.sender == senderKey
            && m_lastMessage.body == message
            && now - m_lastMessage.wallMs < 1500) {
            return;
        }
        m_lastMessage.ts      = qint64(mtime);
        m_lastMessage.sender  = senderKey;
        m_lastMessage.body    = message;
        m_lastMessage.wallMs  = now;
    }

    QString tsString = QDateTime::fromSecsSinceEpoch(mtime)
                       .toString(QStringLiteral("HH:mm:ss yyyy-MM-dd"));
    QString displayName = alias.isEmpty() ? who : alias;

    // Palette mirrors libpurple's HTML logger so history scrolled into
    // view doesn't visually flip colors when a fresh message arrives.
    // For chats we override the per-sender colour with our own hash —
    // libpurple's logger reuses one red for everyone in a chat, so
    // without this Radek and Martina would be indistinguishable.
    const bool inChat = m_conv &&
        purple_conversation_get_type(m_conv) == PURPLE_CONV_TYPE_CHAT;
    QString selfAlias, selfNick;
    if (m_conv) {
        if (PurpleAccount *acc = purple_conversation_get_account(m_conv)) {
            const char *a = purple_account_get_alias(acc);
            if (a && *a) selfAlias = QString::fromUtf8(a);
        }
        if (inChat) {
            if (PurpleConvChat *chat = purple_conversation_get_chat_data(m_conv)) {
                const char *nick = purple_conv_chat_get_nick(chat);
                if (nick) selfNick = QString::fromUtf8(nick);
            }
        }
    }

    // For self-sent messages libpurple hands us the prpl's idea of "who you
    // are" — for tdlib-purple that's the phone number. Substitute the
    // account's local alias so the user sees a name, not a number.
    const bool isSelf = (flags & PURPLE_MESSAGE_SEND)
                     || (!selfNick.isEmpty() && (who == selfNick || alias == selfNick));
    if (isSelf && !selfAlias.isEmpty())
        displayName = selfAlias;

    QString color = QStringLiteral("#000");
    if (flags & PURPLE_MESSAGE_SYSTEM)      color = QStringLiteral("#888");
    else if (flags & PURPLE_MESSAGE_ERROR)  color = QStringLiteral("#a00");
    else if (flags & PURPLE_MESSAGE_SEND)   color = QStringLiteral("#16569E");
    else if (flags & PURPLE_MESSAGE_RECV) {
        if (inChat) {
            color = (!selfAlias.isEmpty() && displayName == selfAlias)
                ? QStringLiteral("#16569E")
                : colorForSender(displayName);
        } else {
            color = QStringLiteral("#A82F2F");
        }
    }

    // libpurple often passes message text that already contains HTML — keep
    // it, but expand <img id="N"> tags via the imgstore to inline data URIs,
    // and rewrite relative <img src> to absolute file:// so QTextBrowser
    // can find received images saved next to the log files. Also collapse
    // libpurple-xfer-style image hyperlinks into <img> tags.
    QString body = rewriteImageLinks(resolveImgIds(message));
    if (m_conv) {
        PurpleLogType lt = (purple_conversation_get_type(m_conv) == PURPLE_CONV_TYPE_CHAT)
                          ? PURPLE_LOG_CHAT : PURPLE_LOG_IM;
        char *dir = purple_log_get_log_dir(lt,
            purple_conversation_get_name(m_conv),
            purple_conversation_get_account(m_conv));
        if (dir) {
            body = resolveImgPaths(body, QString::fromUtf8(dir));
            g_free(dir);
        }
    }
    // Plain "\n" between lines is whitespace in HTML — turn it into <br>
    // so multi-line messages don't collapse onto a single line.
    body = convertNewlines(body);
    body = styleQuotes(body);

    // Header line: "Sender HH:MM:SS YYYY-MM-DD".  Content on the next line.
    QString header;
    if (displayName.isEmpty()) {
        // System / xfer notifications without a sender — just timestamp.
        header = QStringLiteral(
            "<span style='color:#888; font-size:smaller'>%1</span>")
            .arg(tsString);
    } else {
        header = QStringLiteral(
            "<b style='color:%1'>%2</b> "
            "<span style='color:#888; font-size:smaller'>%3</span>")
            .arg(color, escapeHtml(displayName), tsString);
    }
    QString html = QStringLiteral(
        "<div style='margin:3px 0 0 0'>%1</div>"
        "<div style='margin:0'>%2</div>"
    ).arg(header, body);

    // insertBlock() forces a new paragraph at the cursor position; without
    // it QTextDocument will splice a freshly-inserted <div> into the
    // trailing line of the previous message and the next header ends up on
    // the same row as the previous body.
    QTextCursor c = m_history->textCursor();
    c.movePosition(QTextCursor::End);
    if (!m_history->document()->isEmpty())
        c.insertBlock();
    c.insertHtml(html);

    // Always pin to bottom on new messages — QTextBrowser only auto-scrolls
    // when the user was already at the bottom, so this is the only way to
    // surface a freshly-arrived message after the user scrolled up.
    if (auto *bar = m_history->verticalScrollBar())
        bar->setValue(bar->maximum());
}

void ConversationWindow::addChatUsers(GList *cbuddies, bool /*newArrivals*/)
{
    if (!m_userList) return;
    // Self detection: prefer the account alias (Telegram's user_id from
    // purple_conv_chat_get_nick doesn't match "Radek Bucek" display), and
    // fall back to the chat nick if no alias is configured.
    QString selfAlias, selfNick;
    if (m_conv) {
        if (PurpleAccount *acc = purple_conversation_get_account(m_conv)) {
            const char *a = purple_account_get_alias(acc);
            if (a && *a) selfAlias = QString::fromUtf8(a);
        }
        if (PurpleConvChat *chat = purple_conversation_get_chat_data(m_conv)) {
            const char *nick = purple_conv_chat_get_nick(chat);
            if (nick) selfNick = QString::fromUtf8(nick);
        }
    }
    for (GList *l = cbuddies; l; l = l->next) {
        PurpleConvChatBuddy *cb = static_cast<PurpleConvChatBuddy *>(l->data);
        if (!cb) continue;
        const char *name = purple_conv_chat_cb_get_name(cb);
        if (!name) continue;
        QString display = (cb->alias && *cb->alias)
                          ? QString::fromUtf8(cb->alias)
                          : QString::fromUtf8(name);
        auto *item = new QListWidgetItem(display, m_userList);
        item->setData(Qt::UserRole, QString::fromUtf8(name));
        bool isSelf = (!selfAlias.isEmpty() && selfAlias == display)
                   || (!selfNick.isEmpty()
                       && (selfNick == QString::fromUtf8(name)
                           || selfNick == display));
        QString hex = isSelf ? QStringLiteral("#16569E")
                             : colorForSender(display);
        item->setForeground(QBrush(QColor(hex)));
    }
}

void ConversationWindow::removeChatUsers(GList *users)
{
    if (!m_userList) return;
    for (GList *l = users; l; l = l->next) {
        const char *name = static_cast<const char *>(l->data);
        if (!name) continue;
        QString s = QString::fromUtf8(name);
        for (int i = 0; i < m_userList->count(); ) {
            if (m_userList->item(i)->data(Qt::UserRole).toString() == s)
                delete m_userList->takeItem(i);
            else
                ++i;
        }
    }
}

} // namespace konqix
