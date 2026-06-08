// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Radek Bucek

#include "LogIndex.h"

#include "LogFormat.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTextDocument>
#include <QTimer>
#include <QUrl>

#include <limits>

extern "C" {
#include <libpurple/account.h>
#include <libpurple/prpl.h>
#include <libpurple/plugin.h>
#include <libpurple/util.h>
}

namespace konqix {

namespace {

// Bump when on-disk schema changes; rows are wiped + rebuilt on mismatch.
// v2: xfer-narration system rows are no longer indexed.
constexpr int SCHEMA_VERSION = 2;

QString accountUserDecode(const QString &enc)
{
    return QString::fromUtf8(QByteArray::fromPercentEncoding(enc.toUtf8()));
}

// libpurple html_logger filename: "YYYY-MM-DD.HHMMSS<TZ><tzname>.html"
QDateTime parseFilenameDate(const QString &filename)
{
    static const QRegularExpression re(
        QStringLiteral(R"(^(\d{4})-(\d{2})-(\d{2})\.(\d{2})(\d{2})(\d{2}))"));
    auto m = re.match(filename);
    if (!m.hasMatch()) return {};
    QDate d(m.captured(1).toInt(), m.captured(2).toInt(), m.captured(3).toInt());
    QTime t(m.captured(4).toInt(), m.captured(5).toInt(), m.captured(6).toInt());
    return QDateTime(d, t);
}

QDateTime stampForLine(const QDateTime &fileStart, const QString &timeText)
{
    static const QRegularExpression re(
        QStringLiteral(R"(^(\d{1,2}):(\d{2})(?::(\d{2}))?$)"));
    auto m = re.match(timeText.trimmed());
    if (!m.hasMatch()) return fileStart;
    QTime t(m.captured(1).toInt(), m.captured(2).toInt(),
            m.captured(3).isEmpty() ? 0 : m.captured(3).toInt());
    return QDateTime(fileStart.date(), t);
}

bool decomposePath(const QString &absPath, const QString &logRoot,
                   QString *outAccount, PurpleLogType *outType, QString *outConvName,
                   QString *outFileName)
{
    if (!absPath.startsWith(logRoot + QLatin1Char('/')))
        return false;
    QString rel = absPath.mid(logRoot.size() + 1);
    QStringList parts = rel.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (parts.size() < 4) return false;

    QString protocol    = parts.at(0);
    QString accountUser = accountUserDecode(parts.at(1));
    QString convDirDec  = accountUserDecode(parts.at(2));
    *outFileName        = parts.last();

    *outAccount = protocol + QLatin1Char('/') + accountUser;

    if (convDirDec.endsWith(QLatin1String(".chat"))) {
        *outType = PURPLE_LOG_CHAT;
        convDirDec.chop(5);
    } else {
        *outType = PURPLE_LOG_IM;
    }
    *outConvName = convDirDec;
    return true;
}

// Translate user filter words into an FTS5 MATCH expression. Each word
// is sanitized (FTS5 syntax chars stripped) and given a trailing `*`
// so partial-word typing also matches.
QString buildFtsExpr(const QStringList &words)
{
    QStringList terms;
    static const QRegularExpression bad(QStringLiteral("[\"()*:^]"));
    for (const QString &w : words) {
        QString s = w;
        s.remove(bad);
        s = s.trimmed();
        if (s.isEmpty()) continue;
        terms << s + QLatin1Char('*');
    }
    return terms.join(QLatin1Char(' '));
}

} // namespace

static LogIndex *g_instance = nullptr;

LogIndex *LogIndex::instance()
{
    if (!g_instance)
        g_instance = new LogIndex(QCoreApplication::instance());
    return g_instance;
}

QString LogIndex::accountKey(PurpleAccount *acc)
{
    if (!acc) return {};
    // Use the prpl's list_icon() result — that's the string libpurple
    // uses when building log directory paths (e.g. "telegram" for the
    // telegram-tdlib prpl whose protocol_id is "telegram-tdlib"), so the
    // key matches what the path-walking indexer stored.
    QString proto;
    if (PurplePlugin *plugin =
            purple_find_prpl(purple_account_get_protocol_id(acc))) {
        if (auto *info = PURPLE_PLUGIN_PROTOCOL_INFO(plugin)) {
            if (info->list_icon) {
                const char *icon = info->list_icon(acc, nullptr);
                if (icon) proto = QString::fromUtf8(icon);
            }
        }
    }
    if (proto.isEmpty())
        proto = QString::fromUtf8(purple_account_get_protocol_id(acc));
    return proto + QLatin1Char('/')
         + QString::fromUtf8(purple_account_get_username(acc));
}

LogIndex::LogIndex(QObject *parent)
    : QObject(parent)
{
    m_logRoot = QDir::homePath() + QStringLiteral("/.purple/logs");

    QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    QDir().mkpath(cacheDir);
    m_dbPath = cacheDir + QStringLiteral("/log-index.sqlite");

    openDb();
    if (m_db.isOpen())
        createSchema();

    m_batchTimer = new QTimer(this);
    m_batchTimer->setSingleShot(true);
    m_batchTimer->setInterval(0);
    connect(m_batchTimer, &QTimer::timeout, this, &LogIndex::processBatch);
}

LogIndex::~LogIndex()
{
    if (m_db.isOpen()) m_db.close();
    QSqlDatabase::removeDatabase(QStringLiteral("konqix-logindex"));
}

void LogIndex::openDb()
{
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                     QStringLiteral("konqix-logindex"));
    m_db.setDatabaseName(m_dbPath);
    if (!m_db.open()) {
        qWarning("LogIndex: cannot open db at %s: %s",
                 qUtf8Printable(m_dbPath),
                 qUtf8Printable(m_db.lastError().text()));
        return;
    }
    QSqlQuery q(m_db);
    q.exec(QStringLiteral("PRAGMA journal_mode=WAL"));
    q.exec(QStringLiteral("PRAGMA synchronous=NORMAL"));
    q.exec(QStringLiteral("PRAGMA foreign_keys=ON"));
}

void LogIndex::createSchema()
{
    QSqlQuery q(m_db);

    int cur = 0;
    if (q.exec(QStringLiteral("PRAGMA user_version")) && q.next())
        cur = q.value(0).toInt();
    if (cur != 0 && cur != SCHEMA_VERSION) {
        q.exec(QStringLiteral("DROP TABLE IF EXISTS messages_fts"));
        q.exec(QStringLiteral("DROP TABLE IF EXISTS messages"));
        q.exec(QStringLiteral("DROP TABLE IF EXISTS files"));
    }

    q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS files ("
        "  id INTEGER PRIMARY KEY,"
        "  path TEXT UNIQUE NOT NULL,"
        "  mtime INTEGER NOT NULL,"
        "  size INTEGER NOT NULL"
        ")"));
    q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS messages ("
        "  id INTEGER PRIMARY KEY,"
        "  file_id INTEGER NOT NULL REFERENCES files(id) ON DELETE CASCADE,"
        "  account TEXT NOT NULL,"
        "  conv_type INTEGER NOT NULL,"
        "  conv_name TEXT NOT NULL,"
        "  ts INTEGER NOT NULL,"
        "  color TEXT,"
        "  sender TEXT,"
        "  body_html TEXT,"
        "  body_text TEXT"
        ")"));
    q.exec(QStringLiteral(
        "CREATE INDEX IF NOT EXISTS idx_messages_conv "
        "ON messages(account, conv_type, conv_name, ts)"));
    q.exec(QStringLiteral(
        "CREATE VIRTUAL TABLE IF NOT EXISTS messages_fts USING fts5("
        "  body_text, content='messages', content_rowid='id', tokenize='unicode61'"
        ")"));
    // FTS triggers — we INSERT new rows and DELETE old rows (no UPDATE).
    q.exec(QStringLiteral(
        "CREATE TRIGGER IF NOT EXISTS messages_ai AFTER INSERT ON messages "
        "BEGIN INSERT INTO messages_fts(rowid, body_text) "
        "VALUES (new.id, new.body_text); END"));
    q.exec(QStringLiteral(
        "CREATE TRIGGER IF NOT EXISTS messages_ad AFTER DELETE ON messages "
        "BEGIN INSERT INTO messages_fts(messages_fts, rowid, body_text) "
        "VALUES ('delete', old.id, old.body_text); END"));

    q.exec(QStringLiteral("PRAGMA user_version = %1").arg(SCHEMA_VERSION));
}

void LogIndex::rebuildAll()
{
    if (m_busy || !m_db.isOpen()) return;
    QSqlQuery q(m_db);
    q.exec(QStringLiteral("DROP TABLE IF EXISTS messages_fts"));
    q.exec(QStringLiteral("DROP TABLE IF EXISTS messages"));
    q.exec(QStringLiteral("DROP TABLE IF EXISTS files"));
    createSchema();
    m_ready = false;
    startIndexing();
}

void LogIndex::startIndexing()
{
    if (m_busy || !m_db.isOpen()) return;
    m_busy = true;
    scanAllPaths();
    if (m_pendingFiles.isEmpty()) {
        cleanupDeleted();
        m_ready = true;
        m_busy  = false;
        emit ready();
        return;
    }
    emit progressChanged(0, m_totalFiles);
    m_batchTimer->start();
}

void LogIndex::scanAllPaths()
{
    m_pendingFiles.clear();
    QDirIterator it(m_logRoot, QStringList() << QStringLiteral("*.html"),
                    QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) m_pendingFiles.append(it.next());
    m_totalFiles = m_pendingFiles.size();
    m_doneFiles  = 0;
}

void LogIndex::processBatch()
{
    constexpr int BATCH = 64;
    m_db.transaction();
    int processed = 0;
    while (processed < BATCH && !m_pendingFiles.isEmpty()) {
        QString path = m_pendingFiles.takeFirst();
        indexFile(path);
        ++m_doneFiles;
        ++processed;
    }
    m_db.commit();
    emit progressChanged(m_doneFiles, m_totalFiles);
    if (!m_pendingFiles.isEmpty()) {
        m_batchTimer->start();
    } else {
        cleanupDeleted();
        m_ready = true;
        m_busy  = false;
        emit ready();
    }
}

void LogIndex::indexFile(const QString &path)
{
    QFileInfo fi(path);
    qint64 mt = fi.lastModified().toSecsSinceEpoch();
    qint64 sz = fi.size();

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT id, mtime, size FROM files WHERE path = ?"));
    q.addBindValue(path);
    if (!q.exec()) return;
    qint64 existingId = -1;
    if (q.next()) {
        existingId = q.value(0).toLongLong();
        if (q.value(1).toLongLong() == mt && q.value(2).toLongLong() == sz)
            return;  // unchanged → skip
        QSqlQuery del(m_db);
        del.prepare(QStringLiteral("DELETE FROM messages WHERE file_id = ?"));
        del.addBindValue(existingId);
        del.exec();
    }

    QString account, convName, fileName;
    PurpleLogType convType = PURPLE_LOG_IM;
    if (!decomposePath(path, m_logRoot, &account, &convType, &convName, &fileName))
        return;

    QDateTime fileStart = parseFilenameDate(fileName);
    if (!fileStart.isValid()) fileStart = fi.lastModified();

    // Upsert files row, then resolve file_id.
    QSqlQuery upsert(m_db);
    upsert.prepare(QStringLiteral(
        "INSERT INTO files(path, mtime, size) VALUES (?, ?, ?) "
        "ON CONFLICT(path) DO UPDATE SET mtime=excluded.mtime, size=excluded.size"));
    upsert.addBindValue(path);
    upsert.addBindValue(mt);
    upsert.addBindValue(sz);
    if (!upsert.exec()) return;

    qint64 fileId = existingId;
    if (fileId < 0) {
        QSqlQuery sel(m_db);
        sel.prepare(QStringLiteral("SELECT id FROM files WHERE path = ?"));
        sel.addBindValue(path);
        if (!sel.exec() || !sel.next()) return;
        fileId = sel.value(0).toLongLong();
    }

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return;
    QString html = QString::fromUtf8(f.readAll());
    f.close();

    // Strip the <h1>Conversation with…</h1> preamble + the surrounding <p>
    // wrappers (same as LogFormat::reformatLogMessages does for display).
    html.remove(QRegularExpression(QStringLiteral("<h1>.*?</h1>"),
                                   QRegularExpression::CaseInsensitiveOption
                                       | QRegularExpression::DotMatchesEverythingOption));
    html.remove(QRegularExpression(QStringLiteral("</?p[^>]*>"),
                                   QRegularExpression::CaseInsensitiveOption));

    const QString convDir = fi.absolutePath();

    static const QRegularExpression sepRe(QStringLiteral("<br\\s*/?\\s*>\\s*\\n+"),
                                          QRegularExpression::CaseInsensitiveOption);

    QSqlQuery insMsg(m_db);
    insMsg.prepare(QStringLiteral(
        "INSERT INTO messages(file_id, account, conv_type, conv_name, ts, "
        "color, sender, body_html, body_text) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)"));

    for (const QString &line : html.split(sepRe)) {
        ParsedLogLine p = parseLogLine(line);
        if (!p.matched) continue;
        // Skip libpurple's xfer narration — the inline image already
        // represents the transfer in the rendered view; the system lines
        // are pure noise.
        if (!p.hasSender && isTransferNoise(p.bodyRaw)) continue;

        // Image resolution + newline normalisation: this matches what
        // the live ConversationWindow does at render time, so the index
        // already holds final-renderable HTML.
        QString body = p.bodyRaw;
        // Drop in-line <img id="N"> refs: imgstore is empty post-restart
        // so we can't materialise the bytes. Leave a placeholder so the
        // body_text still has *something* to surface in search.
        body.replace(QRegularExpression(
                         QStringLiteral(R"(<img\s+[^>]*\bid\s*=\s*['"]?\d+['"]?[^>]*>)"),
                         QRegularExpression::CaseInsensitiveOption),
                     QStringLiteral("[image]"));
        body = resolveImgPaths(body, convDir);
        body = rewriteImageLinks(body);
        body = convertNewlines(body).trimmed();

        QTextDocument doc;
        doc.setHtml(body);
        QString bodyText = doc.toPlainText().trimmed();

        qint64 ts = stampForLine(fileStart, p.time).toSecsSinceEpoch();

        insMsg.bindValue(0, fileId);
        insMsg.bindValue(1, account);
        insMsg.bindValue(2, int(convType));
        insMsg.bindValue(3, convName);
        insMsg.bindValue(4, ts);
        insMsg.bindValue(5, p.hasSender ? QVariant(p.color) : QVariant());
        insMsg.bindValue(6, p.hasSender ? QVariant(p.sender) : QVariant());
        insMsg.bindValue(7, body);
        insMsg.bindValue(8, bodyText);
        insMsg.exec();
    }
}

void LogIndex::cleanupDeleted()
{
    QSqlQuery q(m_db);
    if (!q.exec(QStringLiteral("SELECT id, path FROM files"))) return;
    QList<qint64> dead;
    while (q.next()) {
        QString path = q.value(1).toString();
        if (!QFileInfo::exists(path))
            dead.append(q.value(0).toLongLong());
    }
    if (dead.isEmpty()) return;
    m_db.transaction();
    QSqlQuery del(m_db);
    del.prepare(QStringLiteral("DELETE FROM files WHERE id = ?"));
    for (qint64 id : dead) {
        del.bindValue(0, id);
        del.exec();
    }
    m_db.commit();
}

void LogIndex::touchConv(const QString &account, PurpleLogType type, const QString &name)
{
    // Cheap re-scan of the conv's directory; only changed files are
    // actually reparsed (mtime+size check inside indexFile).
    if (!m_db.isOpen()) return;

    // The conv-dir layout: <logRoot>/<protocol>/<account-enc>/<convname[.chat]>
    int slash = account.indexOf(QLatin1Char('/'));
    if (slash < 0) return;
    QString protocol = account.left(slash);
    QString user     = account.mid(slash + 1);

    // libpurple writes log dir paths with its own purple_escape_filename
    // (lowercase %xx), which doesn't match Qt's QUrl::toPercentEncoding
    // (uppercase, different safe-set). Use the same function so the path
    // we stat resolves to the real directory.
    auto escape = [](const QString &s) {
        QByteArray utf = s.toUtf8();
        return QString::fromUtf8(purple_escape_filename(utf.constData()));
    };
    QString convDirName = (type == PURPLE_LOG_CHAT) ? name + QStringLiteral(".chat") : name;
    QString convDir = m_logRoot + QLatin1Char('/') + protocol
                    + QLatin1Char('/') + escape(user)
                    + QLatin1Char('/') + escape(convDirName);

    QDir d(convDir);
    if (!d.exists()) return;
    const auto files = d.entryInfoList(QStringList() << QStringLiteral("*.html"),
                                       QDir::Files);
    m_db.transaction();
    for (const QFileInfo &fi : files)
        indexFile(fi.absoluteFilePath());
    m_db.commit();
}

QList<QDate> LogIndex::datesForConv(const QString &account, PurpleLogType type,
                                    const QString &name, const QStringList &filterWords)
{
    QList<QDate> result;
    if (!m_db.isOpen()) return result;

    QString fts = buildFtsExpr(filterWords);
    QString sql = QStringLiteral(
        "SELECT DISTINCT strftime('%Y-%m-%d', m.ts, 'unixepoch', 'localtime') AS d "
        "FROM messages m ");
    if (!fts.isEmpty())
        sql += QStringLiteral("JOIN messages_fts f ON f.rowid = m.id "
                              "WHERE f.body_text MATCH ? AND ");
    else
        sql += QStringLiteral("WHERE ");
    sql += QStringLiteral("m.account = ? AND m.conv_type = ? AND m.conv_name = ? "
                          "ORDER BY d DESC");

    QSqlQuery q(m_db);
    q.prepare(sql);
    int idx = 0;
    if (!fts.isEmpty()) q.bindValue(idx++, fts);
    q.bindValue(idx++, account);
    q.bindValue(idx++, int(type));
    q.bindValue(idx++, name);
    if (!q.exec()) {
        qWarning("LogIndex::datesForConv: %s", qUtf8Printable(q.lastError().text()));
        return result;
    }
    while (q.next()) {
        QDate d = QDate::fromString(q.value(0).toString(), Qt::ISODate);
        if (d.isValid()) result.append(d);
    }
    return result;
}

QList<IndexedMessage> LogIndex::messagesForConv(const QString &account, PurpleLogType type,
                                                const QString &name,
                                                qint64 fromTs, qint64 toTs,
                                                const QStringList &filterWords)
{
    QList<IndexedMessage> result;
    if (!m_db.isOpen()) return result;

    QString fts = buildFtsExpr(filterWords);
    QString sql = QStringLiteral(
        "SELECT m.ts, m.color, m.sender, m.body_html, m.body_text "
        "FROM messages m ");
    if (!fts.isEmpty())
        sql += QStringLiteral("JOIN messages_fts f ON f.rowid = m.id "
                              "WHERE f.body_text MATCH ? AND ");
    else
        sql += QStringLiteral("WHERE ");
    sql += QStringLiteral(
        "m.account = ? AND m.conv_type = ? AND m.conv_name = ? "
        "AND m.ts >= ? AND m.ts <= ? "
        "ORDER BY m.ts ASC");

    QSqlQuery q(m_db);
    q.prepare(sql);
    int idx = 0;
    if (!fts.isEmpty()) q.bindValue(idx++, fts);
    q.bindValue(idx++, account);
    q.bindValue(idx++, int(type));
    q.bindValue(idx++, name);
    q.bindValue(idx++, fromTs);
    q.bindValue(idx++, toTs);
    if (!q.exec()) {
        qWarning("LogIndex::messagesForConv: %s", qUtf8Printable(q.lastError().text()));
        return result;
    }
    while (q.next()) {
        IndexedMessage m;
        m.ts       = q.value(0).toLongLong();
        m.color    = q.value(1).toString();
        m.sender   = q.value(2).toString();
        m.bodyHtml = q.value(3).toString();
        m.bodyText = q.value(4).toString();
        result.append(m);
    }
    return result;
}

} // namespace konqix
