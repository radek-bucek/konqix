Name:           konqix
Version:        0.1.1
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
