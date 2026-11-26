// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#pragma once

namespace musacad::app {

/// Windows only. On a laptop with an integrated and a dedicated GPU, Windows runs a program on
/// the integrated one unless told otherwise ("Let Windows decide"), and the viewport then lags.
/// The executable already exports the variables NVIDIA Optimus and AMD switchable graphics
/// read to pick the dedicated GPU, as games do. This also sets Windows' own preference for
/// musacad_app.exe to High performance (Settings > System > Display > Graphics), the first
/// time only: a choice the user made there is left as it is. It takes effect from the next
/// start. A no-op on other platforms.
#ifdef _WIN32
void prefer_high_performance_gpu() noexcept;
#else
inline void prefer_high_performance_gpu() noexcept {}
#endif

} // namespace musacad::app
