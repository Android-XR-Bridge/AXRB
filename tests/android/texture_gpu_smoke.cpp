// Real Vulkan regression for AXRB's staging, submit-time snapshot and GPU lifetime.
#include "../../runtime/vulkan/texture/astc_substitution.h"
#include <cstdio>
#include <cstdlib>
#define CHECK(x) do { if(!(x)){std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);return 1;} } while(0)
#define OK(x) CHECK((x)==VK_SUCCESS)
static VkResult VKAPI_CALL loader(VkDevice,void*) { return VK_SUCCESS; }
int main() {
    using namespace axrb::texture;
    VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO}; VkInstance instance;
    OK(vkCreateInstance(&ci,nullptr,&instance));
    uint32_t n=1; VkPhysicalDevice physical; OK(vkEnumeratePhysicalDevices(instance,&n,&physical));
    vkGetPhysicalDeviceQueueFamilyProperties(physical,&n,nullptr);
    std::vector<VkQueueFamilyProperties> families(n);vkGetPhysicalDeviceQueueFamilyProperties(physical,&n,families.data());
    uint32_t family=0;while(family<n && !(families[family].queueFlags&VK_QUEUE_COMPUTE_BIT))++family;CHECK(family<n);
    float priority=1;VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qi.queueFamilyIndex=family;qi.queueCount=1;qi.pQueuePriorities=&priority;
    VkPhysicalDeviceFeatures features{};features.textureCompressionBC=VK_TRUE;
    VkDeviceCreateInfo di{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};di.queueCreateInfoCount=1;di.pQueueCreateInfos=&qi;di.pEnabledFeatures=&features;
    VkDevice device;OK(vkCreateDevice(physical,&di,nullptr,&device));VkQueue queue;vkGetDeviceQueue(device,family,0,&queue);
    DeviceCalls calls;calls.device=device;calls.gdpa=vkGetDeviceProcAddr;calls.loader=loader;calls.computeFamilies=1u<<family;
    vkGetPhysicalDeviceMemoryProperties(physical,&calls.memory);
    VkPhysicalDeviceProperties props;vkGetPhysicalDeviceProperties(physical,&props);calls.maxStorageRange=props.limits.maxStorageBufferRange;
#define LOAD(field,name) calls.field=name
    LOAD(createImage,vkCreateImage);LOAD(destroyImage,vkDestroyImage);LOAD(createImageView,vkCreateImageView);
    LOAD(createBuffer,vkCreateBuffer);LOAD(destroyBuffer,vkDestroyBuffer);LOAD(bufferRequirements,vkGetBufferMemoryRequirements);
    LOAD(bindBufferMemory,vkBindBufferMemory);LOAD(allocateMemory,vkAllocateMemory);LOAD(freeMemory,vkFreeMemory);
    LOAD(mapMemory,vkMapMemory);LOAD(unmapMemory,vkUnmapMemory);LOAD(copyBufferToImage,vkCmdCopyBufferToImage);
#undef LOAD
    auto& layer=astc_images();layer.add_device(calls);
    auto allocate=[&](VkMemoryRequirements req,VkMemoryPropertyFlags flags){
        VkMemoryAllocateInfo info{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};info.allocationSize=req.size;
        for(uint32_t t=0;t<calls.memory.memoryTypeCount;++t)if((req.memoryTypeBits&(1u<<t)) && (calls.memory.memoryTypes[t].propertyFlags&flags)==flags){info.memoryTypeIndex=t;break;}
        VkDeviceMemory memory{};if(vkAllocateMemory(device,&info,nullptr,&memory)!=VK_SUCCESS)std::abort();
        layer.allocated_memory(device,&info,memory);return memory;
    };
    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};bi.size=256;bi.usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    VkBuffer buffer;OK(vkCreateBuffer(device,&bi,nullptr,&buffer));layer.created_buffer(buffer,bi.size);
    VkMemoryRequirements req;vkGetBufferMemoryRequirements(device,buffer,&req);
    auto memory=allocate(req,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    OK(vkBindBufferMemory(device,buffer,memory,0));layer.bound_buffer(buffer,memory,0);
    void* mapped;OK(vkMapMemory(device,memory,0,VK_WHOLE_SIZE,0,&mapped));layer.mapped(memory,0,mapped);
    VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};ii.imageType=VK_IMAGE_TYPE_2D;ii.format=VK_FORMAT_ASTC_4x4_UNORM_BLOCK;
    ii.extent={8,8,1};ii.mipLevels=1;ii.arrayLayers=1;ii.samples=VK_SAMPLE_COUNT_1_BIT;ii.tiling=VK_IMAGE_TILING_OPTIMAL;
    ii.usage=VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_SAMPLED_BIT;
    VkImage image;OK(layer.create_image(device,&ii,nullptr,&image));vkGetImageMemoryRequirements(device,image,&req);
    auto imageMemory=allocate(req,0);OK(vkBindImageMemory(device,image,imageMemory,0));
    VkCommandPoolCreateInfo pi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};pi.queueFamilyIndex=family;pi.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    VkCommandPool pool;OK(vkCreateCommandPool(device,&pi,nullptr,&pool));layer.created_pool(pool,family);
    VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};ai.commandPool=pool;ai.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;ai.commandBufferCount=1;
    VkCommandBuffer cmd;OK(vkAllocateCommandBuffers(device,&ai,&cmd));layer.allocated_command_buffers(device,pool,1,&cmd);
    for(int iteration=0;iteration<3;++iteration){
        layer.reset_command_buffer(cmd);
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        OK(vkBeginCommandBuffer(cmd,&begin));layer.began(cmd,begin.flags);
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};barrier.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;barrier.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;barrier.image=image;barrier.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
        vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&barrier);
        // LDR void-extent ASTC blocks, initially opaque red.
        const uint8_t red[16]={0xfc,0xfd,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0,0,0,0,0xff,0xff};
        for(int b=0;b<4;++b)std::memcpy(static_cast<uint8_t*>(mapped)+b*16,red,16);
        VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={8,8,1};
        CHECK(layer.copy_buffer_to_image(cmd,device,buffer,image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy));
        // A valid post-recording write must be picked up at submit (green).
        for(int b=0;b<4;++b){auto* block=static_cast<uint8_t*>(mapped)+b*16;block[8]=block[9]=0;block[10]=block[11]=255;}
        barrier.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;barrier.newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&barrier);
        copy.bufferOffset=128;vkCmdCopyImageToBuffer(cmd,image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,buffer,1,&copy);
        OK(vkEndCommandBuffer(cmd));layer.before_submit(1,&cmd);
        std::vector<VkCommandBuffer> commands;layer.gpu_commands(cmd,commands);CHECK(commands.size()==1);commands.push_back(cmd);
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=uint32_t(commands.size());submit.pCommandBuffers=commands.data();
        OK(vkQueueSubmit(queue,1,&submit,VK_NULL_HANDLE));OK(vkQueueWaitIdle(queue));
        const auto* output=static_cast<const uint8_t*>(mapped)+128;
        // BC3's two RGB565 endpoints must both describe solid green.
        for(int b=0;b<4;++b)CHECK(output[b*16+8]==0xe0 && output[b*16+9]==7);
    }
    layer.reset_pool(pool,true);vkDestroyCommandPool(device,pool,nullptr);
    layer.destroy_image(device,image,nullptr);layer.free_memory(imageMemory);vkFreeMemory(device,imageMemory,nullptr);
    layer.unmapped(memory);vkUnmapMemory(device,memory);layer.destroyed_buffer(buffer);vkDestroyBuffer(device,buffer,nullptr);
    layer.free_memory(memory);vkFreeMemory(device,memory,nullptr);layer.remove_device(device);
    vkDestroyDevice(device,nullptr);vkDestroyInstance(instance,nullptr);
    std::puts("PASS: GPU ASTC upload, post-recording writes, readback and repeated staging retirement");return 0;
}
