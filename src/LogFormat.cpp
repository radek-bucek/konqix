// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Radek Bucek

#include "LogFormat.h"

#include <QByteArray>
#include <QDateTime>
#include <QRegularExpression>
#include <QUrl>

#include <array>
#include <cstring>

extern "C" {
#include <libpurple/imgstore.h>
}

namespace konqix {

QString resolveImgIds(const QString &html)
{
    static const QRegularExpression re(
        QStringLiteral(R"(<img\s+[^>]*\bid\s*=\s*['"]?(\d+)['"]?[^>]*>)"),
        QRegularExpression::CaseInsensitiveOption);
    QString out;
    int last = 0;
    auto it = re.globalMatch(html);
    while (it.hasNext()) {
        auto m = it.next();
        out += html.mid(last, m.capturedStart() - last);
        int id = m.captured(1).toInt();
        PurpleStoredImage *img = purple_imgstore_find_by_id(id);
        if (img) {
            gconstpointer data = purple_imgstore_get_data(img);
            size_t size = purple_imgstore_get_size(img);
            const char *ext = purple_imgstore_get_extension(img);
            QByteArray bytes(static_cast<const char *>(data), int(size));
            QString mime = QStringLiteral("image/png");
            if (ext) {
                QString e = QString::fromLatin1(ext).toLower();
                if (e == QLatin1String("jpg") || e == QLatin1String("jpeg"))
                    mime = QStringLiteral("image/jpeg");
                else if (e == QLatin1String("gif"))  mime = QStringLiteral("image/gif");
                else if (e == QLatin1String("webp")) mime = QStringLiteral("image/webp");
                else if (e == QLatin1String("png"))  mime = QStringLiteral("image/png");
            }
            out += QStringLiteral("<img src=\"data:%1;base64,%2\">")
                   .arg(mime, QString::fromLatin1(bytes.toBase64()));
        } else {
            out += QStringLiteral("[image]");
        }
        last = m.capturedEnd();
    }
    out += html.mid(last);
    return out;
}

QString resolveImgPaths(const QString &html, const QString &baseDir)
{
    if (baseDir.isEmpty()) return html;
    static const QRegularExpression re(
        QStringLiteral(R"((<img\s+[^>]*?\bsrc\s*=\s*)(['"])([^'"]+)(['"]))"),
        QRegularExpression::CaseInsensitiveOption);
    QString out;
    int last = 0;
    auto it = re.globalMatch(html);
    while (it.hasNext()) {
        auto m = it.next();
        out += html.mid(last, m.capturedStart() - last);
        QString src = m.captured(3);
        if (src.startsWith(QLatin1String("data:")) ||
            src.startsWith(QLatin1String("file:")) ||
            src.startsWith(QLatin1String("http"))) {
            out += m.captured(0);
        } else {
            QString abs = src.startsWith(QLatin1Char('/'))
                ? src
                : (baseDir + QLatin1Char('/') + src);
            out += m.captured(1) + m.captured(2)
                 + QUrl::fromLocalFile(abs).toString() + m.captured(2);
        }
        last = m.capturedEnd();
    }
    out += html.mid(last);
    return out;
}

QString rewriteImageLinks(const QString &html)
{
    static const QRegularExpression re(
        QStringLiteral(R"(<a\s+href\s*=\s*['"](file://[^'"]+\.(?:jpe?g|png|gif|webp|bmp))['"]\s*>[^<]*</a>)"),
        QRegularExpression::CaseInsensitiveOption);
    int last = 0;
    QString out;
    auto it = re.globalMatch(html);
    while (it.hasNext()) {
        auto m = it.next();
        out += html.mid(last, m.capturedStart() - last);
        out += QStringLiteral("<img src='%1' />").arg(m.captured(1));
        last = m.capturedEnd();
    }
    out += html.mid(last);
    return out;
}

bool isTransferNoise(const QString &body)
{
    QString plain = body;
    plain.remove(QRegularExpression(QStringLiteral("<[^>]+>")));
    plain = plain.trimmed();
    if (plain.isEmpty()) return false;
    static const QStringList prefixes = {
        QStringLiteral("Offering to send "),
        QStringLiteral("Starting transfer of "),
        QStringLiteral("Transfer of file "),
        QStringLiteral("Cancelling transfer of "),
        QStringLiteral("Resuming transfer of "),
        QStringLiteral("Beginning transfer of "),
    };
    for (const QString &p : prefixes) {
        if (plain.startsWith(p, Qt::CaseInsensitive))
            return true;
    }
    if (plain.contains(QStringLiteral(" is offering to send file "),
                       Qt::CaseInsensitive))
        return true;
    return false;
}

QString colorForSender(const QString &sender)
{
    // 18 mid-saturated Material 600/700 colours, avoiding anything near
    // the self-blue #16569E so the user's own messages always stand out.
    // Adjacent palette entries are intentionally far apart in hue.
    static const std::array<QStringView, 18> palette = {
        u"#D32F2F", u"#00796B", u"#9C27B0", u"#F57C00",
        u"#0097A7", u"#C2185B", u"#388E3C", u"#7B1FA2",
        u"#E64A19", u"#673AB7", u"#558B2F", u"#5D4037",
        u"#607D8B", u"#00838F", u"#689F38", u"#E91E63",
        u"#795548", u"#455A64",
    };
    // Sequential assignment: each new sender takes the next palette slot.
    // Hashing inevitably collides (Petr Halounek and milan kocian both
    // landed on the same FNV-1a mod 18 bucket); sequential guarantees
    // every distinct sender encountered in a session gets its own colour
    // until the palette is exhausted (then we wrap and live with reuse).
    static QHash<QString, QString> assigned;
    static int next = 0;
    auto it = assigned.constFind(sender);
    if (it != assigned.constEnd()) return it.value();
    QString c = palette[next % palette.size()].toString();
    assigned.insert(sender, c);
    ++next;
    return c;
}

QString styleQuotes(const QString &bodyHtml)
{
    // Fast path: no quote marker → return as-is. Keeps the per-message
    // overhead near-zero for the (large) majority of messages.
    if (!bodyHtml.contains(QStringLiteral("&gt;"))
        && !bodyHtml.contains(QLatin1Char('>')))
        return bodyHtml;

    static const QRegularExpression brRe(
        QStringLiteral("<br\\s*/?\\s*>"),
        QRegularExpression::CaseInsensitiveOption);

    const QStringList parts = bodyHtml.split(brRe);

    auto isQuoteLine = [](const QString &line) {
        // Skip leading whitespace + any single opening tag (Telegram's
        // "Name wrote:" header is wrapped in <span style='font-weight:bold;'>).
        QString tail = line;
        tail.remove(QRegularExpression(QStringLiteral("^\\s*(?:<[^>]+>\\s*)*")));
        return tail.startsWith(QStringLiteral("&gt; "))
            || tail.startsWith(QStringLiteral("&gt;\t"))
            || tail.startsWith(QStringLiteral("> "))
            || tail.startsWith(QStringLiteral(">\t"));
    };

    QString out;
    out.reserve(bodyHtml.size() + 64);
    int i = 0;
    while (i < parts.size()) {
        if (isQuoteLine(parts.at(i))) {
            QString block;
            while (i < parts.size() && isQuoteLine(parts.at(i))) {
                if (!block.isEmpty()) block += QStringLiteral("<br>");
                block += parts.at(i);
                ++i;
            }
            out += QStringLiteral(
                "<span style='color:#777; font-style:italic'>");
            out += block;
            out += QStringLiteral("</span>");
            if (i < parts.size()) out += QStringLiteral("<br>");
        } else {
            out += parts.at(i);
            ++i;
            if (i < parts.size()) out += QStringLiteral("<br>");
        }
    }
    return out;
}

QString convertNewlines(QString html)
{
    static const QRegularExpression brNl(
        QStringLiteral(R"(<br\s*/?>\s*\n+)"),
        QRegularExpression::CaseInsensitiveOption);
    html.replace(brNl, QStringLiteral("<br>"));
    html.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    html.replace(QChar('\n'), QStringLiteral("<br>"));
    return html;
}

// Parse one HTML-logger message line into structured fields. Use greedy
// body capture so the body is always whatever follows the prefix —
// non-greedy + $-lookahead was capturing empty bodies in practice.
ParsedLogLine parseLogLine(const QString &line)
{
    ParsedLogLine out;

    // Skip lines that contain no visible text at all (just whitespace
    // between separators, residual <p> wrappers, etc.).
    {
        QString stripped = line;
        stripped.remove(QRegularExpression(QStringLiteral("<[^>]+>")));
        stripped.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
        if (stripped.trimmed().isEmpty())
            return out;
    }

    static const QRegularExpression named(
        QStringLiteral(
            R"(<span[^>]*color\s*:\s*([^"';]+)[^>]*>\s*)"
            R"(<span[^>]*font-size\s*:\s*smaller[^>]*>\s*\(([^)]+)\)\s*</span>\s*)"
            R"(<b>([^<]+):</b>\s*</span>\s*([\s\S]*))"
        ),
        QRegularExpression::CaseInsensitiveOption);
    if (auto m = named.match(line); m.hasMatch()) {
        out.matched   = true;
        out.hasSender = true;
        out.color     = m.captured(1).trimmed();
        out.time      = m.captured(2);
        out.sender    = m.captured(3);
        out.bodyRaw   = m.captured(4);
        return out;
    }

    static const QRegularExpression sys(
        QStringLiteral(
            R"(<span[^>]*font-size\s*:\s*smaller[^>]*>\s*\(([^)]+)\)\s*</span>\s*([\s\S]*))"
        ),
        QRegularExpression::CaseInsensitiveOption);
    if (auto m = sys.match(line); m.hasMatch()) {
        out.matched   = true;
        out.hasSender = false;
        out.time      = m.captured(1);
        out.bodyRaw   = m.captured(2);
        return out;
    }
    return out;
}

// Header layout: "<b>Name</b> HH:MM:SS YYYY-MM-DD" / body on the next row.
// Bare timestamp (sys branch) loses the leading bracketed prefix entirely.
// For chats the per-message colour comes from our hash (libpurple's HTML
// logger writes the same red for every participant); selfAlias is the
// account alias that should override to blue.
static QString reformatLogLine(const QString &line, const QString &datePart,
                               bool isChat, const QString &selfAlias)
{
    ParsedLogLine p = parseLogLine(line);
    if (!p.matched)
        return QString();

    // Drop libpurple xfer narration ("Offering to send …", "Transfer of
    // file … complete"). The inline image / transfer outcome is already
    // visible in the conversation, so these system lines are pure noise.
    if (!p.hasSender && isTransferNoise(p.bodyRaw))
        return QString();

    QString body = styleQuotes(convertNewlines(p.bodyRaw).trimmed());
    if (p.hasSender) {
        QString color = p.color;
        if (isChat) {
            color = (!selfAlias.isEmpty() && p.sender == selfAlias)
                ? QStringLiteral("#16569E")
                : colorForSender(p.sender);
        }
        return QStringLiteral(
            "<div style='margin:3px 0 0 0'>"
            "<b style='color:%1'>%2</b> "
            "<span style='color:#888; font-size:smaller'>%3 %4</span></div>"
            "<div style='margin:0'>%5</div>"
        ).arg(color, p.sender.toHtmlEscaped(), p.time, datePart, body);
    }
    return QStringLiteral(
        "<div style='margin:3px 0 0 0'>"
        "<span style='color:#888; font-size:smaller'>%1 %2</span></div>"
        "<div style='margin:0; color:#888'>%3</div>"
    ).arg(p.time, datePart, body);
}

QString reformatLogMessages(const QString &html, time_t baseDate,
                            bool isChat, const QString &selfAlias)
{
    QString datePart = QDateTime::fromSecsSinceEpoch(baseDate)
                       .toString(QStringLiteral("yyyy-MM-dd"));

    // Strip the <h1>Conversation with…</h1> preamble + the surrounding <p>
    // wrapper. The wrapper would otherwise nest the divs inside a paragraph
    // and inherit Qt's default paragraph left-margin, throwing off
    // indentation consistency between messages.
    QString cleaned = html;
    cleaned.remove(QRegularExpression(
        QStringLiteral("<h1>.*?</h1>"),
        QRegularExpression::CaseInsensitiveOption
            | QRegularExpression::DotMatchesEverythingOption));
    cleaned.remove(QRegularExpression(
        QStringLiteral("</?p[^>]*>"),
        QRegularExpression::CaseInsensitiveOption));

    // Split only on the logger's terminator "<br>\n" — that's how it
    // separates messages. Bare "<br>" inside a body (from multi-line text
    // we sent ourselves) must stay so multi-line messages render
    // intact in the rendered output.
    static const QRegularExpression sepRe(
        QStringLiteral("<br\\s*/?\\s*>\\s*\\n+"),
        QRegularExpression::CaseInsensitiveOption);

    QString out;
    for (const QString &line : cleaned.split(sepRe))
        out += reformatLogLine(line, datePart, isChat, selfAlias);
    return out;
}

} // namespace konqix
