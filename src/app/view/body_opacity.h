// SPDX-FileCopyrightText: 2026 Danius Kalvaitis
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "app/view/pick.h"
#include "snapshot/body.h"

namespace kalve137::app {

float bodyOpacity(PickMode mode);
snapshot::Scene withBodyOpacity(snapshot::Scene scene, PickMode mode);

} // namespace kalve137::app
