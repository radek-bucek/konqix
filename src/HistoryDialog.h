// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Radek Bucek

#pragma once

#include <QDate>
#include <QDialog>
#include <QSet>
#include <QStringList>

extern "C" {
#include <libpurple/account.h>
#include <libpurple/log.h>
}

class QTreeWidget;
class QTreeWidgetItem;
class QTextBrowser;
class QLabel;
class QLineEdit;
class QTimer;

namespace konqix {

class LogIndex;

// Browses logged conversations with a single buddy/chat, grouped year →
// month → day. Search hits across the whole archive: matches filter the
// tree (showing only days that contain a match), highlight inline in the
// rendered HTML on the right.
class HistoryDialog : public QDialog
{
    Q_OBJECT
public:
    HistoryDialog(PurpleLogType type, const QString &name,
                  PurpleAccount *acc, QWidget *parent = nullptr);
    ~HistoryDialog() override;

private slots:
    void onSelectionChanged();
    void applyFilter();
    void onIndexProgress(int done, int total);
    void onIndexReady();

private:
    void buildTree();
    void loadSelection();
    QString renderMessages(const QList<struct IndexedMessage> &msgs) const;
    QString highlightBody(QString html) const;
    void updateTreeVisibility();
    void selectFirstVisibleDay();

    // Convert (account, type, name) for LogIndex queries.
    QString m_accountKey;
    QString m_convName;
    PurpleLogType m_type;
    QString m_displayName;
    QString m_selfAlias;   // account alias (e.g. "Radek Bucek"). Used in
                           // chat history to override the per-sender hash
                           // with the self colour #16569E.

    QTreeWidget *m_tree = nullptr;
    QLineEdit *m_search = nullptr;
    QTimer *m_debounce = nullptr;
    QTextBrowser *m_view = nullptr;
    QLabel *m_emptyLabel = nullptr;

    QStringList m_filterWords;     // current search words
    QSet<QDate> m_matchingDates;   // empty when no filter active
    bool m_filterActive = false;
};

} // namespace konqix
