#pragma once
#include <cstdint>
class ReadOnlyFiles;
namespace arm_bridge {
void prepare();
int64_t binder(uint32_t operation,int64_t fd,uint64_t a=0,uint64_t b=0,uint64_t c=0,uint64_t d=0);
int64_t platform_host(const char*,const uint64_t*);
using Access=bool (*)(uint64_t,uint64_t,bool);
int64_t dispatch(uint64_t operation,uint64_t a,uint64_t b,uint64_t version,Access access,ReadOnlyFiles* files=nullptr,void(*unlock)()=nullptr,void(*relock)()=nullptr);
}
