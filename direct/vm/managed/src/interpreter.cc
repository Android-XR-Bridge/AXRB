#include "arun/vm/interpreter.h"

#include <bit>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <format>
#include <limits>
#include <mutex>
#include <vector>

#include "arun/log.h"
#include "arun/vm/class_linker.h"
#include "arun/vm/dex_instruction.h"
#include "arun/vm/monitor.h"
#include "arun/vm/runtime.h"
#include "arun/vm/thread.h"

namespace arun::vm {
namespace {

// ---------------------------------------------------------------------------
// Code checking (runs once per method)
// ---------------------------------------------------------------------------

// Operand positions for the wide-register table.
enum : std::uint8_t { kWideA = 1, kWideB = 2, kWideC = 4 };

std::uint8_t WideOperands(std::uint8_t op) {
  switch (op) {
    case 0x04: case 0x05: case 0x06: return kWideA | kWideB;          // move-wide*
    case 0x0b: case 0x10: return kWideA;                               // move-result-wide, return-wide
    case 0x16: case 0x17: case 0x18: case 0x19: return kWideA;         // const-wide*
    case 0x2f: case 0x30: case 0x31: return kWideB | kWideC;           // cmp*-double, cmp-long
    case 0x45: case 0x4c: return kWideA;                               // aget/aput-wide
    case 0x53: case 0x5a: return kWideA;                               // iget/iput-wide
    case 0x61: case 0x68: return kWideA;                               // sget/sput-wide
    case 0x7d: case 0x7e: case 0x80: return kWideA | kWideB;           // neg/not-long, neg-double
    case 0x81: case 0x83: case 0x88: case 0x89: return kWideA;         // *-to-long/double from 32-bit
    case 0x84: case 0x85: case 0x8a: case 0x8c: return kWideB;         // long/double to 32-bit
    case 0x86: case 0x8b: return kWideA | kWideB;                      // long<->double
    default: break;
  }
  if (op >= 0x9b && op <= 0xa2) return kWideA | kWideB | kWideC;      // long binops
  if (op >= 0xa3 && op <= 0xa5) return kWideA | kWideB;               // long shifts (C is int)
  if (op >= 0xab && op <= 0xaf) return kWideA | kWideB | kWideC;      // double binops
  if (op >= 0xbb && op <= 0xc2) return kWideA | kWideB;               // long/2addr
  if (op >= 0xc3 && op <= 0xc5) return kWideA;                        // long shift/2addr
  if (op >= 0xcb && op <= 0xcf) return kWideA | kWideB;               // double/2addr
  return 0;
}

std::uint32_t ShortyWords(std::string_view shorty, bool is_static) {
  std::uint32_t words = is_static ? 0 : 1;
  for (std::size_t i = 1; i < shorty.size(); ++i) words += (shorty[i] == 'J' || shorty[i] == 'D') ? 2 : 1;
  return words;
}

Status Bad(const RtMethod* m, std::uint32_t pc, std::string what) {
  return Err(ReasonCode::kDexMalformed,
             std::format("{} @{:#x}: {}", m->PrettyName(), pc, what));
}

Status CheckCode(const DexFile& dex, RtMethod* m, const CodeItem& code) {
  const std::uint32_t n = static_cast<std::uint32_t>(code.insns.size());
  const std::uint32_t R = code.registers_size;
  const std::uint16_t* insns = code.insns.data();
  if (code.ins_size != m->arg_words) {
    return Bad(m, 0, std::format("ins_size {} but the signature needs {} argument words",
                                 code.ins_size, m->arg_words));
  }

  std::vector<std::uint8_t> kind(n, 0);  // 1 = instruction start, 2 = payload
  struct Ref {
    std::uint32_t pc;
    std::int64_t target;
    std::uint16_t payload;  // 0 = branch target; otherwise the payload signature
  };
  std::vector<Ref> refs;

  std::uint32_t pc = 0;
  while (pc < n) {
    const std::uint16_t unit = insns[pc];
    if (unit == kPackedSwitchSignature || unit == kSparseSwitchSignature ||
        unit == kArrayDataSignature) {
      std::uint64_t width;
      if (n - pc < 4) return Bad(m, pc, "truncated payload header");
      if (unit == kPackedSwitchSignature) {
        width = 4 + 2ull * insns[pc + 1];
      } else if (unit == kSparseSwitchSignature) {
        width = 2 + 4ull * insns[pc + 1];
      } else {
        const std::uint64_t elem = insns[pc + 1];
        const std::uint64_t count = insns[pc + 2] | (static_cast<std::uint64_t>(insns[pc + 3]) << 16);
        if (elem != 1 && elem != 2 && elem != 4 && elem != 8) return Bad(m, pc, "bad array-data width");
        width = 4 + (elem * count + 1) / 2;
      }
      if (width > n - pc) return Bad(m, pc, "payload runs past the end of the method");
      kind[pc] = 2;
      pc += static_cast<std::uint32_t>(width);
      continue;
    }

    const std::uint8_t op = static_cast<std::uint8_t>(unit & 0xff);
    const OpcodeInfo& info = kOpcodes[op];
    if (info.format == Format::kUnused) return Bad(m, pc, std::format("unused opcode {:#04x}", op));
    const std::uint32_t w = FormatWidth(info.format);
    if (w > n - pc) return Bad(m, pc, std::format("{} runs past the end", info.name));
    if (op == 0x00 && unit != 0) return Bad(m, pc, "nop with non-zero high byte");
    kind[pc] = 1;
    Insn in{insns + pc};
    const std::uint8_t wide = WideOperands(op);

    auto reg = [&](std::uint32_t r, bool is_wide) -> Status {
      if (r >= R || (is_wide && r + 1 >= R)) {
        return Bad(m, pc, std::format("{} uses register v{} of {}", info.name, r, R));
      }
      return Status::Ok();
    };
    auto index = [&](std::uint32_t idx) -> Status {
      std::uint32_t limit = 0xffffffffu;
      switch (info.index) {
        case IndexKind::kString: limit = dex.num_strings(); break;
        case IndexKind::kType: limit = dex.num_types(); break;
        case IndexKind::kField: limit = dex.num_fields(); break;
        case IndexKind::kMethod: limit = dex.num_methods(); break;
        case IndexKind::kProto: limit = dex.num_protos(); break;
        default: break;
      }
      if (idx >= limit) return Bad(m, pc, std::format("{} index {} out of range", info.name, idx));
      return Status::Ok();
    };

    switch (info.format) {
      case Format::k10x: break;
      case Format::k12x:
        ARUN_RETURN_IF_ERROR(reg(in.A4(), wide & kWideA));
        ARUN_RETURN_IF_ERROR(reg(in.B4(), wide & kWideB));
        break;
      case Format::k11n: ARUN_RETURN_IF_ERROR(reg(in.A4(), false)); break;
      case Format::k11x: ARUN_RETURN_IF_ERROR(reg(in.AA(), wide & kWideA)); break;
      case Format::k10t: refs.push_back({pc, static_cast<std::int64_t>(pc) + in.Off8(), 0}); break;
      case Format::k20t: refs.push_back({pc, static_cast<std::int64_t>(pc) + in.S16_1(), 0}); break;
      case Format::k30t: refs.push_back({pc, static_cast<std::int64_t>(pc) + in.S32_1(), 0}); break;
      case Format::k22x:
        ARUN_RETURN_IF_ERROR(reg(in.AA(), wide & kWideA));
        ARUN_RETURN_IF_ERROR(reg(in.U16_1(), wide & kWideB));
        break;
      case Format::k32x:
        ARUN_RETURN_IF_ERROR(reg(in.U16_1(), wide & kWideA));
        ARUN_RETURN_IF_ERROR(reg(in.U16_2(), wide & kWideB));
        break;
      case Format::k21t:
        ARUN_RETURN_IF_ERROR(reg(in.AA(), false));
        refs.push_back({pc, static_cast<std::int64_t>(pc) + in.S16_1(), 0});
        break;
      case Format::k21s: case Format::k21h: case Format::k31i: case Format::k51l:
        ARUN_RETURN_IF_ERROR(reg(in.AA(), wide & kWideA));
        break;
      case Format::k21c:
        ARUN_RETURN_IF_ERROR(reg(in.AA(), wide & kWideA));
        ARUN_RETURN_IF_ERROR(index(in.U16_1()));
        break;
      case Format::k31c:
        ARUN_RETURN_IF_ERROR(reg(in.AA(), false));
        ARUN_RETURN_IF_ERROR(index(in.U32_1()));
        break;
      case Format::k23x:
        ARUN_RETURN_IF_ERROR(reg(in.AA(), wide & kWideA));
        ARUN_RETURN_IF_ERROR(reg(in.BB(), wide & kWideB));
        ARUN_RETURN_IF_ERROR(reg(in.CC(), wide & kWideC));
        break;
      case Format::k22b:
        ARUN_RETURN_IF_ERROR(reg(in.AA(), false));
        ARUN_RETURN_IF_ERROR(reg(in.BB(), false));
        break;
      case Format::k22t:
        ARUN_RETURN_IF_ERROR(reg(in.A4(), false));
        ARUN_RETURN_IF_ERROR(reg(in.B4(), false));
        refs.push_back({pc, static_cast<std::int64_t>(pc) + in.S16_1(), 0});
        break;
      case Format::k22s:
        ARUN_RETURN_IF_ERROR(reg(in.A4(), false));
        ARUN_RETURN_IF_ERROR(reg(in.B4(), false));
        break;
      case Format::k22c:
        ARUN_RETURN_IF_ERROR(reg(in.A4(), wide & kWideA));
        ARUN_RETURN_IF_ERROR(reg(in.B4(), false));
        ARUN_RETURN_IF_ERROR(index(in.U16_1()));
        break;
      case Format::k31t: {
        ARUN_RETURN_IF_ERROR(reg(in.AA(), false));
        const std::uint16_t sig = op == 0x26 ? kArrayDataSignature
                                  : op == 0x2b ? kPackedSwitchSignature
                                               : kSparseSwitchSignature;
        refs.push_back({pc, static_cast<std::int64_t>(pc) + in.S32_1(), sig});
        break;
      }
      case Format::k35c: case Format::k45cc: {
        const std::uint32_t count = in.ArgCount35();
        if (count > 5) return Bad(m, pc, "more than 5 arguments in a 35c instruction");
        std::uint32_t args[5];
        in.Args35(args);
        for (std::uint32_t k = 0; k < count; ++k) ARUN_RETURN_IF_ERROR(reg(args[k], false));
        if (info.index != IndexKind::kCallSite) ARUN_RETURN_IF_ERROR(index(in.U16_1()));
        if (info.index == IndexKind::kMethod && op != 0xfa) {
          const MethodId& mid = dex.GetMethodId(in.U16_1());
          const bool is_static = op == 0x71;
          const std::uint32_t words = ShortyWords(dex.ProtoShorty(mid.proto_idx), is_static);
          if (words != count) {
            return Bad(m, pc, std::format("{} passes {} words, the method takes {}", info.name,
                                          count, words));
          }
        }
        if (op == 0x24 && dex.TypeDescriptor(in.U16_1())[0] != '[') {
          return Bad(m, pc, "filled-new-array of a non-array type");
        }
        break;
      }
      case Format::k3rc: case Format::k4rcc: {
        const std::uint32_t count = in.ArgCountRange();
        const std::uint32_t first = in.FirstRegRange();
        if (first + count > R) return Bad(m, pc, "register range exceeds the frame");
        if (info.index != IndexKind::kCallSite) ARUN_RETURN_IF_ERROR(index(in.U16_1()));
        if (info.index == IndexKind::kMethod && op != 0xfb) {
          const MethodId& mid = dex.GetMethodId(in.U16_1());
          const bool is_static = op == 0x77;
          const std::uint32_t words = ShortyWords(dex.ProtoShorty(mid.proto_idx), is_static);
          if (words != count) {
            return Bad(m, pc, std::format("{} passes {} words, the method takes {}", info.name,
                                          count, words));
          }
        }
        if (op == 0x25 && dex.TypeDescriptor(in.U16_1())[0] != '[') {
          return Bad(m, pc, "filled-new-array/range of a non-array type");
        }
        break;
      }
      default:
        return Bad(m, pc, "unhandled instruction format");
    }

    // Type-shape checks the interpreter relies on.
    if (op == 0x22 && dex.TypeDescriptor(in.U16_1())[0] != 'L') return Bad(m, pc, "new-instance of a non-class type");
    if (op == 0x23 && dex.TypeDescriptor(in.U16_1())[0] != '[') return Bad(m, pc, "new-array of a non-array type");
    if ((op == 0x1c || op == 0x1f || op == 0x20)) {
      const char c = dex.TypeDescriptor(in.U16_1())[0];
      if (c != 'L' && c != '[') return Bad(m, pc, "class operand is a primitive type");
    }
    pc += w;
  }

  for (const Ref& r : refs) {
    if (r.target < 0 || r.target >= n) return Bad(m, r.pc, "branch target outside the method");
    const auto t = static_cast<std::uint32_t>(r.target);
    if (r.payload == 0) {
      if (kind[t] != 1) return Bad(m, r.pc, "branch into the middle of an instruction or payload");
      continue;
    }
    if (kind[t] != 2 || insns[t] != r.payload) return Bad(m, r.pc, "payload reference is invalid");
    if (t % 2 != 0) return Bad(m, r.pc, "payload is not 4-byte aligned");
    if (r.payload == kPackedSwitchSignature || r.payload == kSparseSwitchSignature) {
      const std::uint32_t size = insns[t + 1];
      const std::uint32_t targets_at =
          r.payload == kPackedSwitchSignature ? t + 4 : t + 2 + 2 * size;
      for (std::uint32_t k = 0; k < size; ++k) {
        const std::int32_t off = static_cast<std::int32_t>(
            insns[targets_at + 2 * k] | (static_cast<std::uint32_t>(insns[targets_at + 2 * k + 1]) << 16));
        const std::int64_t tgt = static_cast<std::int64_t>(r.pc) + off;
        if (tgt < 0 || tgt >= n || kind[static_cast<std::uint32_t>(tgt)] != 1) {
          return Bad(m, r.pc, "switch target is not an instruction");
        }
      }
    }
  }

  for (const TryItem& t : code.tries) {
    if (kind[t.start_addr] != 1) return Bad(m, t.start_addr, "try block does not start on an instruction");
    auto handlers = dex.FindCatchHandlers(code, t.start_addr);
    if (!handlers.ok()) return handlers.status();
    for (const CatchHandler& h : *handlers) {
      if (kind[h.address] != 1) return Bad(m, h.address, "catch handler is not an instruction");
    }
  }
  return Status::Ok();
}

// ---------------------------------------------------------------------------
// Execution helpers
// ---------------------------------------------------------------------------

inline float AsF(std::uint32_t v) { return std::bit_cast<float>(v); }
inline std::uint32_t FBits(float f) { return std::bit_cast<std::uint32_t>(f); }

std::int32_t F2I(double v) {
  if (std::isnan(v)) return 0;
  if (v >= 2147483647.0) return std::numeric_limits<std::int32_t>::max();
  if (v <= -2147483648.0) return std::numeric_limits<std::int32_t>::min();
  return static_cast<std::int32_t>(v);
}
std::int64_t F2L(double v) {
  if (std::isnan(v)) return 0;
  if (v >= 9223372036854775807.0) return std::numeric_limits<std::int64_t>::max();
  if (v <= -9223372036854775808.0) return std::numeric_limits<std::int64_t>::min();
  return static_cast<std::int64_t>(v);
}

// Integer binary op by index (add sub mul div rem and or xor shl shr ushr).
// Returns false for division by zero.
bool IntOp(int idx, std::int32_t a, std::int32_t b, std::int32_t* out) {
  const auto ua = static_cast<std::uint32_t>(a), ub = static_cast<std::uint32_t>(b);
  switch (idx) {
    case 0: *out = static_cast<std::int32_t>(ua + ub); return true;
    case 1: *out = static_cast<std::int32_t>(ua - ub); return true;
    case 2: *out = static_cast<std::int32_t>(ua * ub); return true;
    case 3:
      if (b == 0) return false;
      *out = (a == std::numeric_limits<std::int32_t>::min() && b == -1) ? a : a / b;
      return true;
    case 4:
      if (b == 0) return false;
      *out = (b == -1) ? 0 : a % b;
      return true;
    case 5: *out = a & b; return true;
    case 6: *out = a | b; return true;
    case 7: *out = a ^ b; return true;
    case 8: *out = static_cast<std::int32_t>(ua << (b & 31)); return true;
    case 9: *out = a >> (b & 31); return true;
    case 10: *out = static_cast<std::int32_t>(ua >> (b & 31)); return true;
    default: return true;
  }
}

bool LongOp(int idx, std::int64_t a, std::int64_t b, std::int64_t* out) {
  const auto ua = static_cast<std::uint64_t>(a), ub = static_cast<std::uint64_t>(b);
  switch (idx) {
    case 0: *out = static_cast<std::int64_t>(ua + ub); return true;
    case 1: *out = static_cast<std::int64_t>(ua - ub); return true;
    case 2: *out = static_cast<std::int64_t>(ua * ub); return true;
    case 3:
      if (b == 0) return false;
      *out = (a == std::numeric_limits<std::int64_t>::min() && b == -1) ? a : a / b;
      return true;
    case 4:
      if (b == 0) return false;
      *out = (b == -1) ? 0 : a % b;
      return true;
    case 5: *out = a & b; return true;
    case 6: *out = a | b; return true;
    case 7: *out = a ^ b; return true;
    case 8: *out = static_cast<std::int64_t>(ua << (b & 63)); return true;
    case 9: *out = a >> (b & 63); return true;
    case 10: *out = static_cast<std::int64_t>(ua >> (b & 63)); return true;
    default: return true;
  }
}

template <typename T>
T FloatOp(int idx, T a, T b) {
  switch (idx) {
    case 0: return a + b;
    case 1: return a - b;
    case 2: return a * b;
    case 3: return a / b;
    default: return std::fmod(a, b);
  }
}

// Does a field/array access variant (0 = int/float, 1 = wide, 2 = object,
// 3 = boolean, 4 = byte, 5 = char, 6 = short) match the declared kind? Object
// versus primitive must match exactly: that is the memory-safety line. Same-size
// primitives are accepted interchangeably where the verifier would.
bool FieldKindMatches(int variant, char kind) {
  switch (variant) {
    case 0: return kind == 'I' || kind == 'F';
    case 1: return kind == 'J' || kind == 'D';
    case 2: return kind == 'L';
    case 3: return kind == 'Z';
    case 4: return kind == 'B';
    case 5: return kind == 'C';
    case 6: return kind == 'S';
    default: return false;
  }
}
bool ArrayKindMatches(int variant, char kind) {
  switch (variant) {
    case 0: return kind == 'I' || kind == 'F';
    case 1: return kind == 'J' || kind == 'D';
    case 2: return kind == 'L';
    case 3: case 4: return kind == 'Z' || kind == 'B';
    case 5: case 6: return kind == 'C' || kind == 'S';
    default: return false;
  }
}

char ComponentKind(RtClass* array_class) {
  RtClass* c = array_class->component;
  return c->IsPrimitive() ? c->primitive : 'L';
}

// Loads/stores a field slot of the given kind, honouring volatile.
std::uint64_t LoadSlot(Slot* s, bool is_volatile) {
  if (is_volatile) return std::atomic_ref<std::uint64_t>(s->raw).load(std::memory_order_seq_cst);
  return s->raw;
}
void StoreSlot(Slot* s, std::uint64_t v, bool is_volatile) {
  if (is_volatile) {
    std::atomic_ref<std::uint64_t>(s->raw).store(v, std::memory_order_seq_cst);
  } else {
    s->raw = v;
  }
}

// Canonical slot value for a 32-bit register written to a narrower field.
std::uint64_t NarrowForKind(char kind, std::uint32_t v) {
  switch (kind) {
    case 'Z': return v & 1u;
    case 'B': return static_cast<std::uint64_t>(static_cast<std::int64_t>(static_cast<std::int8_t>(v)));
    case 'C': return v & 0xffffu;
    case 'S': return static_cast<std::uint64_t>(static_cast<std::int64_t>(static_cast<std::int16_t>(v)));
    default: return v;
  }
}

std::string DescriptorToJava(std::string_view d) {
  std::string out;
  int dims = 0;
  while (!d.empty() && d[0] == '[') {
    ++dims;
    d.remove_prefix(1);
  }
  if (!d.empty() && d[0] == 'L') {
    out.assign(d.substr(1, d.size() - 2));
    for (char& c : out) if (c == '/') c = '.';
  } else if (!d.empty()) {
    switch (d[0]) {
      case 'Z': out = "boolean"; break;
      case 'B': out = "byte"; break;
      case 'C': out = "char"; break;
      case 'S': out = "short"; break;
      case 'I': out = "int"; break;
      case 'J': out = "long"; break;
      case 'F': out = "float"; break;
      case 'D': out = "double"; break;
      default: out = std::string(d);
    }
  }
  for (int i = 0; i < dims; ++i) out += "[]";
  return out;
}

void CallNative(Thread* self, RtMethod* method, Frame* frame, JValue* result) {
  Runtime* rt = self->runtime();
  NativeFn fn = method->native;
  if (fn == nullptr) {
    fn = rt->FindNative(method);
    if (fn != nullptr) method->native = fn;
  }
  if (fn != nullptr) {
    fn(self, frame, result);
    return;
  }
  if (rt->options().invoke_guest_native && rt->options().invoke_guest_native(self, method, frame, result)) return;
  rt->ThrowNew(self, "Ljava/lang/UnsatisfiedLinkError;",
               "No implementation found for " + method->PrettyName());
}

void Execute(Thread* self, Frame* f, JValue* result);

}  // namespace

// ---------------------------------------------------------------------------
// Public entry points
// ---------------------------------------------------------------------------

Status PrepareMethod(RtMethod* m) {
  PrepareState st = m->prepare_state.load(std::memory_order_acquire);
  if (st == PrepareState::kPrepared) return Status::Ok();
  static std::mutex prepare_lock;
  std::lock_guard<std::mutex> g(prepare_lock);
  st = m->prepare_state.load(std::memory_order_acquire);
  if (st == PrepareState::kPrepared) return Status::Ok();
  if (st == PrepareState::kFailed) return Err(ReasonCode::kDexMalformed, m->prepare_error);

  const DexFile& dex = *m->declaring->dex_cache->dex;
  auto code = dex.ReadCodeItem(m->code_off);
  Status s = code.ok() ? CheckCode(dex, m, *code) : code.status();
  if (!s.ok()) {
    m->prepare_error = s.message();
    m->prepare_state.store(PrepareState::kFailed, std::memory_order_release);
    ARUN_LOG_ERROR(log::Category::kRuntime, "rejecting {}: {}", m->PrettyName(), s.message());
    return s;
  }
  m->code = *code;
  m->prepare_state.store(PrepareState::kPrepared, std::memory_order_release);
  return Status::Ok();
}

std::uint32_t FrameSizeFor(RtMethod* method) {
  if (method->IsNative() || method->IsAbstract()) return method->arg_words;
  return method->code.registers_size;
}

// ARUN_TRACE_METHODS=<descriptor prefix>[,<prefix>...]: logs every call of a
// matching method (e.g. "Lcom/unity3d/"), for following an app's startup.
static bool TraceMethod(const RtMethod* method) {
  static const std::vector<std::string> prefixes = [] {
    std::vector<std::string> out;
    const char* env = std::getenv("ARUN_TRACE_METHODS");
    if (env == nullptr) return out;
    std::string s(env);
    for (std::size_t start = 0; start <= s.size();) {
      std::size_t end = s.find(',', start);
      if (end == std::string::npos) end = s.size();
      if (end > start) out.push_back(s.substr(start, end - start));
      start = end + 1;
    }
    return out;
  }();
  if (prefixes.empty()) return false;
  for (const std::string& p : prefixes) {
    if (method->declaring->descriptor.compare(0, p.size(), p) == 0) return true;
  }
  return false;
}

void InvokeFrame(Thread* self, RtMethod* method, Frame* frame, JValue* result) {
  Runtime* rt = self->runtime();
  result->raw = 0;
  if (TraceMethod(method)) rt->Log(3, "trace", self->name() + ": " + method->PrettyName());
  if (method->IsSynchronized()) {
    Object* lock = method->IsStatic()
                       ? rt->ClassMirror(self, method->declaring)
                       : frame->refs[frame->num_regs - method->arg_words];
    if (lock == nullptr || !MonitorEnter(self, lock)) return;
    frame->sync = lock;
  }
  if (method->IsNative()) {
    CallNative(self, method, frame, result);
  } else if (method->IsAbstract()) {
    rt->ThrowNew(self, "Ljava/lang/AbstractMethodError;", method->PrettyName());
  } else {
    Execute(self, frame, result);
  }
  if (frame->sync != nullptr) {
    // Release the monitor on every exit path without losing the method's own
    // exception, if it threw one.
    Object* pending = self->exception();
    self->ClearException();
    MonitorExit(self, frame->sync);
    if (pending != nullptr) self->SetException(pending);
    frame->sync = nullptr;
  }
  if (method->ReturnKind() == 'L' && !self->IsExceptionPending()) self->result_ref = result->l;
}

namespace {

// ---------------------------------------------------------------------------
// The interpreter loop
// ---------------------------------------------------------------------------

void Execute(Thread* self, Frame* f, JValue* result) {
  RtMethod* const m = f->method;
  Runtime* const rt = self->runtime();
  ClassLinker& cl = rt->class_linker();
  DexCache* const dc = m->declaring->dex_cache;
  const DexFile& dex = *dc->dex;
  const std::uint16_t* const insns = m->code.insns.data();
  const std::uint32_t n = static_cast<std::uint32_t>(m->code.insns.size());
  std::uint32_t* const regs = f->regs;
  Object** const refs = f->refs;
  std::uint32_t pc = 0;

  auto set_i = [&](std::uint32_t r, std::int32_t v) {
    regs[r] = static_cast<std::uint32_t>(v);
    refs[r] = nullptr;
  };
  auto set_j = [&](std::uint32_t r, std::int64_t v) {
    const auto u = static_cast<std::uint64_t>(v);
    regs[r] = static_cast<std::uint32_t>(u);
    regs[r + 1] = static_cast<std::uint32_t>(u >> 32);
    refs[r] = nullptr;
    refs[r + 1] = nullptr;
  };
  auto set_l = [&](std::uint32_t r, Object* o) {
    refs[r] = o;
    regs[r] = o != nullptr ? 1u : 0u;  // lets if-eqz/if-nez test references
  };
  auto get_i = [&](std::uint32_t r) { return static_cast<std::int32_t>(regs[r]); };
  auto get_j = [&](std::uint32_t r) {
    return static_cast<std::int64_t>(static_cast<std::uint64_t>(regs[r]) |
                                     (static_cast<std::uint64_t>(regs[r + 1]) << 32));
  };
  auto get_d = [&](std::uint32_t r) { return std::bit_cast<double>(get_j(r)); };
  auto set_d = [&](std::uint32_t r, double v) { set_j(r, std::bit_cast<std::int64_t>(v)); };
  auto get_f = [&](std::uint32_t r) { return AsF(regs[r]); };
  auto set_f = [&](std::uint32_t r, float v) {
    regs[r] = FBits(v);
    refs[r] = nullptr;
  };
  auto throw_new = [&](std::string_view desc, std::string_view msg) { rt->ThrowNew(self, desc, msg); };
  auto npe = [&](std::string_view what) { throw_new("Ljava/lang/NullPointerException;", what); };

  for (;;) {
    if (pc >= n) {
      throw_new("Ljava/lang/VerifyError;",
                std::format("{}: execution ran off the end of the method", m->PrettyName()));
      goto exception;
    }
    f->dex_pc = pc;
    {
      const Insn in{insns + pc};
      const std::uint8_t op = in.op();
      switch (op) {
        case 0x00:  // nop
          if (insns[pc] != 0) {
            throw_new("Ljava/lang/VerifyError;", "execution reached a switch/array payload");
            goto exception;
          }
          pc += 1;
          break;

        // --- moves ---------------------------------------------------------
        case 0x01: set_i(in.A4(), get_i(in.B4())); pc += 1; break;
        case 0x02: set_i(in.AA(), get_i(in.U16_1())); pc += 2; break;
        case 0x03: set_i(in.U16_1(), get_i(in.U16_2())); pc += 3; break;
        case 0x04: set_j(in.A4(), get_j(in.B4())); pc += 1; break;
        case 0x05: set_j(in.AA(), get_j(in.U16_1())); pc += 2; break;
        case 0x06: set_j(in.U16_1(), get_j(in.U16_2())); pc += 3; break;
        case 0x07: set_l(in.A4(), refs[in.B4()]); pc += 1; break;
        case 0x08: set_l(in.AA(), refs[in.U16_1()]); pc += 2; break;
        case 0x09: set_l(in.U16_1(), refs[in.U16_2()]); pc += 3; break;
        case 0x0a: set_i(in.AA(), self->result.i); pc += 1; break;
        case 0x0b: set_j(in.AA(), self->result.j); pc += 1; break;
        case 0x0c:
          set_l(in.AA(), self->result_ref);
          self->result_ref = nullptr;
          pc += 1;
          break;
        case 0x0d:
          set_l(in.AA(), f->caught);
          f->caught = nullptr;
          pc += 1;
          break;

        // --- returns -------------------------------------------------------
        case 0x0e: result->raw = 0; return;
        case 0x0f: result->raw = 0; result->i = get_i(in.AA()); return;
        case 0x10: result->j = get_j(in.AA()); return;
        case 0x11:
          result->l = refs[in.AA()];
          self->result_ref = result->l;
          return;

        // --- constants -----------------------------------------------------
        case 0x12: set_i(in.A4(), in.Lit4()); pc += 1; break;
        case 0x13: set_i(in.AA(), in.S16_1()); pc += 2; break;
        case 0x14: set_i(in.AA(), in.S32_1()); pc += 3; break;
        case 0x15: set_i(in.AA(), static_cast<std::int32_t>(static_cast<std::uint32_t>(in.U16_1()) << 16)); pc += 2; break;
        case 0x16: set_j(in.AA(), in.S16_1()); pc += 2; break;
        case 0x17: set_j(in.AA(), in.S32_1()); pc += 3; break;
        case 0x18: set_j(in.AA(), static_cast<std::int64_t>(in.U64_1())); pc += 5; break;
        case 0x19: set_j(in.AA(), static_cast<std::int64_t>(static_cast<std::uint64_t>(in.U16_1()) << 48)); pc += 2; break;
        case 0x1a: case 0x1b: {
          const std::uint32_t idx = op == 0x1a ? in.U16_1() : in.U32_1();
          Object* s = cl.ResolveString(self, dc, idx);
          if (s == nullptr) goto exception;
          set_l(in.AA(), s);
          pc += op == 0x1a ? 2 : 3;
          break;
        }
        case 0x1c: {
          RtClass* c = cl.ResolveType(self, dc, in.U16_1());
          if (c == nullptr) goto exception;
          Object* mirror = rt->ClassMirror(self, c);
          if (mirror == nullptr) goto exception;
          set_l(in.AA(), mirror);
          pc += 2;
          break;
        }

        // --- monitors ------------------------------------------------------
        case 0x1d: case 0x1e: {
          Object* o = refs[in.AA()];
          if (o == nullptr) {
            npe("Attempt to synchronize on a null object reference");
            goto exception;
          }
          if (!(op == 0x1d ? MonitorEnter(self, o) : MonitorExit(self, o))) goto exception;
          pc += 1;
          break;
        }

        // --- type checks ---------------------------------------------------
        case 0x1f: {
          Object* o = refs[in.AA()];
          if (o != nullptr) {
            RtClass* c = cl.ResolveType(self, dc, in.U16_1());
            if (c == nullptr) goto exception;
            if (!c->IsAssignableFrom(o->klass)) {
              throw_new("Ljava/lang/ClassCastException;",
                        std::format("{} cannot be cast to {}", DescriptorToJava(o->klass->descriptor),
                                    DescriptorToJava(c->descriptor)));
              goto exception;
            }
          }
          pc += 2;
          break;
        }
        case 0x20: {
          Object* o = refs[in.B4()];
          std::int32_t r = 0;
          if (o != nullptr) {
            RtClass* c = cl.ResolveType(self, dc, in.U16_1());
            if (c == nullptr) goto exception;
            r = c->IsAssignableFrom(o->klass) ? 1 : 0;
          }
          set_i(in.A4(), r);
          pc += 2;
          break;
        }
        case 0x21: {
          Object* o = refs[in.B4()];
          if (o == nullptr) {
            npe("Attempt to get length of null array");
            goto exception;
          }
          if (!o->klass->IsArray()) {
            throw_new("Ljava/lang/VerifyError;", "array-length on a non-array");
            goto exception;
          }
          set_i(in.A4(), static_cast<Array*>(o)->length);
          pc += 1;
          break;
        }

        // --- allocation ----------------------------------------------------
        case 0x22: {
          RtClass* c = cl.ResolveType(self, dc, in.U16_1());
          if (c == nullptr) goto exception;
          if (c->IsInterface() || c->IsAbstract()) {
            throw_new("Ljava/lang/InstantiationError;", DescriptorToJava(c->descriptor));
            goto exception;
          }
          if (c == rt->wk.class_ || c == rt->wk.string) {
            // Both have runtime-managed invariants; String objects are made by
            // constructors on an instance allocated here, which is fine for String.
            if (c == rt->wk.class_) {
              throw_new("Ljava/lang/InstantiationError;", "java.lang.Class");
              goto exception;
            }
          }
          if (!cl.EnsureInitialized(self, c)) goto exception;
          Object* o = rt->heap().AllocObject(self, c);
          if (o == nullptr) goto exception;
          set_l(in.AA(), o);
          pc += 2;
          break;
        }
        case 0x23: {
          const std::int32_t len = get_i(in.B4());
          RtClass* c = cl.ResolveType(self, dc, in.U16_1());
          if (c == nullptr) goto exception;
          if (len < 0) {
            throw_new("Ljava/lang/NegativeArraySizeException;", std::to_string(len));
            goto exception;
          }
          Array* a = rt->heap().AllocArray(self, c, len);
          if (a == nullptr) goto exception;
          set_l(in.A4(), a);
          pc += 2;
          break;
        }
        case 0x24: case 0x25: {
          RtClass* c = cl.ResolveType(self, dc, in.U16_1());
          if (c == nullptr) goto exception;
          const char ck = ComponentKind(c);
          if (ck != 'I' && ck != 'L') {
            throw_new("Ljava/lang/InternalError;",
                      "filled-new-array only supports int and reference arrays");
            goto exception;
          }
          std::uint32_t argregs[255];
          std::uint32_t count;
          if (op == 0x24) {
            count = in.ArgCount35();
            std::uint32_t tmp[5];
            in.Args35(tmp);
            for (std::uint32_t k = 0; k < count; ++k) argregs[k] = tmp[k];
          } else {
            count = in.ArgCountRange();
            for (std::uint32_t k = 0; k < count; ++k) argregs[k] = in.FirstRegRange() + k;
          }
          Array* a = rt->heap().AllocArray(self, c, static_cast<std::int32_t>(count));
          if (a == nullptr) goto exception;
          for (std::uint32_t k = 0; k < count; ++k) {
            if (ck == 'I') {
              a->elements<std::int32_t>()[k] = get_i(argregs[k]);
            } else {
              Object* v = refs[argregs[k]];
              if (v != nullptr && !c->component->IsAssignableFrom(v->klass)) {
                throw_new("Ljava/lang/ArrayStoreException;", DescriptorToJava(v->klass->descriptor));
                goto exception;
              }
              a->elements<Object*>()[k] = v;
            }
          }
          self->result.raw = 0;
          self->result_ref = a;
          pc += 3;
          break;
        }
        case 0x26: {
          Object* o = refs[in.AA()];
          if (o == nullptr) {
            npe("Attempt to fill a null array");
            goto exception;
          }
          const std::uint32_t t = static_cast<std::uint32_t>(static_cast<std::int64_t>(pc) + in.S32_1());
          const std::uint32_t width = insns[t + 1];
          const std::uint32_t count = insns[t + 2] | (static_cast<std::uint32_t>(insns[t + 3]) << 16);
          Array* a = static_cast<Array*>(o);
          if (!o->klass->IsArray() || !o->klass->component->IsPrimitive() ||
              o->klass->component_size != width) {
            throw_new("Ljava/lang/VerifyError;", "fill-array-data element width mismatch");
            goto exception;
          }
          if (count > static_cast<std::uint32_t>(a->length)) {
            throw_new("Ljava/lang/ArrayIndexOutOfBoundsException;",
                      std::format("failed to fill array of length {} with {} elements", a->length, count));
            goto exception;
          }
          std::memcpy(a->data(), insns + t + 4, static_cast<std::size_t>(width) * count);
          pc += 3;
          break;
        }
        case 0x27: {
          Object* e = refs[in.AA()];
          if (e == nullptr) {
            npe("throw with null exception");
            goto exception;
          }
          if (!rt->wk.throwable->IsAssignableFrom(e->klass)) {
            throw_new("Ljava/lang/VerifyError;", "throw of a non-Throwable");
            goto exception;
          }
          self->SetException(e);
          goto exception;
        }

        // --- branches --------------------------------------------------------
        case 0x28: case 0x29: case 0x2a: {
          const std::int32_t off = op == 0x28 ? in.Off8() : op == 0x29 ? in.S16_1() : in.S32_1();
          if (off <= 0) self->Safepoint();
          pc = static_cast<std::uint32_t>(static_cast<std::int64_t>(pc) + off);
          break;
        }
        case 0x2b: case 0x2c: {
          const std::uint32_t t = static_cast<std::uint32_t>(static_cast<std::int64_t>(pc) + in.S32_1());
          const std::int32_t key = get_i(in.AA());
          const std::uint32_t size = insns[t + 1];
          auto read32 = [&](std::uint32_t at) {
            return static_cast<std::int32_t>(insns[at] | (static_cast<std::uint32_t>(insns[at + 1]) << 16));
          };
          std::int32_t off = 3;
          if (op == 0x2b) {
            const std::int32_t first = read32(t + 2);
            const std::int64_t index = static_cast<std::int64_t>(key) - first;
            if (index >= 0 && index < size) off = read32(t + 4 + 2 * static_cast<std::uint32_t>(index));
          } else {
            for (std::uint32_t k = 0; k < size; ++k) {
              if (read32(t + 2 + 2 * k) == key) {
                off = read32(t + 2 + 2 * size + 2 * k);
                break;
              }
            }
          }
          if (off <= 0) self->Safepoint();
          pc = static_cast<std::uint32_t>(static_cast<std::int64_t>(pc) + off);
          break;
        }
        case 0x2d: case 0x2e: {
          const float a = get_f(in.BB()), b = get_f(in.CC());
          std::int32_t r;
          if (std::isnan(a) || std::isnan(b)) r = op == 0x2d ? -1 : 1;
          else r = a < b ? -1 : a > b ? 1 : 0;
          set_i(in.AA(), r);
          pc += 2;
          break;
        }
        case 0x2f: case 0x30: {
          const double a = get_d(in.BB()), b = get_d(in.CC());
          std::int32_t r;
          if (std::isnan(a) || std::isnan(b)) r = op == 0x2f ? -1 : 1;
          else r = a < b ? -1 : a > b ? 1 : 0;
          set_i(in.AA(), r);
          pc += 2;
          break;
        }
        case 0x31: {
          const std::int64_t a = get_j(in.BB()), b = get_j(in.CC());
          set_i(in.AA(), a < b ? -1 : a > b ? 1 : 0);
          pc += 2;
          break;
        }
        case 0x32: case 0x33: case 0x34: case 0x35: case 0x36: case 0x37: {
          const std::uint32_t ra = in.A4(), rb = in.B4();
          bool take;
          if (op == 0x32 || op == 0x33) {
            // Works for ints and references: an int register has a null ref slot,
            // a reference register has a 0/1 regs slot.
            const bool eq = regs[ra] == regs[rb] && refs[ra] == refs[rb];
            take = op == 0x32 ? eq : !eq;
          } else {
            const std::int32_t a = get_i(ra), b = get_i(rb);
            take = op == 0x34 ? a < b : op == 0x35 ? a >= b : op == 0x36 ? a > b : a <= b;
          }
          if (take) {
            const std::int32_t off = in.S16_1();
            if (off <= 0) self->Safepoint();
            pc = static_cast<std::uint32_t>(static_cast<std::int64_t>(pc) + off);
          } else {
            pc += 2;
          }
          break;
        }
        case 0x38: case 0x39: case 0x3a: case 0x3b: case 0x3c: case 0x3d: {
          const std::int32_t a = get_i(in.AA());
          const bool take = op == 0x38 ? a == 0 : op == 0x39 ? a != 0 : op == 0x3a ? a < 0
                           : op == 0x3b ? a >= 0 : op == 0x3c ? a > 0 : a <= 0;
          if (take) {
            const std::int32_t off = in.S16_1();
            if (off <= 0) self->Safepoint();
            pc = static_cast<std::uint32_t>(static_cast<std::int64_t>(pc) + off);
          } else {
            pc += 2;
          }
          break;
        }

        // --- arrays ------------------------------------------------------------
        case 0x44: case 0x45: case 0x46: case 0x47: case 0x48: case 0x49: case 0x4a:
        case 0x4b: case 0x4c: case 0x4d: case 0x4e: case 0x4f: case 0x50: case 0x51: {
          const bool is_put = op >= 0x4b;
          const int variant = is_put ? op - 0x4b : op - 0x44;
          Object* o = refs[in.BB()];
          if (o == nullptr) {
            npe(is_put ? "Attempt to store to null array" : "Attempt to read from null array");
            goto exception;
          }
          if (!o->klass->IsArray() || !ArrayKindMatches(variant, ComponentKind(o->klass))) {
            throw_new("Ljava/lang/VerifyError;", std::format("{} on {}", kOpcodes[op].name, o->klass->descriptor));
            goto exception;
          }
          Array* a = static_cast<Array*>(o);
          const std::int32_t idx = get_i(in.CC());
          if (idx < 0 || idx >= a->length) {
            throw_new("Ljava/lang/ArrayIndexOutOfBoundsException;",
                      std::format("length={}; index={}", a->length, idx));
            goto exception;
          }
          const std::uint32_t r = in.AA();
          if (!is_put) {
            switch (variant) {
              case 0: set_i(r, a->elements<std::int32_t>()[idx]); break;
              case 1: set_j(r, a->elements<std::int64_t>()[idx]); break;
              case 2: set_l(r, a->elements<Object*>()[idx]); break;
              case 3: set_i(r, a->elements<std::uint8_t>()[idx]); break;
              case 4: set_i(r, a->elements<std::int8_t>()[idx]); break;
              case 5: set_i(r, a->elements<std::uint16_t>()[idx]); break;
              default: set_i(r, a->elements<std::int16_t>()[idx]); break;
            }
          } else {
            switch (variant) {
              case 0: a->elements<std::int32_t>()[idx] = get_i(r); break;
              case 1: a->elements<std::int64_t>()[idx] = get_j(r); break;
              case 2: {
                Object* v = refs[r];
                if (v != nullptr && !o->klass->component->IsAssignableFrom(v->klass)) {
                  throw_new("Ljava/lang/ArrayStoreException;",
                            std::format("{} cannot be stored in an array of type {}",
                                        DescriptorToJava(v->klass->descriptor),
                                        DescriptorToJava(o->klass->descriptor)));
                  goto exception;
                }
                a->elements<Object*>()[idx] = v;
                break;
              }
              case 3: case 4: a->elements<std::uint8_t>()[idx] = static_cast<std::uint8_t>(regs[r]); break;
              default: a->elements<std::uint16_t>()[idx] = static_cast<std::uint16_t>(regs[r]); break;
            }
          }
          pc += 2;
          break;
        }

        // --- instance fields ---------------------------------------------------
        case 0x52: case 0x53: case 0x54: case 0x55: case 0x56: case 0x57: case 0x58:
        case 0x59: case 0x5a: case 0x5b: case 0x5c: case 0x5d: case 0x5e: case 0x5f: {
          const bool is_put = op >= 0x59;
          const int variant = is_put ? op - 0x59 : op - 0x52;
          RtField* fld = cl.ResolveField(self, dc, in.U16_1(), false);
          if (fld == nullptr) goto exception;
          Object* o = refs[in.B4()];
          if (o == nullptr) {
            npe(std::format("Attempt to {} field '{}' on a null object reference",
                            is_put ? "write to" : "read from", fld->name));
            goto exception;
          }
          if (!o->klass->IsSubclassOf(fld->declaring) || !FieldKindMatches(variant, fld->kind)) {
            throw_new("Ljava/lang/IncompatibleClassChangeError;",
                      std::format("{} of {}.{} on {}", kOpcodes[op].name, fld->declaring->descriptor,
                                  fld->name, o->klass->descriptor));
            goto exception;
          }
          Slot* s = FieldSlots(o) + fld->slot;
          const std::uint32_t r = in.A4();
          if (!is_put) {
            const std::uint64_t v = LoadSlot(s, fld->IsVolatile());
            if (variant == 1) set_j(r, static_cast<std::int64_t>(v));
            else if (variant == 2) set_l(r, reinterpret_cast<Object*>(v));
            else set_i(r, static_cast<std::int32_t>(v));
          } else {
            std::uint64_t v;
            if (variant == 1) {
              v = static_cast<std::uint64_t>(get_j(r));
            } else if (variant == 2) {
              Object* val = refs[r];
              v = reinterpret_cast<std::uint64_t>(val);
            } else {
              v = NarrowForKind(fld->kind, regs[r]);
            }
            StoreSlot(s, v, fld->IsVolatile());
          }
          pc += 2;
          break;
        }

        // --- static fields -----------------------------------------------------
        case 0x60: case 0x61: case 0x62: case 0x63: case 0x64: case 0x65: case 0x66:
        case 0x67: case 0x68: case 0x69: case 0x6a: case 0x6b: case 0x6c: case 0x6d: {
          const bool is_put = op >= 0x67;
          const int variant = is_put ? op - 0x67 : op - 0x60;
          RtField* fld = cl.ResolveField(self, dc, in.U16_1(), true);
          if (fld == nullptr) goto exception;
          if (!FieldKindMatches(variant, fld->kind)) {
            throw_new("Ljava/lang/IncompatibleClassChangeError;",
                      std::format("{} of {}.{}", kOpcodes[op].name, fld->declaring->descriptor, fld->name));
            goto exception;
          }
          if (!cl.EnsureInitialized(self, fld->declaring)) goto exception;
          Slot* s = &fld->declaring->statics[fld->slot];
          const std::uint32_t r = in.AA();
          if (!is_put) {
            const std::uint64_t v = LoadSlot(s, fld->IsVolatile());
            if (variant == 1) set_j(r, static_cast<std::int64_t>(v));
            else if (variant == 2) set_l(r, reinterpret_cast<Object*>(v));
            else set_i(r, static_cast<std::int32_t>(v));
          } else {
            std::uint64_t v;
            if (variant == 1) v = static_cast<std::uint64_t>(get_j(r));
            else if (variant == 2) v = reinterpret_cast<std::uint64_t>(refs[r]);
            else v = NarrowForKind(fld->kind, regs[r]);
            StoreSlot(s, v, fld->IsVolatile());
          }
          pc += 2;
          break;
        }

        // --- invokes -----------------------------------------------------------
        case 0x6e: case 0x6f: case 0x70: case 0x71: case 0x72:
        case 0x74: case 0x75: case 0x76: case 0x77: case 0x78: {
          const bool range = op >= 0x74;
          const int k = range ? op - 0x74 : op - 0x6e;
          static constexpr InvokeKind kKinds[] = {InvokeKind::kVirtual, InvokeKind::kSuper,
                                                  InvokeKind::kDirect, InvokeKind::kStatic,
                                                  InvokeKind::kInterface};
          const InvokeKind kind = kKinds[k];
          std::uint32_t argregs[255];
          std::uint32_t count;
          if (!range) {
            count = in.ArgCount35();
            std::uint32_t tmp[5];
            in.Args35(tmp);
            for (std::uint32_t i = 0; i < count; ++i) argregs[i] = tmp[i];
          } else {
            count = in.ArgCountRange();
            for (std::uint32_t i = 0; i < count; ++i) argregs[i] = in.FirstRegRange() + i;
          }

          RtMethod* resolved = cl.ResolveMethod(self, dc, in.U16_1(), kind);
          if (resolved == nullptr) goto exception;
          RtMethod* target = resolved;
          Object* receiver = nullptr;
          if (kind != InvokeKind::kStatic) {
            receiver = refs[argregs[0]];
            if (receiver == nullptr) {
              npe(std::format("Attempt to invoke {} method '{}' on a null object reference",
                              kind == InvokeKind::kInterface ? "interface" : "virtual",
                              resolved->PrettyName()));
              goto exception;
            }
            if (resolved->IsStatic()) {
              throw_new("Ljava/lang/IncompatibleClassChangeError;",
                        "instance invoke of static method " + resolved->PrettyName());
              goto exception;
            }
            if (!resolved->declaring->IsAssignableFrom(receiver->klass)) {
              throw_new("Ljava/lang/IncompatibleClassChangeError;",
                        std::format("{} does not implement {}", receiver->klass->descriptor,
                                    resolved->declaring->descriptor));
              goto exception;
            }
          } else if (!resolved->IsStatic()) {
            throw_new("Ljava/lang/IncompatibleClassChangeError;",
                      "static invoke of instance method " + resolved->PrettyName());
            goto exception;
          }

          switch (kind) {
            case InvokeKind::kStatic:
              if (!cl.EnsureInitialized(self, resolved->declaring)) goto exception;
              break;
            case InvokeKind::kDirect:
              break;
            case InvokeKind::kVirtual:
            case InvokeKind::kInterface:
              if (resolved->declaring->IsInterface()) {
                target = cl.FindInterfaceTarget(self, receiver->klass, resolved);
                if (target == nullptr) goto exception;
              } else if (resolved->vtable_index >= 0) {
                target = receiver->klass->vtable[static_cast<std::size_t>(resolved->vtable_index)];
              }
              break;
            case InvokeKind::kSuper:
              if (resolved->declaring->IsInterface()) {
                target = resolved;  // Iface.super.m(): the named default method itself
              } else if (resolved->vtable_index >= 0 && m->declaring->super != nullptr &&
                         static_cast<std::size_t>(resolved->vtable_index) < m->declaring->super->vtable.size()) {
                target = m->declaring->super->vtable[static_cast<std::size_t>(resolved->vtable_index)];
              }
              break;
          }
          if (target->IsAbstract()) {
            throw_new("Ljava/lang/AbstractMethodError;", target->PrettyName());
            goto exception;
          }
          if (!target->IsNative()) {
            Status ps = PrepareMethod(target);
            if (!ps.ok()) {
              throw_new("Ljava/lang/VerifyError;", ps.message());
              goto exception;
            }
          }
          self->Safepoint();
          const std::uint32_t size = FrameSizeFor(target);
          if (count != target->arg_words || size < count) {
            throw_new("Ljava/lang/IncompatibleClassChangeError;",
                      "argument count mismatch calling " + target->PrettyName());
            goto exception;
          }
          Frame* callee = self->PushFrame(target, size);
          if (callee == nullptr) {
            throw_new("Ljava/lang/StackOverflowError;", "stack size exceeded");
            goto exception;
          }
          const std::uint32_t base = size - count;
          for (std::uint32_t i = 0; i < count; ++i) {
            callee->regs[base + i] = regs[argregs[i]];
            callee->refs[base + i] = refs[argregs[i]];
          }
          JValue r;
          InvokeFrame(self, target, callee, &r);
          self->PopFrame(callee);
          if (self->IsExceptionPending()) goto exception;
          self->result = r;
          if (target->ReturnKind() != 'L') self->result_ref = nullptr;
          pc += 3;
          break;
        }

        // --- unary ops and conversions ------------------------------------------
        case 0x7b: set_i(in.A4(), static_cast<std::int32_t>(0u - regs[in.B4()])); pc += 1; break;
        case 0x7c: set_i(in.A4(), ~get_i(in.B4())); pc += 1; break;
        case 0x7d: set_j(in.A4(), static_cast<std::int64_t>(0ull - static_cast<std::uint64_t>(get_j(in.B4())))); pc += 1; break;
        case 0x7e: set_j(in.A4(), ~get_j(in.B4())); pc += 1; break;
        case 0x7f: set_f(in.A4(), -get_f(in.B4())); pc += 1; break;
        case 0x80: set_d(in.A4(), -get_d(in.B4())); pc += 1; break;
        case 0x81: set_j(in.A4(), get_i(in.B4())); pc += 1; break;
        case 0x82: set_f(in.A4(), static_cast<float>(get_i(in.B4()))); pc += 1; break;
        case 0x83: set_d(in.A4(), static_cast<double>(get_i(in.B4()))); pc += 1; break;
        case 0x84: set_i(in.A4(), static_cast<std::int32_t>(get_j(in.B4()))); pc += 1; break;
        case 0x85: set_f(in.A4(), static_cast<float>(get_j(in.B4()))); pc += 1; break;
        case 0x86: set_d(in.A4(), static_cast<double>(get_j(in.B4()))); pc += 1; break;
        case 0x87: set_i(in.A4(), F2I(get_f(in.B4()))); pc += 1; break;
        case 0x88: set_j(in.A4(), F2L(get_f(in.B4()))); pc += 1; break;
        case 0x89: set_d(in.A4(), static_cast<double>(get_f(in.B4()))); pc += 1; break;
        case 0x8a: set_i(in.A4(), F2I(get_d(in.B4()))); pc += 1; break;
        case 0x8b: set_j(in.A4(), F2L(get_d(in.B4()))); pc += 1; break;
        case 0x8c: set_f(in.A4(), static_cast<float>(get_d(in.B4()))); pc += 1; break;
        case 0x8d: set_i(in.A4(), static_cast<std::int8_t>(regs[in.B4()])); pc += 1; break;
        case 0x8e: set_i(in.A4(), static_cast<std::uint16_t>(regs[in.B4()])); pc += 1; break;
        case 0x8f: set_i(in.A4(), static_cast<std::int16_t>(regs[in.B4()])); pc += 1; break;

        default: {
          // --- binary arithmetic -----------------------------------------------
          if ((op >= 0x90 && op <= 0x9a) || (op >= 0xb0 && op <= 0xba)) {
            const bool two = op >= 0xb0;
            const int idx = op - (two ? 0xb0 : 0x90);
            const std::uint32_t dst = two ? in.A4() : in.AA();
            const std::int32_t a = two ? get_i(in.A4()) : get_i(in.BB());
            const std::int32_t b = two ? get_i(in.B4()) : get_i(in.CC());
            std::int32_t out;
            if (!IntOp(idx, a, b, &out)) {
              throw_new("Ljava/lang/ArithmeticException;", "divide by zero");
              goto exception;
            }
            set_i(dst, out);
            pc += two ? 1 : 2;
            break;
          }
          if ((op >= 0x9b && op <= 0xa5) || (op >= 0xbb && op <= 0xc5)) {
            const bool two = op >= 0xbb;
            const int idx = op - (two ? 0xbb : 0x9b);
            const std::uint32_t dst = two ? in.A4() : in.AA();
            const std::int64_t a = two ? get_j(in.A4()) : get_j(in.BB());
            // Shift amounts are ints even for long shifts.
            const std::int64_t b = idx >= 8 ? (two ? get_i(in.B4()) : get_i(in.CC()))
                                             : (two ? get_j(in.B4()) : get_j(in.CC()));
            std::int64_t out;
            if (!LongOp(idx, a, b, &out)) {
              throw_new("Ljava/lang/ArithmeticException;", "divide by zero");
              goto exception;
            }
            set_j(dst, out);
            pc += two ? 1 : 2;
            break;
          }
          if ((op >= 0xa6 && op <= 0xaa) || (op >= 0xc6 && op <= 0xca)) {
            const bool two = op >= 0xc6;
            const int idx = op - (two ? 0xc6 : 0xa6);
            const float a = two ? get_f(in.A4()) : get_f(in.BB());
            const float b = two ? get_f(in.B4()) : get_f(in.CC());
            set_f(two ? in.A4() : in.AA(), FloatOp<float>(idx, a, b));
            pc += two ? 1 : 2;
            break;
          }
          if ((op >= 0xab && op <= 0xaf) || (op >= 0xcb && op <= 0xcf)) {
            const bool two = op >= 0xcb;
            const int idx = op - (two ? 0xcb : 0xab);
            const double a = two ? get_d(in.A4()) : get_d(in.BB());
            const double b = two ? get_d(in.B4()) : get_d(in.CC());
            set_d(two ? in.A4() : in.AA(), FloatOp<double>(idx, a, b));
            pc += two ? 1 : 2;
            break;
          }
          if (op >= 0xd0 && op <= 0xe2) {
            const bool lit16 = op <= 0xd7;
            static constexpr int kLit16Map[] = {0, -1, 2, 3, 4, 5, 6, 7};
            static constexpr int kLit8Map[] = {0, -1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
            const int idx = lit16 ? kLit16Map[op - 0xd0] : kLit8Map[op - 0xd8];
            const std::uint32_t dst = lit16 ? in.A4() : in.AA();
            const std::int32_t a = lit16 ? get_i(in.B4()) : get_i(in.BB());
            const std::int32_t lit = lit16 ? in.S16_1() : in.Lit8();
            std::int32_t out;
            if (idx < 0) {  // rsub-int: literal minus register
              out = static_cast<std::int32_t>(static_cast<std::uint32_t>(lit) - static_cast<std::uint32_t>(a));
            } else if (!IntOp(idx, a, lit, &out)) {
              throw_new("Ljava/lang/ArithmeticException;", "divide by zero");
              goto exception;
            }
            set_i(dst, out);
            pc += 2;
            break;
          }
          if (op >= 0xfa) {
            // invoke-polymorphic/custom and method-handle constants. d8 does not
            // emit these for ordinary app code (lambdas and string concatenation
            // are desugared), and this runtime has no java.lang.invoke yet. Fail
            // loudly and specifically.
            ARUN_LOG_WARN(log::Category::kRuntime, "{}: unsupported instruction {}", m->PrettyName(),
                          kOpcodes[op].name);
            throw_new("Ljava/lang/UnsupportedOperationException;",
                      std::format("{} is not implemented by this runtime (ADR 0004): {}",
                                  kOpcodes[op].name, m->PrettyName()));
            goto exception;
          }
          throw_new("Ljava/lang/VerifyError;", std::format("bad opcode {:#04x}", op));
          goto exception;
        }
      }
    }
    continue;

  exception : {
    // Find a handler in this method; otherwise return with the exception pending.
    // Resolving a catch type can allocate (and so collect), so the in-flight
    // exception is parked in the frame's rooted `caught` slot meanwhile.
    Object* ex = self->exception();
    f->caught = ex;
    auto handlers = dex.FindCatchHandlers(m->code, f->dex_pc);
    if (handlers.ok()) {
      for (const CatchHandler& h : *handlers) {
        bool match = h.type_idx == kDexNoIndex;
        if (!match) {
          self->ClearException();
          RtClass* c = cl.ResolveType(self, dc, h.type_idx);
          if (self->IsExceptionPending()) {
            // An unresolvable catch type cannot match; keep looking.
            self->ClearException();
          }
          self->SetException(ex);
          match = c != nullptr && c->IsAssignableFrom(ex->klass);
        }
        if (match) {
          self->ClearException();
          f->caught = ex;
          pc = h.address;
          goto next;
        }
      }
    }
    f->caught = nullptr;
    return;
  }
  next:;
  }
}

}  // namespace

float ArgFloat(Frame* args, std::uint32_t reg) { return std::bit_cast<float>(args->regs[reg]); }
double ArgDouble(Frame* args, std::uint32_t reg) {
  return std::bit_cast<double>(ArgLong(args, reg));
}

}  // namespace arun::vm
