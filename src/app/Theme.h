// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Franz X. Mohr

#pragma once

class QApplication;

namespace studio
{

/// Fusion style with a restrained stylesheet on top, following the system's
/// light or dark scheme, so the application looks the same on every platform.
void applyTheme(QApplication &app);

} // namespace studio
