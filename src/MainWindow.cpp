// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Radek Bucek

#include "MainWindow.h"
#include "BuddyListModel.h"
#include "AccountsDialog.h"
#include "ConversationManager.h"
#include "HistoryDialog.h"
#include "LogIndex.h"
#include "Notifier.h"
#include "PurpleCore.h"
#ifdef HAVE_HUNSPELL
#include "SpellChecker.h"
#endif

#include <QActionGroup>

#include <QAction>
#include <QCloseEvent>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QStandardPaths>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>
#include <QToolBar>
#include <QTreeView>
#include <QVBoxLayout>

extern "C" {
#include <libpurple/blist.h>
#include <libpurple/conversation.h>
#include <libpurple/prefs.h>
#include <libpurple/prpl.h>
#include <libpurple/savedstatuses.h>
#include <libpurple/status.h>
}

namespace konqix {

// Map a libpurple status primitive to the matching tray-style SVG icon.
// Shared between the tray (Notifier) and the main-window title bar.
static QIcon iconForStatusPrimitive(int prim)
{
    switch (prim) {
        case PURPLE_STATUS_AWAY:          return QIcon(QStringLiteral(":/icons/konqix-away.svg"));
        case PURPLE_STATUS_EXTENDED_AWAY: return QIcon(QStringLiteral(":/icons/konqix-xaway.svg"));
        case PURPLE_STATUS_INVISIBLE:     return QIcon(QStringLiteral(":/icons/konqix-invisible.svg"));
        case PURPLE_STATUS_OFFLINE:       return QIcon(QStringLiteral(":/icons/konqix-offline.svg"));
        default:                          return QIcon(QStringLiteral(":/icons/konqix.svg"));
    }
}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("Konqix"));
    // Title-bar icon tracks the persisted status — buildStatusBar will
    // refresh it again with the same value once it runs.
    setWindowIcon(iconForStatusPrimitive(
        purple_prefs_get_int("/konqix/status/last_primitive")));
    resize(360, 600);

    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(2, 2, 2, 2);

    m_model = new BuddyListModel(this);
    // Apply persisted prefs to the model.
    m_model->setShowOffline(purple_prefs_get_bool("/konqix/blist/show_offline"));
    m_model->setShowAway(purple_prefs_get_bool("/konqix/blist/show_away"));
    m_model->setSortByStatus(purple_prefs_get_bool("/konqix/blist/sort_by_status"));
    m_model->setSecondarySort(BuddyListModel::parseSecondarySort(
        QString::fromUtf8(purple_prefs_get_string("/konqix/blist/sort_secondary"))));

    m_tree = new QTreeView(central);
    m_tree->setModel(m_model);
    m_tree->setHeaderHidden(true);
    m_tree->setRootIsDecorated(true);
    m_tree->setIndentation(14);
    // Double-click opens the conversation; expand/collapse stays on the
    // disclosure arrow so the gesture means one thing only.
    m_tree->setExpandsOnDoubleClick(false);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    layout->addWidget(m_tree, 1);

    // Dismissible warning banner — sits between the buddy list and the
    // status bar. Hidden by default; populated by warnAboutMissingProtocols().
    m_warningBanner = new QFrame(central);
    m_warningBanner->setFrameShape(QFrame::StyledPanel);
    m_warningBanner->setStyleSheet(QStringLiteral(
        "QFrame {"
        "  background: #fff3cd;"
        "  border: 1px solid #ffc107;"
        "  border-radius: 3px;"
        "}"
        "QLabel { background: transparent; border: none; color: #856404; }"));
    auto *bannerLayout = new QHBoxLayout(m_warningBanner);
    bannerLayout->setContentsMargins(6, 4, 4, 4);
    auto *iconLabel = new QLabel(m_warningBanner);
    iconLabel->setPixmap(style()->standardIcon(QStyle::SP_MessageBoxWarning)
                         .pixmap(16, 16));
    bannerLayout->addWidget(iconLabel);
    m_warningLabel = new QLabel(m_warningBanner);
    m_warningLabel->setWordWrap(true);
    bannerLayout->addWidget(m_warningLabel, 1);
    auto *closeBtn = new QToolButton(m_warningBanner);
    closeBtn->setText(QStringLiteral("×"));
    closeBtn->setAutoRaise(true);
    closeBtn->setToolTip(tr("Dismiss"));
    closeBtn->setStyleSheet(QStringLiteral(
        "QToolButton { background: transparent; border: none; "
        "  color: #856404; font-size: 16px; padding: 0 4px; }"));
    connect(closeBtn, &QToolButton::clicked, m_warningBanner, &QWidget::hide);
    bannerLayout->addWidget(closeBtn);
    m_warningBanner->hide();
    layout->addWidget(m_warningBanner);

    setCentralWidget(central);

    buildMenus();
    buildStatusBar();

    connect(m_tree, &QTreeView::doubleClicked, this, &MainWindow::onItemDoubleClicked);
    connect(m_tree, &QTreeView::customContextMenuRequested,
            this, &MainWindow::onItemContextMenu);
    // Default: expand groups (subject to per-group "collapsed" setting) and
    // keep contacts collapsed so each row stands for a single person.
    connect(m_model, &BuddyListModel::modelChanged,
            this, &MainWindow::expandTopGroups);

    // Persist expand/collapse state of groups across restarts via the
    // standard libpurple "collapsed" bool setting on the group node.
    connect(m_tree, &QTreeView::expanded, this, [this](const QModelIndex &idx) {
        PurpleBlistNode *n = m_model->nodeFromIndex(idx);
        if (n && purple_blist_node_get_type(n) == PURPLE_BLIST_GROUP_NODE)
            purple_blist_node_set_bool(n, "collapsed", FALSE);
    });
    connect(m_tree, &QTreeView::collapsed, this, [this](const QModelIndex &idx) {
        PurpleBlistNode *n = m_model->nodeFromIndex(idx);
        if (n && purple_blist_node_get_type(n) == PURPLE_BLIST_GROUP_NODE)
            purple_blist_node_set_bool(n, "collapsed", TRUE);
    });

    restoreGeometryFromPrefs();

    // libpurple silently skips connecting accounts whose protocol plugin
    // isn't loaded — we warn the user about it ourselves. Defer to the next
    // event loop tick so the buddy list is on screen first.
    QTimer::singleShot(0, this, &MainWindow::warnAboutMissingProtocols);
}

void MainWindow::warnAboutMissingProtocols()
{
    QStringList missing;
    for (GList *l = purple_accounts_get_all(); l; l = l->next) {
        auto *acc = static_cast<PurpleAccount *>(l->data);
        if (!purple_account_get_enabled(acc, PurpleCore::UI_ID)) continue;
        const char *protoId = purple_account_get_protocol_id(acc);
        if (!protoId) continue;
        if (!purple_find_prpl(protoId)) {
            missing << QStringLiteral("%1 (%2)")
                .arg(QString::fromUtf8(purple_account_get_username(acc)),
                     QString::fromUtf8(protoId));
        }
    }
    if (missing.isEmpty()) {
        m_warningBanner->hide();
        return;
    }
    m_warningLabel->setText(tr("Missing protocol plugin for: %1. "
                               "Install the matching libpurple plugin or "
                               "disable the account.")
                            .arg(missing.join(QStringLiteral(", "))));
    m_warningBanner->show();
}

void MainWindow::restoreGeometryFromPrefs()
{
    QByteArray b64 = QByteArray(purple_prefs_get_string("/konqix/window/geometry"));
    if (!b64.isEmpty()) {
        QByteArray state = QByteArray::fromBase64(b64);
        if (!state.isEmpty() && restoreGeometry(state))
            return;
    }
    int w = purple_prefs_get_int("/konqix/window/width");
    int h = purple_prefs_get_int("/konqix/window/height");
    if (w > 50 && h > 50)
        resize(w, h);
}

void MainWindow::saveGeometryToPrefs()
{
    if (isMinimized()) return;
    QRect g = geometry();
    if (g.width() < 50 || g.height() < 50) return;
    QByteArray b64 = saveGeometry().toBase64();
    purple_prefs_set_string("/konqix/window/geometry", b64.constData());
    purple_prefs_set_int("/konqix/window/width", g.width());
    purple_prefs_set_int("/konqix/window/height", g.height());
}

void MainWindow::hideEvent(QHideEvent *event)
{
    QMainWindow::hideEvent(event);
    saveGeometryToPrefs();
}

void MainWindow::expandTopGroups()
{
    for (int i = 0; i < m_model->rowCount(); ++i) {
        QModelIndex idx = m_model->index(i, 0);
        PurpleBlistNode *node = m_model->nodeFromIndex(idx);
        bool collapsed = false;
        if (node && purple_blist_node_get_type(node) == PURPLE_BLIST_GROUP_NODE)
            collapsed = purple_blist_node_get_bool(node, "collapsed");
        if (collapsed)
            m_tree->collapse(idx);
        else
            m_tree->expand(idx);
    }
}

void MainWindow::buildMenus()
{
    auto *fileMenu = menuBar()->addMenu(tr("&File"));
    auto *accountsAct = fileMenu->addAction(tr("&Accounts…"));
    connect(accountsAct, &QAction::triggered, this, &MainWindow::showAccountsDialog);

    auto *addBuddyAct = fileMenu->addAction(tr("Add &Buddy…"));
    connect(addBuddyAct, &QAction::triggered, this, &MainWindow::showAddBuddyDialog);

    auto *addGroupAct = fileMenu->addAction(tr("Add &Group…"));
    connect(addGroupAct, &QAction::triggered, this, &MainWindow::addGroup);

    fileMenu->addSeparator();
    auto *quitAct = fileMenu->addAction(tr("&Quit"));
    connect(quitAct, &QAction::triggered, qApp, &QCoreApplication::quit);

    auto *viewMenu = menuBar()->addMenu(tr("&View"));
    auto *showOffline = viewMenu->addAction(tr("Show &offline buddies"));
    showOffline->setCheckable(true);
    showOffline->setChecked(m_model->showOffline());
    connect(showOffline, &QAction::toggled, this, &MainWindow::onShowOfflineToggled);

    auto *showAway = viewMenu->addAction(tr("Show &away buddies"));
    showAway->setCheckable(true);
    showAway->setChecked(m_model->showAway());
    connect(showAway, &QAction::toggled, this, &MainWindow::onShowAwayToggled);

    viewMenu->addSeparator();
    auto *sortMenu = viewMenu->addMenu(tr("&Sort by"));

    auto *statusAct = sortMenu->addAction(tr("&Status"));
    statusAct->setCheckable(true);
    statusAct->setChecked(m_model->sortByStatus());
    connect(statusAct, &QAction::toggled, this, [this](bool on) {
        m_model->setSortByStatus(on);
        purple_prefs_set_bool("/konqix/blist/sort_by_status", on ? TRUE : FALSE);
        expandTopGroups();
    });

    sortMenu->addSeparator();
    auto *secondaryGroup = new QActionGroup(this);
    secondaryGroup->setExclusive(true);
    auto addSecondary = [&](const QString &label,
                            BuddyListModel::SecondarySort mode) {
        auto *a = sortMenu->addAction(label);
        a->setCheckable(true);
        secondaryGroup->addAction(a);
        a->setChecked(m_model->secondarySort() == mode);
        connect(a, &QAction::triggered, this, [this, mode]() {
            m_model->setSecondarySort(mode);
            purple_prefs_set_string("/konqix/blist/sort_secondary",
                BuddyListModel::secondarySortToString(mode).toUtf8().constData());
            expandTopGroups();
        });
    };
    addSecondary(tr("&Name (A–Z)"),        BuddyListModel::SecondarySort::Name);
    addSecondary(tr("Last &conversation"), BuddyListModel::SecondarySort::ActivityDesc);
    addSecondary(tr("Last &seen online"),  BuddyListModel::SecondarySort::LastSeen);

    viewMenu->addSeparator();
#ifdef HAVE_HUNSPELL
    auto *spellMenu = viewMenu->addMenu(tr("Check &spelling"));
    auto *spellAct = spellMenu->addAction(tr("&Enabled"));
    spellAct->setCheckable(true);
    spellAct->setChecked(purple_prefs_get_bool("/konqix/input/spellcheck"));
    connect(spellAct, &QAction::toggled, this, [](bool on) {
        purple_prefs_set_bool("/konqix/input/spellcheck", on ? TRUE : FALSE);
    });
    spellMenu->addSeparator();
    {
        auto *langGroup = new QActionGroup(spellMenu);
        langGroup->setExclusive(true);
        QString curLang = QString::fromUtf8(
            purple_prefs_get_string("/konqix/input/spellcheck_lang"));
        auto *autoAct = spellMenu->addAction(tr("&Auto (from locale)"));
        autoAct->setCheckable(true);
        autoAct->setChecked(curLang.isEmpty());
        langGroup->addAction(autoAct);
        connect(autoAct, &QAction::toggled, this, [](bool on) {
            if (on) purple_prefs_set_string(
                "/konqix/input/spellcheck_lang", "");
        });
        for (const QString &lang : konqix::SpellHighlighter::availableLanguages()) {
            QLocale loc(lang);
            // Render as "Native name (lang_code)" when QLocale recognises
            // the BCP-47 tag, otherwise just the raw locale name.
            QString label = (loc != QLocale::c())
                ? QStringLiteral("%1 (%2)").arg(loc.nativeLanguageName(), lang)
                : lang;
            auto *a = spellMenu->addAction(label);
            a->setCheckable(true);
            a->setChecked(curLang == lang);
            langGroup->addAction(a);
            connect(a, &QAction::toggled, this, [lang](bool on) {
                if (on) purple_prefs_set_string(
                    "/konqix/input/spellcheck_lang",
                    lang.toUtf8().constData());
            });
        }
    }
#endif
    auto *notifyMenu = viewMenu->addMenu(tr("&Notifications"));
    auto addNotifyAction = [&](const QString &label, const char *pref) {
        auto *a = notifyMenu->addAction(label);
        a->setCheckable(true);
        a->setChecked(purple_prefs_get_bool(pref));
        connect(a, &QAction::toggled, this, [pref](bool on) {
            purple_prefs_set_bool(pref, on ? TRUE : FALSE);
        });
    };
    addNotifyAction(tr("Play &sound"),            "/konqix/notify/sound");
    addNotifyAction(tr("&Flash tray icon"),       "/konqix/notify/flash_tray");
    addNotifyAction(tr("Show notification &balloon"),
                                                  "/konqix/notify/balloon");
    addNotifyAction(tr("&Open conversation window on incoming"),
                                                  "/konqix/notify/open_conv");
    notifyMenu->addSeparator();
    auto *chooseSoundAct = notifyMenu->addAction(tr("Choose sound &file…"));
    connect(chooseSoundAct, &QAction::triggered, this, [this]() {
        QString cur = QString::fromUtf8(
            purple_prefs_get_string("/konqix/notify/sound_file"));
        QString startDir = cur.isEmpty()
            ? QStandardPaths::writableLocation(QStandardPaths::MusicLocation)
            : QFileInfo(cur).absolutePath();
        QString picked = QFileDialog::getOpenFileName(this,
            tr("Choose notification sound"), startDir,
            tr("Audio files (*.wav *.ogg *.oga *.flac *.mp3);;All files (*)"));
        if (!picked.isEmpty()) {
            purple_prefs_set_string("/konqix/notify/sound_file",
                                    picked.toUtf8().constData());
        }
    });
    auto *resetSoundAct = notifyMenu->addAction(tr("Use &built-in chime"));
    connect(resetSoundAct, &QAction::triggered, this, []() {
        purple_prefs_set_string("/konqix/notify/sound_file", "");
    });
    auto *testSoundAct = notifyMenu->addAction(tr("&Test sound"));
    connect(testSoundAct, &QAction::triggered, this, []() {
        if (auto *n = Notifier::instance()) n->playSound();
    });

    auto *historyMenu = menuBar()->addMenu(tr("Hi&story"));
    auto *historyAct = historyMenu->addAction(tr("Save &message history"));
    historyAct->setCheckable(true);
    historyAct->setChecked(purple_prefs_get_bool("/konqix/logging/enabled"));
    connect(historyAct, &QAction::toggled, this, [](bool on) {
        purple_prefs_set_bool("/konqix/logging/enabled", on ? TRUE : FALSE);
        // Mirror to libpurple's built-in logger.
        purple_prefs_set_bool("/purple/logging/log_ims", on ? TRUE : FALSE);
        purple_prefs_set_bool("/purple/logging/log_chats", on ? TRUE : FALSE);
    });

    auto *reindexAct = historyMenu->addAction(tr("Re&build search index…"));
    connect(reindexAct, &QAction::triggered, this, [this]() {
        if (QMessageBox::question(this, tr("Rebuild search index"),
                tr("Wipe the SQLite log index and rebuild it from scratch? "
                   "This re-parses every log file in ~/.purple/logs and may "
                   "take a minute or so. The History dialog will be empty "
                   "until it finishes."))
            == QMessageBox::Yes) {
            LogIndex::instance()->rebuildAll();
        }
    });

    auto *helpMenu = menuBar()->addMenu(tr("&Help"));
    auto *aboutAct = helpMenu->addAction(tr("&About"));
    connect(aboutAct, &QAction::triggered, this, [this]() {
        QMessageBox::about(this, tr("About Konqix"),
            tr("Konqix 0.1\n\nA Qt client built on libpurple."));
    });
}

void MainWindow::buildStatusBar()
{
    m_statusCombo = new QComboBox(this);
    m_statusCombo->setIconSize(QSize(16, 16));
    m_statusCombo->addItem(QIcon(QStringLiteral(":/icons/konqix.svg")),
                           tr("Available"), int(PURPLE_STATUS_AVAILABLE));
    m_statusCombo->addItem(QIcon(QStringLiteral(":/icons/konqix-away.svg")),
                           tr("Away"), int(PURPLE_STATUS_AWAY));
    m_statusCombo->addItem(QIcon(QStringLiteral(":/icons/konqix-xaway.svg")),
                           tr("Extended away"), int(PURPLE_STATUS_EXTENDED_AWAY));
    m_statusCombo->addItem(QIcon(QStringLiteral(":/icons/konqix-invisible.svg")),
                           tr("Invisible"), int(PURPLE_STATUS_INVISIBLE));
    m_statusCombo->addItem(QIcon(QStringLiteral(":/icons/konqix-offline.svg")),
                           tr("Offline"), int(PURPLE_STATUS_OFFLINE));
    // Read the user's last choice straight from our pref — not from
    // purple_savedstatus_get_current(), which a connecting prpl (Telegram
    // tdlib in particular) can clobber by feeding back the user's
    // remote-presence state. Then re-activate the saved status to make
    // sure our choice wins regardless of what happened between
    // PurpleCore::init and now.
    int curPrim = purple_prefs_get_int("/konqix/status/last_primitive");
    if (curPrim == 0) curPrim = int(PURPLE_STATUS_AVAILABLE);
    {
        QSignalBlocker block(m_statusCombo);
        for (int i = 0; i < m_statusCombo->count(); ++i) {
            if (m_statusCombo->itemData(i).toInt() == curPrim) {
                m_statusCombo->setCurrentIndex(i);
                break;
            }
        }
    }
    {
        auto prim = static_cast<PurpleStatusPrimitive>(curPrim);
        PurpleSavedStatus *st =
            purple_savedstatus_find_transient_by_type_and_message(prim, nullptr);
        if (!st) st = purple_savedstatus_new(nullptr, prim);
        purple_savedstatus_activate(st);
    }
    connect(m_statusCombo, &QComboBox::currentIndexChanged, this, [this](int) {
        changeStatus(m_statusCombo->currentData().toInt());
    });

    m_statusInfo = new QLabel(this);

    statusBar()->addPermanentWidget(new QLabel(tr("Status:")));
    statusBar()->addPermanentWidget(m_statusCombo);
    statusBar()->addWidget(m_statusInfo, 1);
    // 3 px right margin; Breeze's top separator line is suppressed by the
    // FlatStatusBarStyle in main.cpp so we don't need a QSS border override.
    statusBar()->setContentsMargins(0, 0, 3, 0);
    statusBar()->setSizeGripEnabled(false);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    saveGeometryToPrefs();
    // Hide to tray instead of quitting
    hide();
    event->ignore();
}

void MainWindow::moveEvent(QMoveEvent *event)
{
    QMainWindow::moveEvent(event);
    saveGeometryToPrefs();
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    saveGeometryToPrefs();
}

static PurpleConversation *openIm(QWidget *parent, PurpleAccount *acc, const char *name)
{
    if (!acc || !purple_account_is_connected(acc)) {
        QMessageBox::warning(parent, QObject::tr("Account offline"),
            QObject::tr("Account %1 is not connected. Set status to Available "
                        "and try again.")
                .arg(QString::fromUtf8(acc ? purple_account_get_username(acc) : "?")));
        return nullptr;
    }
    return purple_conversation_new(PURPLE_CONV_TYPE_IM, acc, name);
}

void MainWindow::onItemDoubleClicked(const QModelIndex &index)
{
    PurpleBlistNode *node = m_model->nodeFromIndex(index);
    if (!node) return;

    PurpleConversation *conv = nullptr;
    PurpleBlistNodeType type = purple_blist_node_get_type(node);
    if (type == PURPLE_BLIST_BUDDY_NODE) {
        PurpleBuddy *b = reinterpret_cast<PurpleBuddy *>(node);
        conv = openIm(this, purple_buddy_get_account(b), purple_buddy_get_name(b));
    } else if (type == PURPLE_BLIST_CONTACT_NODE) {
        PurpleContact *c = reinterpret_cast<PurpleContact *>(node);
        PurpleBuddy *b = purple_contact_get_priority_buddy(c);
        if (b)
            conv = openIm(this, purple_buddy_get_account(b), purple_buddy_get_name(b));
    } else if (type == PURPLE_BLIST_CHAT_NODE) {
        PurpleChat *c = reinterpret_cast<PurpleChat *>(node);
        PurpleAccount *acc = purple_chat_get_account(c);
        if (!acc || !purple_account_is_connected(acc)) {
            QMessageBox::warning(this, tr("Account offline"),
                tr("Account is not connected."));
            return;
        }
        serv_join_chat(purple_account_get_connection(acc),
                       purple_chat_get_components(c));
        return;
    }
    if (conv && ConversationManager::instance())
        ConversationManager::instance()->presentConversation(conv);
}

void MainWindow::onItemContextMenu(const QPoint &pos)
{
    QModelIndex idx = m_tree->indexAt(pos);
    if (!idx.isValid()) return;
    PurpleBlistNode *node = m_model->nodeFromIndex(idx);
    if (!node) return;

    QMenu menu(this);
    PurpleBlistNodeType type = purple_blist_node_get_type(node);

    if (type == PURPLE_BLIST_BUDDY_NODE || type == PURPLE_BLIST_CONTACT_NODE) {
        menu.addAction(tr("Show &conversation"), this,
            [this, idx]() { onItemDoubleClicked(idx); });
        menu.addAction(tr("Show &history…"), this, [this, node, type]() {
            PurpleBuddy *b = (type == PURPLE_BLIST_CONTACT_NODE)
                ? purple_contact_get_priority_buddy(reinterpret_cast<PurpleContact *>(node))
                : reinterpret_cast<PurpleBuddy *>(node);
            if (!b) return;
            auto *dlg = new HistoryDialog(PURPLE_LOG_IM,
                QString::fromUtf8(purple_buddy_get_name(b)),
                purple_buddy_get_account(b), this);
            dlg->setAttribute(Qt::WA_DeleteOnClose);
            dlg->show();
        });
        menu.addSeparator();
        // Move to group submenu: list existing groups + "New group..."
        QMenu *moveMenu = menu.addMenu(tr("&Move to group"));
        PurpleBuddy *buddyForMove = (type == PURPLE_BLIST_CONTACT_NODE)
            ? purple_contact_get_priority_buddy(reinterpret_cast<PurpleContact *>(node))
            : reinterpret_cast<PurpleBuddy *>(node);
        PurpleGroup *currentGroup = buddyForMove ? purple_buddy_get_group(buddyForMove) : nullptr;
        for (PurpleBlistNode *n = purple_blist_get_root(); n;
             n = purple_blist_node_get_sibling_next(n)) {
            if (purple_blist_node_get_type(n) != PURPLE_BLIST_GROUP_NODE) continue;
            PurpleGroup *g = reinterpret_cast<PurpleGroup *>(n);
            QString name = QString::fromUtf8(purple_group_get_name(g));
            auto *a = moveMenu->addAction(name);
            a->setEnabled(g != currentGroup);
            connect(a, &QAction::triggered, this, [this, buddyForMove, name]() {
                if (buddyForMove) moveBuddyToGroup(buddyForMove, name);
            });
        }
        moveMenu->addSeparator();
        moveMenu->addAction(tr("&New group…"), this, [this, buddyForMove]() {
            if (!buddyForMove) return;
            PurpleGroup *g = promptForNewGroup();
            if (g)
                moveBuddyToGroup(buddyForMove,
                    QString::fromUtf8(purple_group_get_name(g)));
        });
        menu.addSeparator();
        menu.addAction(tr("Get &info"), this, [this, node, type]() {
            PurpleBuddy *b = (type == PURPLE_BLIST_CONTACT_NODE)
                ? purple_contact_get_priority_buddy(reinterpret_cast<PurpleContact *>(node))
                : reinterpret_cast<PurpleBuddy *>(node);
            if (!b) return;
            PurpleAccount *acc = purple_buddy_get_account(b);
            if (!acc || !purple_account_is_connected(acc)) {
                QMessageBox::warning(this, tr("Account offline"),
                    tr("Account is not connected."));
                return;
            }
            serv_get_info(purple_account_get_connection(acc),
                          purple_buddy_get_name(b));
        });
        menu.addSeparator();
        menu.addAction(tr("&Remove buddy"), this, [this, node, type]() {
            PurpleBuddy *b = (type == PURPLE_BLIST_CONTACT_NODE)
                ? purple_contact_get_priority_buddy(reinterpret_cast<PurpleContact *>(node))
                : reinterpret_cast<PurpleBuddy *>(node);
            if (!b) return;
            if (QMessageBox::question(this, tr("Remove"),
                    tr("Remove %1?").arg(QString::fromUtf8(purple_buddy_get_name(b))))
                == QMessageBox::Yes) {
                purple_account_remove_buddy(purple_buddy_get_account(b), b,
                                            purple_buddy_get_group(b));
                purple_blist_remove_buddy(b);
                // libpurple's remove UI op fires for the buddy node but
                // doesn't separately update the parent group's display
                // (online/total counts) — force a model refresh so the
                // "(online/total)" suffix in the group header tracks.
                if (m_model) m_model->rebuild();
            }
        });
    } else if (type == PURPLE_BLIST_CHAT_NODE) {
        menu.addAction(tr("Show &conversation"), this,
            [this, idx]() { onItemDoubleClicked(idx); });
        menu.addAction(tr("Show &history…"), this, [this, node]() {
            PurpleChat *c = reinterpret_cast<PurpleChat *>(node);
            PurpleAccount *acc = purple_chat_get_account(c);
            // Log directories are keyed by the conversation identifier (room
            // id), not the human-readable alias. purple_chat_get_name()
            // returns the alias when one is set — the prpl's get_chat_name()
            // helper returns the real id, matching what
            // purple_conversation_get_name() would return for the live conv.
            QString convName;
            if (acc) {
                if (PurplePlugin *plug = purple_find_prpl(
                        purple_account_get_protocol_id(acc))) {
                    PurplePluginProtocolInfo *prpl = PURPLE_PLUGIN_PROTOCOL_INFO(plug);
                    if (prpl && prpl->get_chat_name) {
                        char *n = prpl->get_chat_name(purple_chat_get_components(c));
                        if (n) {
                            convName = QString::fromUtf8(n);
                            g_free(n);
                        }
                    }
                }
            }
            if (convName.isEmpty())
                convName = QString::fromUtf8(purple_chat_get_name(c));
            auto *dlg = new HistoryDialog(PURPLE_LOG_CHAT, convName, acc, this);
            dlg->setAttribute(Qt::WA_DeleteOnClose);
            dlg->show();
        });
    } else if (type == PURPLE_BLIST_GROUP_NODE) {
        menu.addAction(tr("Rename group"), this, [this, node]() {
            PurpleGroup *g = reinterpret_cast<PurpleGroup *>(node);
            bool ok = false;
            QString name = QInputDialog::getText(this, tr("Rename"), tr("New name:"),
                QLineEdit::Normal,
                QString::fromUtf8(purple_group_get_name(g)), &ok);
            if (ok && !name.isEmpty())
                purple_blist_rename_group(g, name.toUtf8().constData());
        });
    }

    if (!menu.actions().isEmpty())
        menu.exec(m_tree->viewport()->mapToGlobal(pos));
}

void MainWindow::onShowOfflineToggled(bool checked)
{
    m_model->setShowOffline(checked);
    purple_prefs_set_bool("/konqix/blist/show_offline", checked ? TRUE : FALSE);
    expandTopGroups();
}

void MainWindow::onShowAwayToggled(bool checked)
{
    m_model->setShowAway(checked);
    purple_prefs_set_bool("/konqix/blist/show_away", checked ? TRUE : FALSE);
    expandTopGroups();
}

void MainWindow::showAccountsDialog()
{
    AccountsDialog dlg(this);
    dlg.exec();
}

PurpleGroup *MainWindow::promptForNewGroup()
{
    bool ok = false;
    QString name = QInputDialog::getText(this, tr("New group"),
        tr("Group name:"), QLineEdit::Normal, {}, &ok).trimmed();
    if (!ok || name.isEmpty())
        return nullptr;
    QByteArray utf = name.toUtf8();
    PurpleGroup *g = purple_find_group(utf.constData());
    if (!g) {
        g = purple_group_new(utf.constData());
        purple_blist_add_group(g, nullptr);
    }
    return g;
}

void MainWindow::addGroup()
{
    promptForNewGroup();
    expandTopGroups();
}

void MainWindow::moveBuddyToGroup(PurpleBuddy *buddy, const QString &groupName)
{
    if (!buddy) return;
    QByteArray nameUtf = groupName.toUtf8();
    PurpleGroup *g = purple_find_group(nameUtf.constData());
    if (!g) {
        g = purple_group_new(nameUtf.constData());
        purple_blist_add_group(g, nullptr);
    }
    // purple_blist_add_buddy() with the buddy already on the list moves it
    // to the target group (and emits the right blist signals).
    purple_blist_add_buddy(buddy, nullptr, g, nullptr);
    if (m_model) m_model->rebuild();   // refresh both old + new group headers
    expandTopGroups();
}

void MainWindow::showAddBuddyDialog()
{
    GList *accounts = purple_accounts_get_all_active();
    if (!accounts) {
        QMessageBox::information(this, tr("Add Buddy"),
                                 tr("Enable at least one account first."));
        return;
    }

    QStringList accountStrings;
    QList<PurpleAccount *> accountList;
    for (GList *l = accounts; l; l = l->next) {
        auto *acc = static_cast<PurpleAccount *>(l->data);
        accountStrings << QStringLiteral("%1 (%2)")
                          .arg(QString::fromUtf8(purple_account_get_username(acc)),
                               QString::fromUtf8(purple_account_get_protocol_name(acc)));
        accountList << acc;
    }

    bool ok = false;
    QString accSel = QInputDialog::getItem(this, tr("Add Buddy"), tr("Account:"),
                                           accountStrings, 0, false, &ok);
    if (!ok) return;
    int idx = accountStrings.indexOf(accSel);
    PurpleAccount *acc = accountList.value(idx);
    if (!acc) return;

    QString name = QInputDialog::getText(this, tr("Add Buddy"),
                                          tr("Username:"), QLineEdit::Normal, {}, &ok);
    if (!ok || name.isEmpty()) return;

    QString group = QInputDialog::getText(this, tr("Add Buddy"),
                                           tr("Group:"), QLineEdit::Normal,
                                           QStringLiteral("Buddies"), &ok);
    if (!ok) return;

    PurpleGroup *grp = purple_find_group(group.toUtf8().constData());
    if (!grp) {
        grp = purple_group_new(group.toUtf8().constData());
        purple_blist_add_group(grp, nullptr);
    }

    PurpleBuddy *b = purple_buddy_new(acc, name.toUtf8().constData(), nullptr);
    purple_blist_add_buddy(b, nullptr, grp, nullptr);
    purple_account_add_buddy(acc, b);
    // Same reason as remove: ensure the parent group's (online/total)
    // header refreshes immediately, even though libpurple's update UI
    // op fires on the buddy node itself.
    if (m_model) m_model->rebuild();
}

void MainWindow::changeStatus(int statusType)
{
    PurpleStatusPrimitive prim = static_cast<PurpleStatusPrimitive>(statusType);
    PurpleSavedStatus *st =
        purple_savedstatus_find_transient_by_type_and_message(prim, nullptr);
    if (!st)
        st = purple_savedstatus_new(nullptr, prim);
    purple_savedstatus_activate(st);

    // Persist so the next launch restores this status.
    purple_prefs_set_int("/konqix/status/last_primitive", int(prim));

    // Re-check missing-plugin warning whenever the user toggles status —
    // typical scenario: started offline (no warning needed), switched to
    // Available, accounts try to connect, missing protocols surface.
    if (prim == PURPLE_STATUS_OFFLINE)
        m_warningBanner->hide();
    else
        QTimer::singleShot(0, this, &MainWindow::warnAboutMissingProtocols);

    // Swap the tray + title-bar icon to match the new status.
    setWindowIcon(iconForStatusPrimitive(int(prim)));
    if (auto *n = Notifier::instance())
        n->setStatusPrimitive(int(prim));
}

} // namespace konqix
