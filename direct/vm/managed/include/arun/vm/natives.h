// Host implementations of the core library's `native` methods (ADR 0004).
//
// Contract for every native (see NativeFn): arguments are in registers
// [0, arg_words) of the native frame, instance methods have `this` in register
// 0, the return value goes to *result, and failure is reported by leaving a
// Java exception pending. A native that allocates must keep every object it
// still needs in a RootScope handle or in its argument registers: any
// allocation can run the collector.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "arun/vm/class.h"
#include "arun/vm/object.h"

namespace arun::vm {

class Runtime;
class Thread;
struct Frame;

// Registers all core-library natives with `rt`. Called once from Runtime::Init.
void RegisterCoreNatives(Runtime* rt);

namespace natives {

void RegisterLang(Runtime* rt);     // natives_lang.cc
void RegisterReflect(Runtime* rt);  // natives_reflect.cc
void RegisterThread(Runtime* rt);   // natives_thread.cc
void RegisterIo(Runtime* rt);       // natives_io.cc

// "java.lang.String" / "[Ljava.lang.String;" / "int": Class.getName() form.
std::string BinaryName(const RtClass* klass);
// "java.lang.String" -> "Ljava/lang/String;"; "[I" stays; false if malformed.
bool ClassNameToDescriptor(std::string_view name, std::string* descriptor);
// Parameter descriptors of a method signature "(I[JLx/Y;)V".
std::vector<std::string> ParameterDescriptors(std::string_view signature);
std::string_view ReturnDescriptor(std::string_view signature);

// The ClassLoaderRt behind a java.lang.ClassLoader (nullptr = boot).
ClassLoaderRt* LoaderFromJava(Runtime* rt, Object* loader);
// The class loader of the method `depth` frames above the native's caller.
ClassLoaderRt* CallerLoader(Thread* self, int depth);

// Boxing for reflection: returns nullptr with an exception pending on failure.
Object* Box(Thread* self, char kind, std::uint64_t raw);
// Unboxes (with widening, JLS 5.1.2) `value` to primitive `kind`; false with
// IllegalArgumentException pending if not convertible.
bool Unbox(Thread* self, Object* value, char kind, std::uint64_t* raw);

}  // namespace natives
}  // namespace arun::vm
