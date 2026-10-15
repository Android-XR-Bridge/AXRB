/*
 * Copyright (C) 2024 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef NATIVE_BRIDGE_SUPPORT_ARM64_GUEST_STATE_GUEST_STATE_CPU_STATE_H_
#define NATIVE_BRIDGE_SUPPORT_ARM64_GUEST_STATE_GUEST_STATE_CPU_STATE_H_

#include <cstdint>

namespace berberis {

using GuestAddr = uintptr_t;
using Reservation = __uint128_t;

struct CPUState {
  // General registers.
  uint64_t x[31];

  // Flags
  // clang-format off
#if defined(__x86_64__)
  enum FlagMask {
    kFlagNegative = 1 << 15,
    kFlagZero     = 1 << 14,
    kFlagCarry    = 1 << 8,
    kFlagOverflow = 1,
  };
#else
  enum FlagMask {
    kFlagNegative = 1 << 3,
    kFlagZero     = 1 << 2,
    kFlagCarry    = 1 << 1,
    kFlagOverflow = 1,
  };
#endif

  static constexpr uint32_t kFpsrQcBit = 1U << 27;

  // clang-format on
  uint16_t flags;

  // Caches last-written FPCR, to minimize reads of host register
  uint32_t cached_fpcr;

  // Stores the FPSR flags whose functionality we emulate: currently only IDC. (later IXC)
  uint32_t emulated_fpsr;

  // Stack pointer.
  uint64_t sp;

  // SIMD & FP registers.
  alignas(16) __uint128_t v[32];

  // Current insn address.
  uint64_t insn_addr;

  GuestAddr reservation_address;
  Reservation reservation_value;
};

}
#endif  // NATIVE_BRIDGE_SUPPORT_BRIDGE_SUPPORT_ARM64_GUEST_STATE_GUEST_STATE_CPU_STATE_H_

