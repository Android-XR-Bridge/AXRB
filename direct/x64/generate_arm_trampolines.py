"""Static x86 SysV JNI trampolines; no writable executable/JIT pages required."""
from pathlib import Path
import sys
lines=['.text', '.hidden axrb_arm_dispatch', '.type axrb_arm_entry,@function', 'axrb_arm_entry:', 'sub $216,%rsp']
for i,reg in enumerate(('rdi','rsi','rdx','rcx','r8','r9')):
    lines.append(f'mov %{reg},{i*8}(%rsp)')
for i in range(8):
    lines.append(f'movdqu %xmm{i},{48+i*16}(%rsp)')
lines += ['lea 224(%rsp),%rax','mov %rax,176(%rsp)','mov %r11d,%edi','mov %rsp,%rsi',
          'call axrb_arm_dispatch','mov 184(%rsp),%rax','movq 192(%rsp),%xmm0','add $216,%rsp','ret',
          '.size axrb_arm_entry,.-axrb_arm_entry']
for i in range(64):
    lines += [f'.type axrb_arm_stub_{i},@function',f'axrb_arm_stub_{i}:',f'mov ${i},%r11d',
              'jmp axrb_arm_entry',f'.size axrb_arm_stub_{i},.-axrb_arm_stub_{i}']
lines += ['.section .data.rel.ro,"aw",@progbits','.globl axrb_arm_trampolines','.hidden axrb_arm_trampolines',
          '.type axrb_arm_trampolines,@object','axrb_arm_trampolines:']
lines += [f'.quad axrb_arm_stub_{i}' for i in range(64)]
lines += ['.size axrb_arm_trampolines,.-axrb_arm_trampolines','.section .note.GNU-stack,"",@progbits']
Path(sys.argv[1]).write_text('\n'.join(lines)+'\n')
