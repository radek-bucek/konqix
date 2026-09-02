// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Radek Bucek

#pragma once

#include <QAbstractTextDocumentLayout>
#include <QApplication>
#include <QPainter>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFragment>

namespace konqix {

// Item delegate that renders the display text as HTML when it contains
// a '<' — otherwise defers to the default painter so existing rows keep
// their pixel-exact appearance. Used by the buddy list so a row can
// carry a small grey "(seen 3h ago)" suffix inline in a smaller font.
class HtmlItemDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter *painter,
               const QStyleOptionViewItem &option,
               const QModelIndex &index) const override
    {
        QStyleOptionViewItem opt(option);
        initStyleOption(&opt, index);

        if (!opt.text.contains(QLatin1Char('<'))) {
            QStyledItemDelegate::paint(painter, option, index);
            return;
        }

        QStyle *style = opt.widget ? opt.widget->style()
                                   : QApplication::style();

        QTextDocument doc;
        doc.setDefaultFont(opt.font);
        doc.setDocumentMargin(0);
        doc.setHtml(opt.text);
        centreSmallerFragments(&doc);

        // Draw the row frame + selection background + icon, but with the
        // text stripped so we can lay HTML on top of it ourselves.
        opt.text.clear();
        style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, opt.widget);

        const QRect textRect = style->subElementRect(
            QStyle::SE_ItemViewItemText, &opt, opt.widget);

        QAbstractTextDocumentLayout::PaintContext ctx;
        ctx.palette = opt.palette;
        const QPalette::ColorGroup cg =
            (opt.state & QStyle::State_Enabled) ? QPalette::Normal
                                                : QPalette::Disabled;
        const bool selected = opt.state & QStyle::State_Selected;
        ctx.palette.setColor(QPalette::Text,
            opt.palette.color(cg,
                selected ? QPalette::HighlightedText : QPalette::Text));

        painter->save();
        painter->translate(textRect.topLeft());
        const int docH = int(doc.size().height());
        const int dy = qMax(0, (textRect.height() - docH) / 2);
        painter->translate(0, dy);
        painter->setClipRect(QRect(QPoint(0, -dy), textRect.size()));
        doc.documentLayout()->draw(painter, ctx);
        painter->restore();
    }

    QSize sizeHint(const QStyleOptionViewItem &option,
                   const QModelIndex &index) const override
    {
        QStyleOptionViewItem opt(option);
        initStyleOption(&opt, index);
        if (!opt.text.contains(QLatin1Char('<')))
            return QStyledItemDelegate::sizeHint(option, index);

        QTextDocument doc;
        doc.setDefaultFont(opt.font);
        doc.setDocumentMargin(0);
        doc.setHtml(opt.text);
        centreSmallerFragments(&doc);
        return QSize(int(doc.idealWidth()) + 4,
                     qMax(int(doc.size().height()),
                          option.fontMetrics.lineSpacing() + 2));
    }

private:
    // QTextDocument's CSS parser is finicky: font-size percentages /
    // em units are often silently dropped, and vertical-align:middle
    // on inline spans is ignored outright. Fall back to programmatic
    // formatting: for every fragment that carries an explicit
    // font-family (our shrink marker — set only on the time span, not
    // on plain text or the grey bracket wrapper), force the target
    // smaller size and AlignMiddle so it renders centred against the
    // surrounding baseline instead of sitting flush with it.
    static void centreSmallerFragments(QTextDocument *doc)
    {
        const qreal defPt = doc->defaultFont().pointSizeF();
        if (defPt <= 0)
            return;
        const qreal targetPt = defPt * 0.8;
        QTextCursor cur(doc);
        for (QTextBlock block = doc->begin(); block.isValid();
             block = block.next()) {
            for (auto it = block.begin(); !it.atEnd(); ++it) {
                const QTextFragment frag = it.fragment();
                if (!frag.isValid()) continue;
                const QTextCharFormat cf = frag.charFormat();
                if (!cf.hasProperty(QTextFormat::FontFamilies)
                    && !cf.hasProperty(QTextFormat::FontFamily))
                    continue;
                QTextCharFormat fix;
                fix.setFontPointSize(targetPt);
                fix.setVerticalAlignment(QTextCharFormat::AlignMiddle);
                cur.setPosition(frag.position());
                cur.setPosition(frag.position() + frag.length(),
                                QTextCursor::KeepAnchor);
                cur.mergeCharFormat(fix);
            }
        }
    }
};

} // namespace konqix
