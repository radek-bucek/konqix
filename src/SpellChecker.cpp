// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Radek Bucek

#include "SpellChecker.h"

#include <QDir>
#include <QFileInfo>
#include <QLocale>
#include <QRegularExpression>
#include <QSet>
#include <QTextCharFormat>

#include <algorithm>

#include <hunspell/hunspell.hxx>

#include <iconv.h>
#include <cstring>

namespace konqix {

// Same list of distro roots used by loadDictionary; kept module-local
// so availableLanguages and loadDictionary stay in lock-step.
static const QStringList kHunspellRoots = {
    QStringLiteral("/usr/share/hunspell"),
    QStringLiteral("/usr/share/myspell/dicts"),
    QStringLiteral("/usr/share/myspell"),
};

SpellHighlighter::SpellHighlighter(QTextDocument *doc,
                                   const QString &preferredLang)
    : QSyntaxHighlighter(doc)
{
    // Explicit user choice first; fall back to QLocale, then to en_US,
    // then disable (no underline) if even that's missing.
    if (!preferredLang.isEmpty() && loadDictionary(preferredLang))
        return;
    const QString primary = QLocale().name();   // e.g. "cs_CZ"
    if (loadDictionary(primary)) return;
    loadDictionary(QStringLiteral("en_US"));
}

void SpellHighlighter::setLanguage(const QString &localeName)
{
    m_hunspell.reset();
    m_dictName.clear();
    m_dictEncoding.clear();
    m_dictIsUtf8 = true;
    if (!localeName.isEmpty() && loadDictionary(localeName)) {
        rehighlight();
        return;
    }
    if (loadDictionary(QLocale().name())) {
        rehighlight();
        return;
    }
    loadDictionary(QStringLiteral("en_US"));
    rehighlight();
}

QStringList SpellHighlighter::availableLanguages()
{
    QSet<QString> langs;
    for (const QString &root : kHunspellRoots) {
        QDir d(root);
        if (!d.exists()) continue;
        const auto affs = d.entryList(QStringList() << QStringLiteral("*.aff"),
                                      QDir::Files);
        for (const QString &aff : affs) {
            QString base = aff.left(aff.size() - 4); // strip ".aff"
            if (QFileInfo::exists(d.absoluteFilePath(base + QStringLiteral(".dic"))))
                langs.insert(base);
        }
    }
    QStringList out(langs.begin(), langs.end());
    std::sort(out.begin(), out.end());
    return out;
}

SpellHighlighter::~SpellHighlighter() = default;

bool SpellHighlighter::hasDictionary() const
{
    return m_hunspell != nullptr;
}

bool SpellHighlighter::loadDictionary(const QString &localeName)
{
    for (const QString &root : kHunspellRoots) {
        QString aff = root + QLatin1Char('/') + localeName + QStringLiteral(".aff");
        QString dic = root + QLatin1Char('/') + localeName + QStringLiteral(".dic");
        if (QFileInfo::exists(aff) && QFileInfo::exists(dic)) {
            m_hunspell = std::make_unique<Hunspell>(aff.toUtf8().constData(),
                                                    dic.toUtf8().constData());
            m_dictName = localeName;
            // Cache the dictionary's encoding (declared in the .aff
            // "SET ..." line). Czech ships as ISO-8859-2 on Fedora, so
            // passing toUtf8() naively misses every word with diacritics.
            const char *enc = m_hunspell->get_dic_encoding();
            m_dictEncoding = QByteArray(enc ? enc : "UTF-8");
            const QByteArray u = m_dictEncoding.toUpper();
            m_dictIsUtf8 = (u == "UTF-8" || u == "UTF8");
            return true;
        }
    }
    return false;
}

QByteArray SpellHighlighter::encodeForDict(const QString &word) const
{
    QByteArray utf = word.toUtf8();
    if (m_dictIsUtf8) return utf;
    iconv_t cd = iconv_open(m_dictEncoding.constData(), "UTF-8");
    if (cd == (iconv_t)-1)
        return word.toLocal8Bit();   // best-effort fallback
    QByteArray out(utf.size() * 4 + 8, '\0');
    char *in = utf.data();
    size_t inLeft = static_cast<size_t>(utf.size());
    char *outPtr = out.data();
    size_t outLeft = static_cast<size_t>(out.size());
    iconv(cd, &in, &inLeft, &outPtr, &outLeft);
    iconv_close(cd);
    out.resize(out.size() - int(outLeft));
    return out;
}

QString SpellHighlighter::decodeFromDict(const std::string &raw) const
{
    if (m_dictIsUtf8) return QString::fromUtf8(raw.c_str(), int(raw.size()));
    iconv_t cd = iconv_open("UTF-8", m_dictEncoding.constData());
    if (cd == (iconv_t)-1)
        return QString::fromLocal8Bit(raw.c_str(), int(raw.size()));
    QByteArray out(int(raw.size()) * 4 + 8, '\0');
    char *in = const_cast<char *>(raw.c_str());
    size_t inLeft = raw.size();
    char *outPtr = out.data();
    size_t outLeft = static_cast<size_t>(out.size());
    iconv(cd, &in, &inLeft, &outPtr, &outLeft);
    iconv_close(cd);
    out.resize(out.size() - int(outLeft));
    return QString::fromUtf8(out);
}

bool SpellHighlighter::isMisspelled(const QString &word) const
{
    if (!m_hunspell || word.isEmpty()) return false;
    if (word.size() < 2) return false;
    bool anyLetter = false;
    for (QChar c : word) {
        if (c.isLetter()) { anyLetter = true; break; }
    }
    if (!anyLetter) return false;
    QByteArray enc = encodeForDict(word);
    return !m_hunspell->spell(std::string(enc.constData(), enc.size()));
}

QStringList SpellHighlighter::suggest(const QString &word) const
{
    QStringList out;
    if (!m_hunspell) return out;
    QByteArray enc = encodeForDict(word);
    auto v = m_hunspell->suggest(std::string(enc.constData(), enc.size()));
    out.reserve(int(v.size()));
    for (const auto &s : v) out.append(decodeFromDict(s));
    return out;
}

void SpellHighlighter::ignoreWord(const QString &word)
{
    if (!m_hunspell) return;
    QByteArray enc = encodeForDict(word);
    m_hunspell->add(std::string(enc.constData(), enc.size()));
    rehighlight();
}

void SpellHighlighter::highlightBlock(const QString &text)
{
    if (!m_hunspell) return;
    QTextCharFormat fmt;
    fmt.setUnderlineColor(Qt::red);
    fmt.setUnderlineStyle(QTextCharFormat::SpellCheckUnderline);

    // Split on whitespace and common punctuation. We treat apostrophes as
    // word-internal so "don't" / "I'm" stay one token; everything else
    // ends a word.
    static const QRegularExpression wordRe(
        QStringLiteral(R"((?:[\p{L}\p{M}\p{N}']+))"),
        QRegularExpression::UseUnicodePropertiesOption);
    auto it = wordRe.globalMatch(text);
    while (it.hasNext()) {
        auto m = it.next();
        QString word = m.captured(0);
        if (isMisspelled(word))
            setFormat(m.capturedStart(), m.capturedLength(), fmt);
    }
}

} // namespace konqix
