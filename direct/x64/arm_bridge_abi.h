#pragma once
#include <stdint.h>
/* Private, versioned developer transport. This is not a Linux syscall ABI. */
#define AXRB_ARM_SYSCALL 0x41585242
#define AXRB_ARM_ABI 1
enum { AXRB_ARM_LOAD=1, AXRB_ARM_SYMBOL, AXRB_ARM_BEGIN, AXRB_ARM_STEP, AXRB_ARM_END, AXRB_ARM_GATES, AXRB_ARM_IS_FUNCTION, AXRB_ARM_LOAD_PATH, AXRB_ARM_INITIALIZER };
struct AxrbArmRegisters {
    uint64_t x[31],sp,pc,tls;
    uint32_t pstate,fpcr,fpsr,pad;
    uint64_t v[32][2];
};
