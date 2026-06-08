// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Radek Bucek

#pragma once

#include <QString>
#include <QStringList>
#include <QSyntaxHighlighter>

#include <memory>

class Hunspell;

namespace konqix {

// Lightweight QSyntaxHighlighter that checks each whitespace-delimited
// word against a Hunspell dictionary and underlines unknown ones with a
// red squiggle. The dictionary is auto-picked from QLocale on startup
// (defaults to en_US when nothing matches) and can be queried for
// suggestions to drive the input's right-click menu.
//
// Compiled into the binary only when HAVE_HUNSPELL is defined.
class SpellHighlighter : public QSyntaxHighlighter
{
    Q_OBJECT
public:
    // preferredLang empty → autodetect from QLocale, fall back to en_US.
    explicit SpellHighlighter(QTextDocument *doc,
                              const QString &preferredLang = QString());
    ~SpellHighlighter() override;

    bool hasDictionary() const;
    QString dictionaryName() const { return m_dictName; }

    bool isMisspelled(const QString &word) const;
    QStringList suggest(const QString &word) const;

    // Switch to a new dictionary on the fly. Empty string → autodetect
    // from QLocale, then en_US as the last fallback. Triggers rehighlight.
    void setLanguage(const QString &localeName);

    // Add word to the in-memory dictionary so it stops getting
    // underlined for this session.
    void ignoreWord(const QString &word);

    // Scan the standard Hunspell dictionary roots and return every
    // locale name (e.g. "cs_CZ", "en_US") that has both .aff and .dic.
    // Sorted alphabetically.
    static QStringList availableLanguages();

protected:
    void highlightBlock(const QString &text) override;

private:
    bool loadDictionary(const QString &localeName);
    // The Hunspell dictionary is in whatever encoding the .aff file
    // declares (Czech ships as ISO-8859-2 on Fedora). Convert per-call
    // via iconv. Both helpers fall back gracefully if iconv refuses the
    // encoding name.
    QByteArray encodeForDict(const QString &word) const;
    QString    decodeFromDict(const std::string &raw) const;

    std::unique_ptr<Hunspell> m_hunspell;
    QString m_dictName;
    QByteArray m_dictEncoding;   // e.g. "UTF-8", "ISO8859-2"
    bool m_dictIsUtf8 = true;
};

} // namespace konqix
