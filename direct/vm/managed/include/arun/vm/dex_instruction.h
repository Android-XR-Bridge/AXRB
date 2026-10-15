// Dalvik instruction formats and operand decoding (DEX spec "Dalvik bytecode"
// and "instruction formats").
//
// Pure functions over a code-unit array. Callers must have established that the
// instruction lies entirely inside the method (see CodeChecker), so these do not
// bounds-check.
#pragma once

#include <cstdint>

namespace arun::vm {

enum class Format : std::uint8_t {
  k10x, k12x, k11n, k11x, k10t, k20t, k22x, k21t, k21s, k21h, k21c, k23x, k22b, k22t,
  k22s, k22c, k30t, k32x, k31i, k31t, k31c, k35c, k3rc, k45cc, k4rcc, k51l,
  kUnused,
};

// Size in 16-bit code units, by format.
constexpr std::uint32_t FormatWidth(Format f) {
  switch (f) {
    case Format::k10x: case Format::k12x: case Format::k11n: case Format::k11x:
    case Format::k10t:
      return 1;
    case Format::k20t: case Format::k22x: case Format::k21t: case Format::k21s:
    case Format::k21h: case Format::k21c: case Format::k23x: case Format::k22b:
    case Format::k22t: case Format::k22s: case Format::k22c:
      return 2;
    case Format::k30t: case Format::k32x: case Format::k31i: case Format::k31t:
    case Format::k31c: case Format::k35c: case Format::k3rc:
      return 3;
    case Format::k45cc: case Format::k4rcc:
      return 4;
    case Format::k51l:
      return 5;
    default:
      return 0;
  }
}

// What an instruction's constant-pool operand refers to, for index validation.
enum class IndexKind : std::uint8_t { kNone, kString, kType, kField, kMethod, kProto, kCallSite, kMethodHandle };

struct OpcodeInfo {
  const char* name;
  Format format;
  IndexKind index;
};

extern const OpcodeInfo kOpcodes[256];

// Pseudo-instruction identifiers (first code unit of a payload).
constexpr std::uint16_t kPackedSwitchSignature = 0x0100;
constexpr std::uint16_t kSparseSwitchSignature = 0x0200;
constexpr std::uint16_t kArrayDataSignature = 0x0300;

// Operand accessors. Naming follows the spec: A, B, C are the operands in
// format order; "H" suffix means high-bit nibble etc.
struct Insn {
  const std::uint16_t* p;

  std::uint8_t op() const { return static_cast<std::uint8_t>(p[0] & 0xff); }
  // 12x / 22t / 22s / 22c: A = low nibble of high byte, B = high nibble.
  std::uint32_t A4() const { return (p[0] >> 8) & 0xf; }
  std::uint32_t B4() const { return p[0] >> 12; }
  // 11n: signed 4-bit literal in B.
  std::int32_t Lit4() const { return static_cast<std::int32_t>(static_cast<std::int16_t>(p[0]) >> 12); }
  // 11x / 21* / 31* / 51l / 22x / 23x / 22b: AA = high byte.
  std::uint32_t AA() const { return p[0] >> 8; }
  std::uint16_t U16_1() const { return p[1]; }
  std::uint16_t U16_2() const { return p[2]; }
  std::int16_t S16_1() const { return static_cast<std::int16_t>(p[1]); }
  std::uint32_t U32_1() const { return static_cast<std::uint32_t>(p[1]) | (static_cast<std::uint32_t>(p[2]) << 16); }
  std::int32_t S32_1() const { return static_cast<std::int32_t>(U32_1()); }
  std::uint64_t U64_1() const {
    return static_cast<std::uint64_t>(p[1]) | (static_cast<std::uint64_t>(p[2]) << 16) |
           (static_cast<std::uint64_t>(p[3]) << 32) | (static_cast<std::uint64_t>(p[4]) << 48);
  }
  // 23x: BB = low byte of unit 1, CC = high byte of unit 1.
  std::uint32_t BB() const { return p[1] & 0xff; }
  std::uint32_t CC() const { return p[1] >> 8; }
  // 22b: signed 8-bit literal in CC.
  std::int32_t Lit8() const { return static_cast<std::int8_t>(p[1] >> 8); }
  // 10t: signed 8-bit branch offset in AA.
  std::int32_t Off8() const { return static_cast<std::int8_t>(p[0] >> 8); }

  // 35c / 45cc: argument count and registers C..G.
  std::uint32_t ArgCount35() const { return p[0] >> 12; }
  void Args35(std::uint32_t out[5]) const {
    out[0] = p[2] & 0xf;          // C
    out[1] = (p[2] >> 4) & 0xf;   // D
    out[2] = (p[2] >> 8) & 0xf;   // E
    out[3] = (p[2] >> 12) & 0xf;  // F
    out[4] = (p[0] >> 8) & 0xf;   // G
  }
  // 3rc / 4rcc: AA = count, CCCC = first register.
  std::uint32_t ArgCountRange() const { return p[0] >> 8; }
  std::uint32_t FirstRegRange() const { return p[2]; }
};

}  // namespace arun::vm
