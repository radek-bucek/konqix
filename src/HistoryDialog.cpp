// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Radek Bucek

#include "HistoryDialog.h"
#include "LogFormat.h"
#include "LogIndex.h"
#include "SmoothBrowser.h"

#include <QDateTime>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMap>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSplitter>
#include <QTextBrowser>
#include <QTextDocument>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>
#include <limits>

extern "C" {
#include <libpurple/blist.h>
}

namespace konqix {

namespace {

enum NodeKind { NodeYear = 1, NodeMonth, NodeDay };

constexpr int RoleKind  = Qt::UserRole + 1;
constexpr int RoleYear  = Qt::UserRole + 2;
constexpr int RoleMonth = Qt::UserRole + 3;
constexpr int RoleDay   = Qt::UserRole + 4;

QString resolveDisplayName(PurpleLogType type, const QString &name, PurpleAccount *acc)
{
    if (!acc) return name;
    QByteArray nameUtf = name.toUtf8();
    if (type == PURPLE_LOG_IM) {
        if (PurpleBuddy *b = purple_find_buddy(acc, nameUtf.constData())) {
            const char *alias = purple_buddy_get_alias(b);
            if (alias && *alias) return QString::fromUtf8(alias);
        }
    } else if (type == PURPLE_LOG_CHAT) {
        if (PurpleChat *c = purple_blist_find_chat(acc, nameUtf.constData())) {
            const char *alias = purple_chat_get_name(c);
            if (alias && *alias) return QString::fromUtf8(alias);
        }
    }
    return name;
}

} // namespace

HistoryDialog::HistoryDialog(PurpleLogType type, const QString &name,
                             PurpleAccount *acc, QWidget *parent)
    : QDialog(parent), m_convName(name), m_type(type)
{
    m_accountKey  = LogIndex::accountKey(acc);
    m_displayName = resolveDisplayName(type, name, acc);
    if (acc) {
        const char *alias = purple_account_get_alias(acc);
        if (alias && *alias) m_selfAlias = QString::fromUtf8(alias);
    }

    setWindowTitle(tr("History — %1").arg(m_displayName));
    resize(900, 600);

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(4, 4, 4, 4);

    auto *splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setHandleWidth(0);

    m_tree = new QTreeWidget(splitter);
    m_tree->setHeaderHidden(true);
    m_tree->setRootIsDecorated(true);
    m_tree->setMinimumWidth(200);
    m_tree->setMaximumWidth(320);
    m_tree->setStyleSheet(QStringLiteral(
        "QTreeWidget {"
        "  border: 1px solid palette(mid);"
        "  border-radius: 2px;"
        "  background: palette(base);"
        "}"));
    splitter->addWidget(m_tree);

    auto *right = new QWidget(splitter);
    auto *rightLay = new QVBoxLayout(right);
    rightLay->setContentsMargins(3, 0, 0, 0);
    rightLay->setSpacing(3);

    m_search = new QLineEdit(right);
    m_search->setPlaceholderText(tr("Search messages (space = AND)…"));
    m_search->setClearButtonEnabled(true);
    rightLay->addWidget(m_search);

    m_emptyLabel = new QLabel(tr("Select a year, month or day on the left to view the conversation history."), right);
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->setStyleSheet(QStringLiteral("color: #888"));
    rightLay->addWidget(m_emptyLabel, 1);

    m_view = new QTextBrowser(right);
    m_view->setDocument(new SmoothImageDocument(m_view));
    m_view->setOpenExternalLinks(true);
    m_view->hide();
    rightLay->addWidget(m_view, 1);

    splitter->addWidget(right);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    outer->addWidget(splitter, 1);

    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(500);
    connect(m_debounce, &QTimer::timeout, this, &HistoryDialog::applyFilter);

    connect(m_search, &QLineEdit::textChanged, this, [this](const QString &t) {
        if (t.isEmpty()) {
            m_debounce->stop();
            applyFilter();
        } else {
            m_debounce->start();
        }
    });

    connect(m_tree, &QTreeWidget::itemSelectionChanged,
            this, &HistoryDialog::onSelectionChanged);

    LogIndex *li = LogIndex::instance();
    if (li->isReady()) {
        buildTree();
    } else {
        m_emptyLabel->setText(tr("Indexing log archive… 0 / ?"));
        connect(li, &LogIndex::progressChanged,
                this, &HistoryDialog::onIndexProgress);
        connect(li, &LogIndex::ready,
                this, &HistoryDialog::onIndexReady);
    }
}

HistoryDialog::~HistoryDialog() = default;

void HistoryDialog::onIndexProgress(int done, int total)
{
    if (!isVisible()) return;
    m_emptyLabel->setText(tr("Indexing log archive… %1 / %2").arg(done).arg(total));
}

void HistoryDialog::onIndexReady()
{
    buildTree();
}

void HistoryDialog::buildTree()
{
    m_tree->clear();

    QList<QDate> dates = LogIndex::instance()->datesForConv(
        m_accountKey, m_type, m_convName, {});

    if (dates.isEmpty()) {
        auto *item = new QTreeWidgetItem(m_tree, {tr("No history")});
        item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
        m_emptyLabel->setText(tr("No conversation history found."));
        m_emptyLabel->show();
        m_view->hide();
        return;
    }

    QMap<int, QMap<int, QSet<int>>> buckets;
    for (const QDate &d : dates)
        buckets[d.year()][d.month()].insert(d.day());

    QList<int> years = buckets.keys();
    std::sort(years.begin(), years.end(), std::greater<int>());
    for (int y : years) {
        auto *yItem = new QTreeWidgetItem(m_tree, {QString::number(y)});
        yItem->setData(0, RoleKind, NodeYear);
        yItem->setData(0, RoleYear, y);

        QList<int> months = buckets[y].keys();
        std::sort(months.begin(), months.end(), std::greater<int>());
        for (int mo : months) {
            QString monthName = QLocale().monthName(mo);
            auto *mItem = new QTreeWidgetItem(yItem, {monthName});
            mItem->setData(0, RoleKind, NodeMonth);
            mItem->setData(0, RoleYear, y);
            mItem->setData(0, RoleMonth, mo);

            QList<int> days = QList<int>(buckets[y][mo].begin(),
                                          buckets[y][mo].end());
            std::sort(days.begin(), days.end(), std::greater<int>());
            for (int d : days) {
                auto *dItem = new QTreeWidgetItem(mItem,
                    {QStringLiteral("%1 %2 %3")
                        .arg(d, 2, 10, QChar('0'))
                        .arg(monthName)
                        .arg(y)});
                dItem->setData(0, RoleKind, NodeDay);
                dItem->setData(0, RoleYear, y);
                dItem->setData(0, RoleMonth, mo);
                dItem->setData(0, RoleDay, d);
            }
        }
        yItem->setExpanded(true);
    }

    if (auto *topY = m_tree->topLevelItem(0)) {
        if (topY->childCount() > 0) {
            auto *firstMonth = topY->child(0);
            firstMonth->setExpanded(true);
            if (firstMonth->childCount() > 0)
                m_tree->setCurrentItem(firstMonth->child(0));
            else
                m_tree->setCurrentItem(firstMonth);
        } else {
            m_tree->setCurrentItem(topY);
        }
    }
}

void HistoryDialog::onSelectionChanged()
{
    loadSelection();
}

void HistoryDialog::applyFilter()
{
    QString filter = m_search ? m_search->text().trimmed() : QString();
    m_filterWords = filter.split(QRegularExpression(QStringLiteral("\\s+")),
                                 Qt::SkipEmptyParts);
    m_filterActive = !m_filterWords.isEmpty();

    if (m_filterActive) {
        const QList<QDate> dates = LogIndex::instance()->datesForConv(
            m_accountKey, m_type, m_convName, m_filterWords);
        m_matchingDates = QSet<QDate>(dates.begin(), dates.end());
    } else {
        m_matchingDates.clear();
    }
    updateTreeVisibility();

    // If current selection got hidden, pick the first visible day in the
    // top-most surviving month.
    auto *cur = m_tree->currentItem();
    if (m_filterActive && (!cur || cur->isHidden()))
        selectFirstVisibleDay();
    else
        loadSelection();
}

void HistoryDialog::updateTreeVisibility()
{
    // Walk year → month → day; hide nodes whose dates are excluded by the
    // current filter. Empty parents are also hidden. Inactive filter shows
    // everything (we just unhide).
    for (int yi = 0; yi < m_tree->topLevelItemCount(); ++yi) {
        QTreeWidgetItem *yItem = m_tree->topLevelItem(yi);
        int y = yItem->data(0, RoleYear).toInt();
        bool yearVisible = false;
        for (int mi = 0; mi < yItem->childCount(); ++mi) {
            QTreeWidgetItem *mItem = yItem->child(mi);
            int mo = mItem->data(0, RoleMonth).toInt();
            bool monthVisible = false;
            for (int di = 0; di < mItem->childCount(); ++di) {
                QTreeWidgetItem *dItem = mItem->child(di);
                int d = dItem->data(0, RoleDay).toInt();
                QDate dt(y, mo, d);
                bool show = !m_filterActive || m_matchingDates.contains(dt);
                dItem->setHidden(!show);
                if (show) monthVisible = true;
            }
            mItem->setHidden(!monthVisible);
            if (monthVisible) {
                yearVisible = true;
                if (m_filterActive) mItem->setExpanded(true);
            }
        }
        yItem->setHidden(!yearVisible);
        if (yearVisible && m_filterActive) yItem->setExpanded(true);
    }
}

void HistoryDialog::selectFirstVisibleDay()
{
    for (int yi = 0; yi < m_tree->topLevelItemCount(); ++yi) {
        QTreeWidgetItem *yItem = m_tree->topLevelItem(yi);
        if (yItem->isHidden()) continue;
        for (int mi = 0; mi < yItem->childCount(); ++mi) {
            QTreeWidgetItem *mItem = yItem->child(mi);
            if (mItem->isHidden()) continue;
            for (int di = 0; di < mItem->childCount(); ++di) {
                QTreeWidgetItem *dItem = mItem->child(di);
                if (!dItem->isHidden()) {
                    m_tree->setCurrentItem(dItem);
                    return;
                }
            }
        }
    }
    // None visible — clear selection + view.
    m_tree->setCurrentItem(nullptr);
    m_view->hide();
    m_emptyLabel->setText(tr("No matches for: %1").arg(m_filterWords.join(QChar(' '))));
    m_emptyLabel->show();
}

void HistoryDialog::loadSelection()
{
    auto *item = m_tree->currentItem();
    if (!item) {
        m_view->hide();
        m_emptyLabel->setText(m_filterActive
            ? tr("No matches for: %1").arg(m_filterWords.join(QChar(' ')))
            : tr("Select a year, month or day on the left to view the conversation history."));
        m_emptyLabel->show();
        return;
    }
    int kind = item->data(0, RoleKind).toInt();
    int y = item->data(0, RoleYear).toInt();
    int mo = item->data(0, RoleMonth).toInt();
    int d = item->data(0, RoleDay).toInt();

    QDate from, to;
    if (kind == NodeYear) {
        from = QDate(y, 1, 1);
        to   = QDate(y, 12, 31);
    } else if (kind == NodeMonth) {
        from = QDate(y, mo, 1);
        to   = from.addMonths(1).addDays(-1);
    } else if (kind == NodeDay) {
        from = QDate(y, mo, d);
        to   = from;
    } else {
        m_view->hide();
        m_emptyLabel->show();
        return;
    }

    qint64 fromTs = QDateTime(from, QTime(0, 0)).toSecsSinceEpoch();
    qint64 toTs   = QDateTime(to, QTime(23, 59, 59)).toSecsSinceEpoch();

    QList<IndexedMessage> msgs = LogIndex::instance()->messagesForConv(
        m_accountKey, m_type, m_convName, fromTs, toTs, m_filterWords);

    if (msgs.isEmpty()) {
        m_view->hide();
        m_emptyLabel->setText(m_filterActive
            ? tr("No matches for: %1 in this range.").arg(m_filterWords.join(QChar(' ')))
            : tr("No conversations in this range."));
        m_emptyLabel->show();
        return;
    }

    m_emptyLabel->hide();
    m_view->clear();
    m_view->setHtml(renderMessages(msgs));
    m_view->show();
    if (auto *bar = m_view->verticalScrollBar())
        bar->setValue(0);
}

QString HistoryDialog::renderMessages(const QList<IndexedMessage> &msgs) const
{
    QString out;
    out.reserve(msgs.size() * 200);
    for (const IndexedMessage &m : msgs) {
        QString datePart = QDateTime::fromSecsSinceEpoch(m.ts)
                              .toString(QStringLiteral("yyyy-MM-dd"));
        QString timePart = QDateTime::fromSecsSinceEpoch(m.ts)
                              .toString(QStringLiteral("HH:mm:ss"));
        QString body = highlightBody(styleQuotes(m.bodyHtml));
        if (m.sender.isEmpty()) {
            out += QStringLiteral(
                "<div style='margin:3px 0 0 0'>"
                "<span style='color:#888; font-size:smaller'>%1 %2</span></div>"
                "<div style='margin:0; color:#888'>%3</div>")
                .arg(timePart, datePart, body);
        } else {
            // For chats libpurple wrote one red for everyone — override
            // with the per-sender hash (and the self blue when the sender
            // matches our account alias) so participants are visually
            // distinct.
            QString color = m.color;
            if (m_type == PURPLE_LOG_CHAT) {
                color = (!m_selfAlias.isEmpty() && m.sender == m_selfAlias)
                    ? QStringLiteral("#16569E")
                    : colorForSender(m.sender);
            }
            out += QStringLiteral(
                "<div style='margin:3px 0 0 0'>"
                "<b style='color:%1'>%2</b> "
                "<span style='color:#888; font-size:smaller'>%3 %4</span></div>"
                "<div style='margin:0'>%5</div>")
                .arg(color, m.sender.toHtmlEscaped(),
                     timePart, datePart, body);
        }
    }
    return out;
}

QString HistoryDialog::highlightBody(QString html) const
{
    if (m_filterWords.isEmpty()) return html;

    QStringList escaped;
    for (const QString &w : m_filterWords) {
        if (w.isEmpty()) continue;
        escaped << QRegularExpression::escape(w);
    }
    if (escaped.isEmpty()) return html;
    QRegularExpression re(QStringLiteral("(") + escaped.join(QLatin1Char('|'))
                              + QStringLiteral(")"),
                          QRegularExpression::CaseInsensitiveOption);

    // Tokenize HTML by tag/text spans; only highlight inside text spans so
    // attribute values inside tags (e.g., file:// URLs) aren't touched.
    QString out;
    out.reserve(html.size() + 64);
    int i = 0;
    while (i < html.size()) {
        if (html.at(i) == QLatin1Char('<')) {
            int end = html.indexOf(QLatin1Char('>'), i);
            if (end < 0) { out.append(html.mid(i)); break; }
            out.append(html.mid(i, end - i + 1));
            i = end + 1;
        } else {
            int next = html.indexOf(QLatin1Char('<'), i);
            if (next < 0) next = html.size();
            QString chunk = html.mid(i, next - i);
            chunk.replace(re,
                QStringLiteral(
                    "<span style='background-color:#ffe066; color:#000'>"
                    "\\1</span>"));
            out.append(chunk);
            i = next;
        }
    }
    return out;
}

} // namespace konqix
