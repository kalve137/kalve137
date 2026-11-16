// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>
#include <vector>

namespace kalve137::rebuild {

struct Suggestion {
    std::string check;
    std::string param;
    double from = 0.0;
    double to = 0.0;
    std::string value;
    bool fixesCheck = false;
    bool allChecksPass = false;
    int builds = 0;
    std::string source;
};

std::vector<Suggestion> suggest(const std::string& source);

} // namespace kalve137::rebuild
