// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#include "look/image.h"

#include "model/select.h"

#include <QDir>
#include <QDirIterator>
#include <QFont>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>
#include <map>

namespace kalve137::look {

namespace {

QFont labelFont(double pixels)
{
    QFont font("Roboto");
    font.setPixelSize(static_cast<int>(std::lround(pixels)));
    return font;
}

} // namespace

void loadFonts(const QString& applicationDir)
{
    QDirIterator it(applicationDir + "/fonts", {"*.ttf"}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        QFontDatabase::addApplicationFont(it.next());
    }
}

QImage toImage(const Frame& frame)
{
    QImage image(frame.width, frame.height, QImage::Format_ARGB32);
    for (int y = 0; y < frame.height; ++y) {
        std::copy_n(frame.pixels.begin() + static_cast<std::ptrdiff_t>(y) * frame.width, frame.width,
                    reinterpret_cast<std::uint32_t*>(image.scanLine(y)));
    }
    return image;
}

std::vector<Label> labelAnchors(const Frame& frame, int minimumPixels)
{
    struct Sum {
        double x = 0;
        double y = 0;
        int count = 0;
    };
    std::map<int, Sum> sums;
    for (int y = 0; y < frame.height; ++y) {
        for (int x = 0; x < frame.width; ++x) {
            const int face = frame.faces[static_cast<std::size_t>(y) * frame.width + x];
            if (face >= 0) {
                Sum& s = sums[face];
                s.x += x;
                s.y += y;
                ++s.count;
            }
        }
    }
    std::vector<Label> labels;
    for (const auto& [face, sum] : sums) {
        if (sum.count < minimumPixels) {
            continue;
        }
        const double cx = sum.x / sum.count;
        const double cy = sum.y / sum.count;
        double best = 1e300;
        QPointF anchor(cx, cy);
        for (int y = 2; y < frame.height - 2; ++y) {
            for (int x = 2; x < frame.width - 2; ++x) {
                if (frame.faceAt(x, y) != face || frame.faceAt(x - 2, y) != face || frame.faceAt(x + 2, y) != face ||
                    frame.faceAt(x, y - 2) != face || frame.faceAt(x, y + 2) != face) {
                    continue;
                }
                const double d = (x - cx) * (x - cx) + (y - cy) * (y - cy);
                if (d < best) {
                    best = d;
                    anchor = QPointF(x + 0.5, y + 0.5);
                }
            }
        }
        labels.push_back({face, anchor, sum.count});
    }
    std::sort(labels.begin(), labels.end(), [](const Label& a, const Label& b) {
        return a.pixels != b.pixels ? a.pixels > b.pixels : a.face < b.face;
    });
    return labels;
}

void drawLabels(QImage& image, const Frame& frame, const model::Snapshot& snapshot, double scale,
                const std::vector<int>& only)
{
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    const QFont font = labelFont(11 * scale);
    painter.setFont(font);
    const QFontMetricsF metrics(font);
    std::vector<QRectF> placed;
    const double pad = 3.0 * scale;
    for (const Label& label : labelAnchors(frame, static_cast<int>(60 * scale * scale))) {
        if (!only.empty() && std::find(only.begin(), only.end(), label.face) == only.end()) {
            continue;
        }
        const QString text = QString::fromStdString(model::selectorFor(snapshot, label.face));
        const QSizeF size(metrics.horizontalAdvance(text) + 2 * pad, metrics.height() + pad);
        QRectF box(label.anchor - QPointF(size.width() / 2.0, size.height() / 2.0), size);
        box.moveLeft(std::clamp(box.left(), 2.0, image.width() - box.width() - 2.0));
        box.moveTop(std::clamp(box.top(), 2.0, image.height() - box.height() - 2.0));
        const bool overlaps = std::any_of(placed.begin(), placed.end(), [&](const QRectF& other) {
            return other.adjusted(-2, -2, 2, 2).intersects(box);
        });
        if (overlaps) {
            continue;
        }
        placed.push_back(box);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(20, 23, 28, 215));
        painter.drawRoundedRect(box, 4 * scale, 4 * scale);
        painter.setPen(QColor(236, 238, 241));
        painter.drawText(box, Qt::AlignCenter, text);
    }
}

void drawCaption(QImage& image, const QString& text, double scale)
{
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setFont(labelFont(12 * scale));
    painter.setPen(QColor(200, 205, 212));
    painter.drawText(QRectF(10 * scale, 8 * scale, image.width() - 20 * scale, image.height() - 16 * scale),
                     Qt::AlignLeft | Qt::AlignTop, text);
}

} // namespace kalve137::look
