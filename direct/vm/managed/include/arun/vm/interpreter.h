// The DEX bytecode interpreter (ADR 0004).
//
// Memory safety without a full verifier: before a method first runs,
// PrepareMethod checks every instruction boundary, register operand, constant
// pool index, branch/switch target and payload. At run time, references are only
// ever read from a frame's reference array, and every field/array/invoke
// operation checks the object's actual class first. Hostile bytecode can make a
// program wrong, but not make the host read or write outside an object.
#pragma once

#include <cstdint>

#include "arun/status.h"
#include "arun/vm/class.h"
#include "arun/vm/object.h"

namespace arun::vm {

class Thread;
struct Frame;

// Validates a method's code once; cached on the method. Returns the same status
// on every call.
Status PrepareMethod(RtMethod* method);

// Calls `method` with its arguments already in the callee frame registers
// [num_regs - arg_words, num_regs). Handles natives, synchronized methods and
// class initialisation for static methods. Pops nothing: the caller owns `frame`.
// On return, either `*result` holds the return value or an exception is pending.
void InvokeFrame(Thread* self, RtMethod* method, Frame* frame, JValue* result);

// Number of registers a callee frame for `method` needs.
std::uint32_t FrameSizeFor(RtMethod* method);

}  // namespace arun::vm
