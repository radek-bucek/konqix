Name:           konqix
Version:        0.1.4
Release:        1%{?dist}
Summary:        Qt 6 instant messenger built on libpurple

License:        GPL-2.0-or-later
URL:            https://github.com/radek-bucek/%{name}
Source0:        %{url}/archive/refs/tags/v%{version}/%{name}-%{version}.tar.gz

BuildRequires:  cmake >= 3.16
BuildRequires:  gcc-c++
BuildRequires:  desktop-file-utils
BuildRequires:  pkgconfig(purple)
BuildRequires:  cmake(Qt6Widgets)
BuildRequires:  cmake(Qt6Core)
BuildRequires:  cmake(Qt6Gui)
BuildRequires:  cmake(Qt6Sql)
BuildRequires:  cmake(Qt6Network)
BuildRequires:  cmake(KF6WindowSystem)
BuildRequires:  pkgconfig(hunspell)

Requires:       libpurple
Recommends:     kf6-kwindowsystem
Recommends:     hunspell
Recommends:     hunspell-en-US

%description
A native Qt 6 frontend for libpurple — a clean-room UI that talks to
libpurple directly through the PurpleXxxUiOps callback interface and
reuses all of libpurple's protocol plugins (XMPP, IRC, SIMPLE, Bonjour,
Gadu-Gadu, and any third-party plugins installed in the system or
per-user purple plugin directory).

Features include a grouped buddy list with offline-hiding, IM and chat
windows with inline image rendering and 12-hour history preview, a
separate searchable history dialog, account management with
protocol-specific options, a system tray icon with three states
(online, attention, offline), and configuration read from libpurple's
standard ~/.purple/ directory.

%prep
%autosetup -n %{name}-%{version}

%build
%cmake
%cmake_build

%install
%cmake_install

%check
desktop-file-validate %{buildroot}%{_datadir}/applications/com.konqix.Konqix.desktop

%files
%{_bindir}/%{name}
%{_datadir}/applications/com.konqix.Konqix.desktop
%{_datadir}/icons/hicolor/scalable/apps/%{name}.svg

%changelog
* Mon Sep 08 2026 Radek Bucek <288079766+radek-bucek@users.noreply.github.com> - 0.1.4-1
- Per-buddy status icon in the buddy list (via Qt::DecorationRole)
  and matching peer name + icon in the IM conversation window's
  menu-bar corner. Shared PurpleStatusPrimitive → QIcon mapping
  lives in src/StatusIcons.h.
- View → Show status icons toggle (default on) gates both the buddy
  list icon and the conversation-window peer widget in step; new
  pref /konqix/blist/show_status_icons.
- Tray menu status entries (Available / Away / Invisible / Offline)
  are now radio-checkable via QActionGroup; the entry matching the
  active primitive gets the tick, refreshed on QMenu::aboutToShow
  so it stays in sync with status changes made elsewhere.
- Fix: null the conversation window's PurpleBuddy* before libpurple
  frees it. New BuddyListModel::buddyRemoved signal fires from the
  blist-remove ui-op; the peer widget drops the dangling pointer
  and hides itself before any later access can deref freed memory
  inside purple_buddy_get_alias.
- Buddies seen within the last minute now render as "now" instead
  of an empty label pair — visually consistent with the other
  last-seen rows, especially in Exact mode.
- Dead spacer widget m_statusInfo removed from the status bar;
  QStatusBar's addPermanentWidget already right-anchors the
  "Status:" label and combo, so the spacer was a no-op.
* Wed Sep 02 2026 Radek Bucek <288079766+radek-bucek@users.noreply.github.com> - 0.1.3-1
- New buddy list "last seen" indicator. View → Show last seen offers
  Off (default), Approximate ("5 min ago" / "3 h ago" / "2 d ago" /
  date), or Exact (HH:mm today, otherwise yyyy-MM-dd HH:mm). Rendered
  as small grey condensed text after the buddy name, vertically
  centred against the surrounding baseline. Approximate labels tick
  on a 60-second timer without re-querying the prpl. Powered by a
  new HtmlItemDelegate on the buddy tree.
- Fix: DST-aware parsing of the tdlib-purple "Last online" tooltip
  string — the zero-initialised struct tm's tm_isdst=0 made mktime
  interpret every parsed timestamp as winter time, shifting DST-era
  values one hour off from what Get info displayed.
- Startup notification quiet window: for the first 30 s after launch
  konqix suppresses per-message sound / tray flash / balloon so the
  replay burst from libpurple (messages that landed on another
  device while offline) doesn't drown the user in alerts. Unread
  state still tracks in the buddy list; when the window ends, a
  single summary balloon reports the count and — if any conv is
  still unread — the tray flips to the attention icon.
* Wed Aug 05 2026 Radek Bucek <288079766+radek-bucek@users.noreply.github.com> - 0.1.2-1
- Fix a SEGV in the tray-click handler when unread messages exist for a
  conversation that libpurple has already freed (disconnect, prpl close,
  chat rejoin). MessageState kept the raw PurpleConversation* in its
  unread map; ConversationManager::onDestroy cleaned up its own window
  but never told MessageState, so a later tray click routed the dangling
  pointer into ConversationWindow's constructor and dereferenced freed
  memory inside libpurple. onDestroy now calls MessageState::forget()
  so the pointer is dropped before libpurple actually frees the conv.
* Wed Aug 05 2026 Radek Bucek <288079766+radek-bucek@users.noreply.github.com> - 0.1.1-1
- Chat history from the buddy-list context menu ("Show history…" on a
  chat) is no longer empty. The dialog now resolves the room
  identifier via prpl->get_chat_name(components) — libpurple's log
  directories are keyed by the id, not the human-readable alias.
- Typing indicator no longer hides the last line of the conversation.
  Showing the "X is typing…" footer shrinks the QTextBrowser viewport;
  when the user was at the bottom we now re-pin to bottom after Qt's
  layout pass so the last message stays visible.
- Paste into the input field is plain-text only. Rich clipboard
  content (font, colour, size) is stripped so pasted text keeps the
  widget's configured font, matching every other IM client.
- Input field auto-grows with typed content, capped at half of the
  window height. The default (empty / short) height stays at 120 px
  as before.
* Sun May 31 2026 Radek Bucek <288079766+radek-bucek@users.noreply.github.com> - 0.1.0-1
- Initial release.
- Grouped buddy list with offline-hiding, four sort modes, persistent
  expand/collapse and persistent main window geometry.
- IM and chat windows with inline image rendering and 12-hour history
  preview; Enter to send, Shift+Enter for newline.
- Image paste from clipboard with caption-before-send.
- Searchable history dialog: year/month/day tree, free-text filter
  backed by a SQLite FTS5 log index at ~/.cache/konqix/.
- Receive-side typing indicator + outgoing typing notifications.
- Hunspell-backed spell check on the input with per-conversation
  language override.
- Per-sender chat colours (sequential assignment, no hash collisions)
  and quote / citation styling for "> Name wrote:" replies.
- Tray icon with three states (online, attention, offline), context
  menu, balloon notification toggle, optional sound and auto-open.
- Account management (add / edit / remove / enable / disable) with
  protocol-specific options from PurpleAccountOption, including
  user_splits placeholders for protocols like Telegram.
- Single-instance lock via per-user Unix domain socket.
- File transfer (send any file via attach button or drag-and-drop).
- HiDPI: PassThrough scale rounding so fractional KWin scale stays sharp.
- Telegram tdlib accounts default "File downloads" to "Inline".
