#pragma once
#include <stdint.h>
#include "arm_bridge_abi.h"
// Windows ABI boundary. Android SysV callers must use the kernel transport.
struct AxrbArmServiceConfig {
    uint32_t size,version;
    const wchar_t* sysroot;
    const wchar_t* helper;
    const wchar_t* libraries;
};
#ifdef AXRB_ARM_SERVICE_BUILD
#define AXRB_SERVICE extern "C" __declspec(dllexport)
#else
#define AXRB_SERVICE extern "C" __declspec(dllimport)
#endif
AXRB_SERVICE int axrb_arm_service_start(const AxrbArmServiceConfig*);
AXRB_SERVICE uint64_t axrb_arm_service_load(const char*);
AXRB_SERVICE uint64_t axrb_arm_service_symbol(uint64_t,const char*);
AXRB_SERVICE const char* axrb_arm_service_error();
AXRB_SERVICE int axrb_arm_service_step(AxrbArmRegisters*,uint64_t);
AXRB_SERVICE int axrb_arm_service_is_function(uint64_t);
// Binder only: operation 0=open, 1=ioctl, 2=map, 3=close.
AXRB_SERVICE int64_t axrb_arm_service_binder(uint32_t,int64_t,uint64_t,uint64_t,uint64_t,uint64_t);
AXRB_SERVICE int64_t axrb_arm_service_host(const char*,const uint64_t*);
