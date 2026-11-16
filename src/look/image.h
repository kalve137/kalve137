// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "look/raster.h"
#include "model/snapshot.h"

#include <QImage>
#include <QString>

#include <vector>

namespace kalve137::look {

void loadFonts(const QString& applicationDir);

QImage toImage(const Frame& frame);

struct Label {
    int face = -1;
    QPointF anchor;
    int pixels = 0;
};

std::vector<Label> labelAnchors(const Frame& frame, int minimumPixels);

void drawLabels(QImage& image, const Frame& frame, const model::Snapshot& snapshot, double scale = 1.0,
                const std::vector<int>& only = {});
void drawCaption(QImage& image, const QString& text, double scale = 1.0);

} // namespace kalve137::look
