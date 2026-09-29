#pragma once
#include <vulkan/vulkan.h>
namespace axrb {
struct CachedBufferPolicy {
 uint32_t cached=0,uncached=0;
 static CachedBufferPolicy from(const VkPhysicalDeviceMemoryProperties& p){
  CachedBufferPolicy result;
  for(uint32_t i=0;i<p.memoryTypeCount;++i){
   auto flags=p.memoryTypes[i].propertyFlags;
   if(!(flags&VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT))continue;
   if((flags&(VK_MEMORY_PROPERTY_HOST_CACHED_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))==
      (VK_MEMORY_PROPERTY_HOST_CACHED_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))result.cached|=1u<<i;
   else if(!(flags&VK_MEMORY_PROPERTY_HOST_CACHED_BIT))result.uncached|=1u<<i;
  }
  return result;
 }
 uint32_t filter(uint32_t supported)const{
  // Only remove slow CPU-visible alternatives when this EXACT buffer can use
  // a genuine driver-reported cached+coherent type. Never invent support,
  // change memory indices/flags, or modify image requirements/allocations.
  return supported&cached?supported&~uncached:supported;
 }
};
}
