// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Radek Bucek

#pragma once

#include <QApplication>
#include <QImage>
#include <QTextDocument>
#include <QUrl>
#include <QVariant>

namespace konqix {

// QTextDocument that tags every loaded raster image with the screen's
// devicePixelRatio so QTextBrowser can render it 1:1 against the
// physical pixel grid on fractional-DPI desktops (KWin 1.25, etc.).
// Without this, images are rendered at logical-pixel size and then
// upscaled by the compositor with bilinear filtering, producing the
// blurry "rozrastrované" look the user reported.
class SmoothImageDocument : public QTextDocument
{
public:
    using QTextDocument::QTextDocument;

protected:
    QVariant loadResource(int type, const QUrl &name) override
    {
        if (type == QTextDocument::ImageResource) {
            const QString path = name.toLocalFile();
            if (!path.isEmpty()) {
                QImage img(path);
                if (!img.isNull()) {
                    const qreal dpr = qApp ? qApp->devicePixelRatio() : 1.0;
                    if (dpr > 1.001)
                        img.setDevicePixelRatio(dpr);
                    return img;
                }
            }
        }
        return QTextDocument::loadResource(type, name);
    }
};

} // namespace konqix
