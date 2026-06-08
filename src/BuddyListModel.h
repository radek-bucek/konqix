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
    void setSortByStatus(bool on);
    bool sortByStatus() const { return m_sortByStatus; }
    void setSecondarySort(SecondarySort mode);
    SecondarySort secondarySort() const { return m_secondarySort; }

    // Triggered by ui_ops callbacks
    void rebuild();
    void nodeUpdated(PurpleBlistNode *node);
    void nodeRemoved(PurpleBlistNode *node);

signals:
    void modelChanged();

private:
    bool nodeVisible(PurpleBlistNode *node) const;
    bool buddyVisible(PurpleBuddy *buddy) const;
    QList<PurpleBlistNode *> visibleChildren(PurpleBlistNode *parent) const;
    int childRow(PurpleBlistNode *node) const;

    bool m_showOffline = false;
    bool m_showAway = true;
    bool m_sortByStatus = true;
    SecondarySort m_secondarySort = SecondarySort::Name;
};

} // namespace konqix
