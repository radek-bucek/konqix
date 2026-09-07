// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Radek Bucek

#pragma once

#include <QAbstractItemModel>
#include <QHash>

extern "C" {
#include <libpurple/blist.h>
}

namespace konqix {

class BuddyListModel : public QAbstractItemModel
{
    Q_OBJECT
public:
    static BuddyListModel *instance();
    static PurpleBlistUiOps *uiOps();

    explicit BuddyListModel(QObject *parent = nullptr);
    ~BuddyListModel() override;

    // QAbstractItemModel
    QModelIndex index(int row, int column, const QModelIndex &parent = {}) const override;
    QModelIndex parent(const QModelIndex &child) const override;
    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

    PurpleBlistNode *nodeFromIndex(const QModelIndex &index) const;
    QModelIndex indexFromNode(PurpleBlistNode *node) const;

    // Settings
    enum class SecondarySort {
        Name,
        ActivityDesc,   // Latest log directory mtime (last conversation)
        LastSeen,       // Protocol presence "login time" (last online)
    };
    static SecondarySort parseSecondarySort(const QString &s);
    static QString secondarySortToString(SecondarySort m);

    void setShowOffline(bool show);
    bool showOffline() const { return m_showOffline; }
    void setShowAway(bool show);
    bool showAway() const { return m_showAway; }
    enum class LastSeenDisplay {
        Off,           // no suffix
        Approximate,   // "5m ago" / "3h ago" / "2d ago" / date
        Exact,         // "HH:MM" (today) or "YYYY-MM-DD HH:MM"
    };
    static LastSeenDisplay parseLastSeenDisplay(const QString &s);
    static QString lastSeenDisplayToString(LastSeenDisplay m);
    void setLastSeenDisplay(LastSeenDisplay mode);
    LastSeenDisplay lastSeenDisplay() const { return m_lastSeenDisplay; }
    void setSortByStatus(bool on);
    bool sortByStatus() const { return m_sortByStatus; }
    void setSecondarySort(SecondarySort mode);
    SecondarySort secondarySort() const { return m_secondarySort; }

    // Triggered by ui_ops callbacks
    void rebuild();
    void nodeUpdated(PurpleBlistNode *node);
    void nodeRemoved(PurpleBlistNode *node);
    // Re-emit dataChanged() for every buddy/contact row so approximate
    // "N min ago" labels re-format against the current clock. Called
    // once a minute by an internal QTimer; no-op outside Approximate
    // mode.
    void refreshLastSeenLabels();

signals:
    void modelChanged();
    // Fired synchronously whenever a blist update resolves to a buddy —
    // e.g. the conversation window header uses this to keep its peer
    // status icon live without waiting for the (debounced) full rebuild.
    void buddyStatusChanged(PurpleBuddy *buddy);

private:
    bool nodeVisible(PurpleBlistNode *node) const;
    bool buddyVisible(PurpleBuddy *buddy) const;
    QList<PurpleBlistNode *> visibleChildren(PurpleBlistNode *parent) const;
    int childRow(PurpleBlistNode *node) const;
    // Human-readable last-seen text (e.g. "14:32", "12 min ago") for
    // the given buddy, or empty when disabled / unknown / essentially
    // "now". Rendered by data() inside the same [status …] bracket as
    // the status label, in a smaller grey font.
    QString lastSeenLabel(PurpleBuddy *b) const;
    // Build the final display string for a buddy/contact row: name plus
    // an optional "[status]", "[time]" or "[status time]" bracket. Uses
    // rich HTML when a time is present (so the HtmlItemDelegate can draw
    // the time smaller and grey); plain text otherwise.
    QString formatBuddyRow(const QString &name, PurpleBuddy *b) const;

    bool m_showOffline = false;
    bool m_showAway = true;
    LastSeenDisplay m_lastSeenDisplay = LastSeenDisplay::Off;
    bool m_sortByStatus = true;
    SecondarySort m_secondarySort = SecondarySort::Name;
};

} // namespace konqix
