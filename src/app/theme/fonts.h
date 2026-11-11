// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>

namespace kalve137::app::fonts {

struct Families {
    QString qt;
    QString occtRegular;
    QString occtMedium;
};

const Families& load();

const Families& families();

} // namespace kalve137::app::fonts
