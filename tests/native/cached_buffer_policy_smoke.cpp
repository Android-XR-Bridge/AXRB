#include "cached_buffer_policy.h"
#include <assert.h>
#include <stdio.h>
int main(){
 VkPhysicalDeviceMemoryProperties p{};p.memoryTypeCount=5;
 const unsigned flags[]={0,1,6,14,7};for(unsigned i=0;i<5;++i)p.memoryTypes[i].propertyFlags=flags[i];
 auto policy=axrb::CachedBufferPolicy::from(p);assert(policy.cached==8&&policy.uncached==20);
 assert(policy.filter(31)==11);assert(policy.filter(7)==7);assert(policy.filter(16)==16);
 assert(policy.filter(8)==8);assert(policy.filter(0)==0);
 for(unsigned bits=0;bits<32;++bits){auto out=policy.filter(bits);assert(!(out&~bits));assert(!bits||out);assert(!(bits&8)||!(out&20));}
 p.memoryTypes[3].propertyFlags=10;policy=axrb::CachedBufferPolicy::from(p);assert(policy.cached==0);
 for(unsigned bits=0;bits<32;++bits)assert(policy.filter(bits)==bits);
 puts("PASS cached-buffer policy: genuine cached/coherent alternatives only, supported-type subsets, never empty, no-alternative fallback.");
}
