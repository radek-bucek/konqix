// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Radek Bucek

#pragma once

#include <QMainWindow>

extern "C" {
#include <libpurple/blist.h>
}

class QTreeView;
class QComboBox;
class QLabel;
class QFrame;

namespace konqix {

class BuddyListModel;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

    void showAccountsDialog();
    void showAddBuddyDialog();
    void changeStatus(int statusType);

public:
    void saveGeometryToPrefs();

protected:
    void closeEvent(QCloseEvent *event) override;
    void moveEvent(QMoveEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private slots:
    void onItemDoubleClicked(const QModelIndex &index);
    void onItemContextMenu(const QPoint &pos);
    void onShowOfflineToggled(bool checked);
    void onShowAwayToggled(bool checked);

private:
    void buildMenus();
    void buildStatusBar();
    void restoreGeometryFromPrefs();
    void expandTopGroups();
    void addGroup();
    void moveBuddyToGroup(PurpleBuddy *buddy, const QString &groupName);
    PurpleGroup *promptForNewGroup();
    void warnAboutMissingProtocols();

    BuddyListModel *m_model = nullptr;
    QTreeView *m_tree = nullptr;
    QComboBox *m_statusCombo = nullptr;
    QLabel *m_statusInfo = nullptr;
    QFrame *m_warningBanner = nullptr;
    QLabel *m_warningLabel = nullptr;
};

} // namespace konqix
