# konqix

A Qt 6 instant messenger built on libpurple, tuned for KDE Plasma.
Licensed under [**GPL-2.0-or-later**](LICENSE).

It reuses all of libpurple's protocol plugins (XMPP/Jabber, IRC,
Gadu-Gadu, Bonjour, Novell GroupWise, SAMETIME, SIMPLE, Zephyr, and any
third-party plugins such as the Telegram tdlib plugin or Slack/Matrix
bridges). It is a clean-room Qt 6 frontend that talks to `libpurple`
directly through the `PurpleXxxUiOps` callback interface — no GTK code
involved.

## Features

- Grouped buddy list with "hide offline" mode, expand/collapse persistence,
  and four sort modes (status, name, last conversation, last seen).
- IM and chat windows: history preview (current day, with a 12 h fallback
  window if the current day is short or the last conversation predates
  it), Enter to send, Shift+Enter for a new line.
- Inline image rendering in conversations and history (both received and
  paste-sent attachments).
- Send images from the clipboard: paste produces a removable thumbnail
  tile in the input bar; you can add a caption and only then send.
- A separate searchable History dialog with a year / month / day tree
  and a free-text filter.
- Notifications: tray attention icon, optional sound, optional balloon,
  optional auto-open of the conversation window.
- Account management (add / edit / remove / enable / disable) with
  protocol-specific options generated from `PurpleAccountOption`,
  including the `user_splits` fields for protocols that need them.
- Status combo box in the main window and matching entries in the tray
  menu.
- Tray icon with three states (green = online, amber = attention, grey =
  offline), a context menu (show/hide, status, accounts, add buddy,
  quit), and fast activation via `KF6::WindowSystem` when available.
- Closing the main window only hides it to the tray.
- Dialogs for libpurple `request` (input, choice, action, fields, file,
  folder) and `notify` (message, formatted, e-mail(s), userinfo, search
  results, URI).
- Configuration is read from libpurple's standard `~/.purple/` directory
  (`accounts.xml`, `blist.xml`, plugin configs such as
  `telegram-tdlib-api.conf`). Override `KONQIX_CONFIG_DIR` for an
  isolated profile.
- Per-user plugins from `~/.purple/plugins/` plus system plugins from
  `/usr/lib*/purple-2/`.
- For protocols without `user_splits` (e.g. Telegram, where the phone
  number is the username) the Username field shows a placeholder pulled
  from `get_account_text_table()->login_label`
  (e.g. "phone no. (+ country prefix)").
- Persistent main window geometry across restarts (works on Wayland by
  preferring the XCB platform plugin via `QT_QPA_PLATFORM=xcb`).

## What is (intentionally) missing

- No plugin-configuration UI and no generic preferences window —
  everything else is covered by the tray menu and the accounts dialog.
  Saved-status preferences and general prefs live in libpurple and work
  even without a dedicated editor.

## Requirements

Runtime (Fedora 43 package names):

- `libpurple`
- `qt6-qtbase`
- `hunspell` + per-language dictionaries (`hunspell-en-US`, `hunspell-cs`, …)
  for the optional spell checker
- `kf6-kwindowsystem` (optional, strongly recommended for fast tray
  activation under KWin)

Build-time additionally:

- `cmake >= 3.16`, `gcc-c++` (or any C++17 compiler)
- `libpurple-devel`
- Qt 6 development packages: `qt6-qtbase-devel`
- `hunspell-devel` (optional, enables in-input spell checking)
- `kf6-kwindowsystem-devel` (optional)

```bash
sudo dnf install libpurple-devel qt6-qtbase-devel kf6-kwindowsystem-devel \
                 hunspell-devel cmake gcc-c++
```

### Protocol plugins

konqix is a UI on top of libpurple — it talks to the same `~/.purple/`
plugins libpurple already loads. For protocols that aren't bundled in
stock libpurple you need a third-party prpl.

- **Telegram (tdlib-purple)** — install / build instructions and binary
  releases live at
  <https://github.com/radek-bucek/tdlib-purple>. Drop the resulting
  `libtelegram-tdlib.so` into `/usr/lib*/purple-2/` (system-wide) or
  `~/.purple/plugins/` (per user); restart konqix and add a new
  account of type "telegram-tdlib". **konqix has been primarily
  developed and tested against this plugin**, so most of the
  protocol-specific polish (per-sender colours in chats, message
  echo dedup, quote styling, "File downloads" default) is tuned for it.

## Building from source

```bash
cmake -S . -B build
cmake --build build -j$(nproc)
./build/konqix
```

Enable libpurple debug output with `KONQIX_DEBUG=1`.

## Building a Fedora RPM

Everything packaging-related lives in `packaging/`:

```
packaging/
├── konqix.spec        # RPM spec
├── konqix.desktop     # Desktop entry (installed by CMake)
└── make-srpm.sh          # one-shot SRPM (and optionally RPM) builder
```

### Build the SRPM

```bash
./packaging/make-srpm.sh
# → ~/rpmbuild/SRPMS/konqix-<version>-1.fc<release>.src.rpm
```

### Build the SRPM **and** the binary RPM in one go

```bash
./packaging/make-srpm.sh --rpm
# → ~/rpmbuild/RPMS/x86_64/konqix-<version>-1.fc<release>.x86_64.rpm
```

### Use a different rpmbuild tree

```bash
RPM_TOPDIR=/var/tmp/rpmbuild ./packaging/make-srpm.sh
```

### Build in a clean Mock chroot

```bash
./packaging/make-srpm.sh
mock -r fedora-43-x86_64 ~/rpmbuild/SRPMS/konqix-*.src.rpm
```

### Install the built RPM

```bash
sudo dnf install ~/rpmbuild/RPMS/x86_64/konqix-*.x86_64.rpm
```

The package installs:

- `/usr/bin/konqix`
- `/usr/share/applications/konqix.desktop`
- `/usr/share/icons/hicolor/scalable/apps/konqix.svg`

## Building a Debian/Ubuntu package

Debian packaging metadata lives in `debian/` at the repository root (dpkg
tooling requires it there, unlike the RPM spec under `packaging/`):

```
debian/
├── control            # Build-Deps, runtime Deps, package description
├── rules              # dh $@ (debhelper autodetects the CMake buildsystem)
├── changelog
├── copyright
└── source/format       # 3.0 (quilt)
packaging/
└── make-deb.sh        # one-shot binary .deb (and optionally source package) builder
```

Build-time dependencies (Ubuntu/Debian package names):

```bash
sudo apt install cmake pkgconf libpurple-dev qt6-base-dev \
                 libkf6windowsystem-dev libhunspell-dev desktop-file-utils \
                 debhelper
```

### Build the binary .deb

```bash
./packaging/make-deb.sh
# → ~/debbuild/konqix_<version>-1_amd64.deb
```

### Build the source package too

```bash
./packaging/make-deb.sh --source
# → ~/debbuild/konqix_<version>.orig.tar.gz
# → ~/debbuild/konqix_<version>-1.dsc
# → ~/debbuild/konqix_<version>-1.debian.tar.xz
```

### Use a different output directory

```bash
DEB_TOPDIR=/var/tmp/debbuild ./packaging/make-deb.sh
```

### Install the built package

```bash
sudo apt install ~/debbuild/konqix_*_amd64.deb
```

The package installs the same files as the RPM, under the same paths.

## Installing on Arch Linux

konqix isn't in the official Arch repos or the AUR yet. A `PKGBUILD`
is maintained separately at
[asmbk/aur-konqix](https://github.com/asmbk/aur-konqix) (unofficial,
not maintained by this project):

```bash
git clone https://github.com/asmbk/aur-konqix.git
cd aur-konqix
yay -Bi .    # or: paru -Bi .
```

`libpurple` itself is AUR-only (not in the official repos), so an AUR
helper is the easiest path — see that repo's README for the fully
manual install steps (no AUR helper needed) and for keeping the
package up to date across releases.

## Project layout

```
.
├── CMakeLists.txt
├── src/                # all C++ sources
├── resources/          # Qt .qrc + tray/app SVG icons
└── packaging/          # RPM spec, desktop file, make-srpm.sh
```

Source files of note:

| File | Role |
|---|---|
| `main.cpp` | Bootstrap (`QApplication` + `PurpleCore`). |
| `PurpleCore.{h,cpp}` | Registers all UI ops, inits libpurple core, points data storage at XDG config. |
| `QtEventLoop.cpp` | GLib-based `PurpleEventLoopUiOps`; Qt 6 on Linux already integrates the GLib main loop, so everything runs in one event loop. |
| `BuddyListModel.{h,cpp}` | `PurpleBlistUiOps` → `QAbstractItemModel`, with last-activity and last-seen sort modes. |
| `MainWindow.{h,cpp}` | Buddy list, menu, status combo, persistent geometry. |
| `ConversationManager.{h,cpp}` | `PurpleConversationUiOps` → routes messages to `ConversationWindow`. |
| `ConversationWindow.{h,cpp}` | IM/chat window (history preview + input + paste-attachment bar). |
| `HistoryDialog.{h,cpp}` | Standalone history browser with year/month/day tree and search. |
| `LogFormat.{h,cpp}` | Shared helpers that turn libpurple's raw HTML log into the two-row "header / body" layout. |
| `AccountsDialog.{h,cpp}` | Account list, enable/disable, remove. |
| `AccountEditDialog.{h,cpp}` | Account form with protocol-specific options from `PurpleAccountOption`. |
| `TrayIcon.{h,cpp}` | `QSystemTrayIcon` + menu, three-state icon set. |
| `Notifier.{h,cpp}` | Tray flashing, sound, balloon, attention icon swap. |
| `RequestDispatcher.{h,cpp}` | `PurpleRequestUiOps`. |
| `NotifyDispatcher.{h,cpp}` | `PurpleNotifyUiOps`. |
| `MessageState.{h,cpp}` | Per-conversation unread tracking. |

## Contributors

- [@radek-bucek](https://github.com/radek-bucek) — author and maintainer.
- AI assistance: developed with the help of Claude (Anthropic) via Claude Code,
  used as a tool. I have reviewed and understand every line, and I take full
  responsibility for the code.

## License

**GPL-2.0-or-later** — see [`LICENSE`](LICENSE) for the full text.

```
Copyright (C) 2026 Radek Bucek

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
```

Every source file carries an SPDX identifier (`SPDX-License-Identifier: GPL-2.0-or-later`).
