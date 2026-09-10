// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Radek Bucek

#pragma once

#include <QString>
#include <ctime>

namespace konqix {

// Structured representation of one HTML-logger message line. The regexes
// in parseLogLine() are the single source of truth for what libpurple's
// log format looks like — both reformatLogLine() (for rendering) and the
// SQLite indexer in LogIndex consume this struct.
struct ParsedLogLine {
    bool   matched = false;   // false if the line is junk / whitespace / non-message
    bool   hasSender = false; // true for "color span + <b>Name:</b>" lines (named)
    QString color;            // e.g. "#A82F2F" — empty for sys
    QString time;             // "HH:MM:SS"
    QString sender;           // "" for sys
    QString bodyRaw;          // body verbatim from the log (no image/newline post-processing)
};

ParsedLogLine parseLogLine(const QString &line);

// Heuristic: libpurple's xfer subsystem narrates every step ("Offering to
// send foo.png to Martina", "Transfer of file foo.png complete") as
// PURPLE_MESSAGE_SYSTEM in the conv. The inline image already shows what
// happened, so these lines are pure noise. Strips HTML from the input
// before matching, accepts either raw body or escaped/wrapped variant.
bool isTransferNoise(const QString &body);

// Stable per-sender colour for chat participants. libpurple's HTML
// logger writes every chat message in the same red regardless of sender,
// so we hash the name into a fixed mid-saturated palette ourselves. The
// "self" blue (#16569E) is intentionally excluded from the palette so
// the user's own messages stay distinct when the caller short-circuits
// to that colour.
QString colorForSender(const QString &sender);

// Wrap consecutive "> "/"&gt; "-prefixed lines (Telegram tdlib reply
// preview, e-mail-style quoting) in a dim italic span. Operates on the
// post-convertNewlines body, where lines are separated by literal <br>.
QString styleQuotes(const QString &bodyHtml);



// Helpers for turning the raw HTML libpurple's logger (or the live
// write_conv callback) hands us into something QTextBrowser can render
// the way we want: inline images, no broken "<a href=…>foo.jpg</a>"
// hyperlinks, and a two-row "header / body" layout per message.

// Wraps bare URLs in the raw message text with "<a href="…">" anchors,
// via libpurple's own purple_markup_linkify() — it's tag-aware (skips
// anything already inside a tag or attribute), so it's safe to run even
// when the text already contains other HTML. Should run first, before
// the image/newline/quote helpers below, matching how libpurple's own
// UIs (e.g. Pidgin) order their message pipeline. Without this, a bare
// "https://…" in a plain-text message renders as inert text — QTextEdit
// / QTextBrowser have no built-in autodetection for that.
QString linkify(const QString &html);

// Expand "<img id=\"N\">" tags into a base64 data URI by looking the image
// up in libpurple's imgstore.
QString resolveImgIds(const QString &html);

// Rewrite relative <img src='x.jpg'> to absolute file:// URLs rooted at
// baseDir (typically the conv's log directory).
QString resolveImgPaths(const QString &html, const QString &baseDir);

// Turn libpurple-xfer-style "<a href='file:///foo.jpg'>foo.jpg […]</a>"
// hyperlinks into bare <img> tags so QTextBrowser renders the file inline.
QString rewriteImageLinks(const QString &html);

// Strip "\n" right after <br> separators, then convert remaining \n
// (real in-body line breaks) to <br>.
QString convertNewlines(QString html);

// libpurple's HTML logger emits each message inline:
//     <span color:..><span smaller>(HH:MM:SS)</span> <b>Name:</b></span> body<br>
// Reformat those lines into two stacked rows: "[datetime] Name" then body.
// baseDate supplies the date portion (the logger only stores HH:MM:SS in
// the message itself; the date lives in the filename / log->time).
// For chats libpurple wrote the same red colour for every participant —
// pass isChat=true + selfAlias=<account alias> to substitute the
// per-sender hash from colorForSender(), with selfAlias getting #16569E.
QString reformatLogMessages(const QString &html, time_t baseDate,
                            bool isChat = false,
                            const QString &selfAlias = {});

} // namespace konqix
