// SPDX-License-Identifier: GPL-3.0-only
#pragma once

namespace RBX { namespace Graphics {
// Spend loading time on preparing original geometry rather than pacing tiny
// batches at gameplay frame rates. Cap each burst so input/UI still advance.
inline unsigned vitaPartBatchLimit(bool prebuilding) { return prebuilding ? 512 : 128; }
inline bool vitaContinueClusterBatch(bool prebuilding, unsigned rebuilt, double elapsedMs)
{
    return prebuilding ? rebuilt < 8 && elapsedMs < 40.0 : elapsedMs <= 4.0;
}
}}
