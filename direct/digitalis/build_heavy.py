"""Build the upstream optimizing frontend and MachineIR backend on Windows."""
import os
import re
import sys
import hashlib
from pathlib import Path


def patch(text, old, new, count=1):
    """Fail visibly if the pinned upstream compatibility anchor changes."""
    actual = text.count(old)
    if actual != count:
        raise RuntimeError(f'Digitalis compatibility anchor count {actual}, expected {count}: {old[:100]!r}')
    return text.replace(old, new)


def build(source, work, flags, run, env):
    generated = work / 'heavy-generated'
    generated.mkdir(parents=True, exist_ok=True)
    env['PYTHONPATH'] = os.pathsep.join(str(source / p) for p in
                                     ('assembler', 'intrinsics', 'backend', 'device_arch_info'))
    env['PYTHONUTF8'] = '1'
    env['PYTHONHASHSEED'] = '0'
    asm = [source / 'assembler/instructions' / name for name in
           ('insn_def_x86.json', 'insn_def_x86_64.json')]
    (generated / 'berberis/assembler').mkdir(parents=True, exist_ok=True)
    for name, definition in zip(('x86_common', 'x86_64'), asm):
        run([sys.executable, source / 'assembler/gen_asm.py', '--using',
             generated / f'berberis/assembler/gen_assembler_{name}-using-inl.h', definition])
    macros = [source / 'intrinsics' / p / 'macro_def.json' for p in
              ('all_to_x86_32_or_x86_64', 'all_to_x86_64', 'riscv64_to_x86_64')]
    regdir = generated / 'berberis/device_arch_info/x86_64'
    regdir.mkdir(parents=True, exist_ok=True)
    run([sys.executable, source / 'device_arch_info/gen_reg_class.py',
         regdir / 'machine_reg_class-inl.h', source / 'device_arch_info/x86_64/reg_class_def.json'])
    text = (source / 'device_arch_info/include/berberis/device_arch_info/x86_64/device_arch_info.h').read_text()
    (regdir / 'device_arch_info.h').write_text(text.replace('defined(__LP64__)',
        '(defined(__LP64__) || defined(_WIN64))'))
    device_outputs = [generated / 'berberis/device_arch_info' / p / 'device_insn_info-inl.h'
                      for p in ('x86_32_or_x86_64', 'x86_64', 'all_to_x86_32_or_x86_64', 'all_to_x86_64')]
    for p in device_outputs:
        p.parent.mkdir(parents=True, exist_ok=True)
    run([sys.executable, source / 'device_arch_info/gen_device_insn_info.py',
         *device_outputs, *asm, *macros[:2]])
    lir_inputs = [source / 'backend/x86_64/lir_instructions.json',
                  source / 'intrinsics/arm64_to_x86_64/machine_ir_intrinsic_binding.json',
                  *asm, *macros[:2]]
    # The current template emitter uses headers; the legacy generated .cc
    # emitter has a different interface and must not be compiled alongside it.
    names = ('insn-inl_x86_64.h', 'machine_info_x86_64-inl.h',
             'machine_opcode_x86_64-inl.h', 'gen_machine_ir_x86_64-inl.h')
    run([sys.executable, source / 'backend/gen_lir.py', '--headers',
         *(generated / name for name in names), *lir_inputs])
    outputs = [generated / 'berberis/intrinsics' / p.parent.name / 'macro_assembler_interface-inl.h'
               for p in macros]
    for p in outputs:
        p.parent.mkdir(parents=True, exist_ok=True)
    run([sys.executable, source / 'assembler/gen_asm.py', '--binary-assembler', *outputs, *macros])
    names = ('intrinsics-inl.h', 'intrinsics_process_bindings-inl.h',
             'interpreter_intrinsics_hooks-inl.h', 'translator_intrinsics_hooks-inl.h',
             'demultiplexer_intrinsics_hooks-inl.h', 'mock_semantics_listener_intrinsics_hooks-inl.h')
    run([sys.executable, source / 'intrinsics/gen_intrinsics.py', '--public_headers',
         *(generated / 'berberis/intrinsics' / n for n in names),
         source / 'intrinsics/riscv64_to_all/intrinsic_def.json',
         source / 'intrinsics/riscv64_to_x86_64/machine_ir_intrinsic_binding.json', *asm, *macros])
    (generated / 'berberis/intrinsics/intrinsics.h').write_text(
        '#pragma once\n#include <tuple>\n'
        '#include "berberis/intrinsics/common/intrinsics.h"\n'
        '#include "berberis/intrinsics/intrinsics_float.h"\n'
        '#include "berberis/intrinsics/simd_register.h"\n'
        'namespace berberis::intrinsics {\n'
        '#include "berberis/intrinsics/intrinsics-inl.h"\n}\n')
    # clang-cl implements GNU vectors, but intentionally does not define GCC's
    # identity macro. Keep this narrow adaptation out of the pinned sources.
    for name in ('simd_register.h', 'type_traits.h'):
        text = (source / 'intrinsics/include/berberis/intrinsics' / name).read_text(encoding='utf-8')
        text = re.sub(r'#(?:ifdef __GNUC__|if defined\(__GNUC__\))',
                      '#if defined(__GNUC__) || defined(__clang__)', text)
        (generated / 'berberis/intrinsics' / name).write_text(text, encoding='utf-8')
    # Function arguments are evaluated right-to-left by clang-cl. Upstream's
    # emitter increments a shared operand cursor inside a tuple_cat argument
    # pack, which then reads EFLAGS as a GPR. Braced tuple construction gives
    # the cursor an explicitly sequenced, architecture-independent order.
    ir_header = source / 'backend/include/berberis/backend/x86_64/machine_ir.h'
    text = ir_header.read_text(encoding='utf-8')
    text = patch(text, '''    std::apply(
        kEmitInsnFunc,
        std::tuple_cat(std::tuple<CodeEmitter&>{*as}, [&reg_idx, &mem_idx, this]<typename Operand> {''',
        '''    auto get_operand = [&reg_idx, &mem_idx, this]<typename Operand> {''')
    text = patch(text, '''        }.template operator()<Operands>()...));''', '''        };
    auto operands = std::tuple<decltype(get_operand.template operator()<Operands>())...>{
        get_operand.template operator()<Operands>()...};
    std::apply([&](auto&&... pieces) {
      std::apply(kEmitInsnFunc, std::tuple_cat(std::tuple<CodeEmitter&>{*as}, pieces...));
    }, operands);''')
    start = text.index('auto get_operand =')
    end = text.index('auto operands =', start)
    text = text[:start] + text[start:end].replace('<Operands>', '<Operand>') + text[end:]
    # ARM FP exception flags remain observable even if an instruction's value
    # is overwritten. Preserve the host arithmetic that updates MXCSR.
    text = '#include <string_view>\n' + text
    text = patch(text, '''    if constexpr (kSideEffects) {''', '''    constexpr std::string_view name(kMnemo);
    constexpr bool fp_status = name.ends_with("SS") || name.ends_with("SD") ||
        name.ends_with("PS") || name.ends_with("PD") || name.find("CVT") != name.npos;
    if constexpr (kSideEffects || fp_status) {''')
    output = generated / 'berberis/backend/x86_64/machine_ir.h'
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(text, encoding='utf-8')
    includes = ('backend/include', 'heavy_optimizer/arm64/include', 'intrinsics/include',
                'intrinsics/riscv64_to_x86_64/include', 'intrinsics/riscv64_to_all/include',
                'intrinsics/all_to_x86_32_or_x86_64/include', 'intrinsics/all_to_x86_64/include',
                'device_arch_info/include')
    flags = [flags[0], '/I' + str(generated), *flags[1:]] + ['/I' + str(source / p) for p in includes]
    flags = ['/clang:-std=c++23' if flag == '/std:c++20' else flag for flag in flags]
    flags += ['/clang:-Wno-deprecated-anon-enum-enum-conversion', '/clang:-ferror-limit=5']
    flags += ['/D_SILENCE_CXX23_DENORM_DEPRECATION_WARNING']
    bp = (source / 'backend/Android.bp').read_text()
    backend = re.search(r'name: "berberis_backend_srcs",\s*srcs: \[(.*?)\]', bp, re.S).group(1)
    files = [source / 'backend' / p for p in re.findall(r'"([^"]+\.cc)"', backend)]
    files += [source / 'heavy_optimizer/arm64' / p for p in ('frontend.cc', 'heavy_optimize_region.cc')]
    files.append(source / 'intrinsics/riscv64_to_x86_64/constants_pool.cc')
    flags += ['/I' + str(source / 'heavy_optimizer/arm64')]
    # Content-based stamps avoid recompiling the large frontend when only the
    # adapter changes, while still noticing every upstream/compatibility header.
    headers = hashlib.sha256(repr(flags).encode())
    for root in (source, generated, Path(__file__).parent / 'compat'):
        for header in sorted(root.rglob('*.h')):
            headers.update(str(header).encode())
            headers.update(header.read_bytes())
    header_digest = headers.digest()
    objects = []
    for file in files:
        relative = file.relative_to(source) if file.is_relative_to(source) else file.relative_to(generated)
        obj = work / ('heavy_' + '_'.join(relative.with_suffix('').parts) + '.obj')
        if file.name == 'code_emit.cc':
            text = patch(file.read_text(encoding='utf-8'),
                'as->Pmov(GetXReg(dst), GetXReg(src));', 'as->Movdqa(GetXReg(dst), GetXReg(src));')
            file = generated / file.name
            file.write_text(text, encoding='utf-8')
        if file.name == 'code_gen.cc':
            text = file.read_text(encoding='utf-8')
            text = '#include <cstdlib>\n#include <cstdio>\n' + text
            text = patch(text, 'machine_ir->Emit(&emitter);',
                'static const bool dump_ir = std::getenv("AXRB_DIGITALIS_DUMP_IR") != nullptr;\n'
                '    if (dump_ir) '
                'std::fprintf(stderr, "%s\\n", machine_ir->GetDebugString().c_str());\n'
                '    machine_ir->Emit(&emitter);')
            file = generated / file.name
            file.write_text(text, encoding='utf-8')
        if file.name == 'frontend.cc':
            text = patch(file.read_text(encoding='utf-8'),
                'GuestAddr target = jump->target();',
                'static const bool no_loops = std::getenv("AXRB_DIGITALIS_NO_LOOPS") != nullptr;\n'
                '    if (no_loops && '
                'jump->kind() == PseudoJump::Kind::kJumpWithPendingSignalsCheck) continue;\n'
                '    GuestAddr target = jump->target();')
            text = patch(text, '''const size_t offset = offsetof(ThreadState, pending_signals_status);
    auto* cmpb = ir->NewInsn<x86_64::CmpbOpImm>({.base = x86_64::kMachineRegRBP, .disp = offset},
                                                kPendingSignalsPresent,
                                                GetFlagsRegister());''', '''const size_t offset = sizeof(ThreadState);
    auto* cmpb = ir->NewInsn<x86_64::SublOpImm>({.base = x86_64::kMachineRegRBP, .disp = offset},
                                               1, GetFlagsRegister());''')
            file = generated / file.name
            file.write_text(text, encoding='utf-8')
        if file.name == 'constants_pool.cc':
            text = file.read_text(encoding='utf-8')
            text = text.replace('#include <sys/mman.h>', '#include <windows.h>\n#include <stdexcept>')
            text = text.replace('#include "berberis/base/mmap.h"', '')
            start = text.index('int32_t GetConstants() {')
            end = text.index('\n}\n', start) + 2
            text = text[:start] + '''int32_t GetConstants() {
  static const int32_t address = [] {
    // Upstream emits absolute signed-32-bit references to this immutable pool.
    // Reserve a free Windows allocation granule below 2 GiB, without replacing
    // any existing host or guest mapping.
    for (uintptr_t candidate = 0x10000000; candidate < 0x70000000; candidate += 65536) {
      void* memory = VirtualAlloc(reinterpret_cast<void*>(candidate), sizeof(MacroAssemblerConstants),
                                  MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
      if (!memory) continue;
      new (memory) MacroAssemblerConstants(kBerberisMacroAssemblerConstants);
      DWORD old;
      if (!VirtualProtect(memory, sizeof(MacroAssemblerConstants), PAGE_READONLY, &old))
        throw std::runtime_error("Digitalis constant-pool protection failed");
      return static_cast<int32_t>(candidate);
    }
    throw std::runtime_error("Digitalis could not allocate its low-address constant pool");
  }();
  return address;
}''' + text[end:]
            file = generated / file.name
            file.write_text(text, encoding='utf-8')
        digest = hashlib.sha256(header_digest + file.read_bytes()).hexdigest()
        stamp = obj.with_suffix('.sha256')
        if not obj.exists() or not stamp.exists() or stamp.read_text() != digest:
            run([*flags, file, '/Fo' + str(obj)])
            stamp.write_text(digest)
        objects.append(obj)
    return objects
