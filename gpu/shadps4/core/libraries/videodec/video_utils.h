// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/types.h"

struct AVFrame;

namespace Libraries::Videodec {

// Writes src as NV12 with a 64-byte pitch and a 16-aligned height; false if it does not fit max_size.
bool CopyNV12Data(u8* dst, u64 max_size, const AVFrame& src);

} // namespace Libraries::Videodec
