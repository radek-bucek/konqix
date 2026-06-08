// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Radek Bucek

#pragma once

#include <QDate>
#include <QList>
#include <QObject>
#include <QSqlDatabase>
#include <QString>
#include <QStringList>

#include <ctime>

extern "C" {
#include <libpurple/account.h>
#include <libpurple/log.h>
}

class QFileSystemWatcher;
class QTimer;

namespace konqix {

// One message as stored in the SQLite log mirror. Designed to be passed
// directly to HistoryDialog's renderer.
struct IndexedMessage {
    qint64  ts = 0;     // unix epoch seconds (local-time interpretation)
    QString color;      // hex from log span; empty for system lines
    QString sender;     // empty for system lines
    QString bodyHtml;   // body with images resolved + newlines as <br>
    QString bodyText;   // plain text body (filter/highlight source)
};

// Mirror of libpurple's HTML log files in a SQLite + FTS5 database.
//
// The on-disk HTML log stays authoritative. This index exists so the
// History dialog can run full-archive search, tree filtering, and
// highlight without rescanning thousands of files on every keystroke.
class LogIndex : public QObject
{
    Q_OBJECT
public:
    static LogIndex *instance();

    // Build the same account key the index stores: "<protocol>/<username>".
    static QString accountKey(PurpleAccount *acc);

    void startIndexing();

    // Drop every indexed row and re-scan the entire log tree from scratch.
    // Useful when the user suspects the index drifted from disk.
    void rebuildAll();

    bool isReady() const { return m_ready; }

    // Re-stat / re-parse one conv's log directory — called when a live
    // message lands so the index picks up the freshly-appended line.
    void touchConv(const QString &account, PurpleLogType type, const QString &name);

    // Distinct local dates that hold at least one matching message.
    QList<QDate> datesForConv(const QString &account, PurpleLogType type,
                              const QString &name, const QStringList &filterWords);

    // All messages for the conv between fromTs and toTs (inclusive). Pass
    // 0 / std::numeric_limits<qint64>::max() for an unbounded range.
    QList<IndexedMessage> messagesForConv(const QString &account, PurpleLogType type,
                                          const QString &name,
                                          qint64 fromTs, qint64 toTs,
                                          const QStringList &filterWords);

signals:
    void progressChanged(int done, int total);
    void ready();

private:
    explicit LogIndex(QObject *parent = nullptr);
    ~LogIndex() override;

    void openDb();
    void createSchema();
    void scanAllPaths();
    void processBatch();
    void indexFile(const QString &path);
    void cleanupDeleted();

    QString      m_logRoot;
    QString      m_dbPath;
    QSqlDatabase m_db;
    bool         m_ready = false;
    bool         m_busy  = false;
    QStringList  m_pendingFiles;
    int          m_totalFiles = 0;
    int          m_doneFiles  = 0;
    QTimer      *m_batchTimer = nullptr;
};

} // namespace konqix
