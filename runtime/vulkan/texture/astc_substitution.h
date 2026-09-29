#pragma once
// Stores the game's ASTC textures as BC7.
//
// Desktop GPUs cannot sample ASTC, so the emulator's host renderer keeps every
// ASTC image as RGBA8 (32 bits per texel) plus the compressed original: a
// 2048 x 2048 ASTC 6x6 texture with mipmaps takes 24 MiB of VRAM instead of
// 2.5 MiB. BC7 is sampled natively at 8 bits per texel (5.3 MiB).
//
// An ASTC image the game creates is created as the BC7 format of the same
// color space instead, and so are its views. A buffer-to-image copy into it
// is recorded from a staging buffer of this layer; the ASTC bytes are copied
// out of the game's buffer when the copy is recorded and transcoded by the
// background workers (transcode_workers.h). Submitting the command buffer
// waits for its transcodes, after checking that the game's buffer still holds
// the bytes that were transcoded (and transcoding again if not). Staging
// memory returns to a pool when the command buffer is reset or freed.
//
// Images that could be viewed or copied as another format, images with
// extension structures this code does not know, and uploads whose source is
// not host-readable or whose region does not fall on 4-texel BC7 blocks are
// logged; the image stays BC7, which the game then samples without that
// content. debug.axrb.astc_to_bc7=0 turns the substitution off.
#include <vulkan/vulkan.h>
#include <android/log.h>
#include <sys/system_properties.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>
#include "astc_to_bc7.h"
#include "transcode_workers.h"

namespace axrb::texture {

inline bool astc_to_bc7_enabled() {
    static const bool enabled = [] {
        char value[PROP_VALUE_MAX]{};
        __system_property_get("debug.axrb.astc_to_bc7", value);
        return std::strcmp(value, "0") != 0;
    }();
    return enabled;
}

inline VkFormat bc7_format(bool srgb) { return srgb ? VK_FORMAT_BC7_SRGB_BLOCK : VK_FORMAT_BC7_UNORM_BLOCK; }

// Next-layer entry points of one device.
struct DeviceCalls {
    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties memory{};
    PFN_vkCreateImage createImage = nullptr;
    PFN_vkDestroyImage destroyImage = nullptr;
    PFN_vkCreateImageView createImageView = nullptr;
    PFN_vkCreateBuffer createBuffer = nullptr;
    PFN_vkDestroyBuffer destroyBuffer = nullptr;
    PFN_vkGetBufferMemoryRequirements bufferRequirements = nullptr;
    PFN_vkBindBufferMemory bindBufferMemory = nullptr;
    PFN_vkBindBufferMemory2 bindBufferMemory2 = nullptr;
    PFN_vkAllocateMemory allocateMemory = nullptr;
    PFN_vkFreeMemory freeMemory = nullptr;
    PFN_vkMapMemory mapMemory = nullptr;
    PFN_vkUnmapMemory unmapMemory = nullptr;
    PFN_vkAllocateCommandBuffers allocateCommandBuffers = nullptr;
    PFN_vkFreeCommandBuffers freeCommandBuffers = nullptr;
    PFN_vkResetCommandPool resetCommandPool = nullptr;
    PFN_vkDestroyCommandPool destroyCommandPool = nullptr;
    PFN_vkBeginCommandBuffer beginCommandBuffer = nullptr;
    PFN_vkResetCommandBuffer resetCommandBuffer = nullptr;
    PFN_vkCmdCopyBufferToImage copyBufferToImage = nullptr;
    PFN_vkCmdCopyBufferToImage2 copyBufferToImage2 = nullptr;
    PFN_vkCmdCopyImageToBuffer copyImageToBuffer = nullptr;
    PFN_vkCmdExecuteCommands executeCommands = nullptr;
    PFN_vkQueueSubmit queueSubmit = nullptr;
    PFN_vkQueueSubmit2 queueSubmit2 = nullptr;
};

class AstcSubstitution {
public:
    static AstcSubstitution& instance() {
        static AstcSubstitution substitution;
        return substitution;
    }

    // Called when a device was created with textureCompressionBC enabled.
    void add_device(const DeviceCalls& calls) {
        std::lock_guard lock(mutex_);
        devices_[calls.device] = std::make_shared<DeviceState>(calls);
        bc7_settings(false);
        __android_log_print(ANDROID_LOG_INFO, tag, "ASTC textures stored as BC7; %d transcoding worker(s) off the main thread",
                            Workers::instance().count());
    }
    void remove_device(VkDevice device) {
        std::shared_ptr<DeviceState> state;
        std::vector<std::shared_ptr<CommandState>> pending;
        {
            std::lock_guard lock(mutex_);
            auto it = devices_.find(device);
            if (it == devices_.end()) return;
            state = it->second;
            devices_.erase(it);
            for (auto c = commands_.begin(); c != commands_.end();)
                if (c->second->device == state.get()) { pending.push_back(c->second); c = commands_.erase(c); } else ++c;
            for (auto i = images_.begin(); i != images_.end();) i = i->second.device == device ? images_.erase(i) : std::next(i);
        }
        for (auto& command : pending) release(*state, *command);
        state->destroy_pool();
    }
    bool active(VkDevice device) {
        std::lock_guard lock(mutex_);
        return devices_.count(device) != 0;
    }

    // --- Images -----------------------------------------------------------

    VkResult create_image(VkDevice device, const VkImageCreateInfo* info, const VkAllocationCallbacks* allocator, VkImage* out) {
        auto state = device_state(device);
        int bw = 0, bh = 0;
        bool srgb = false;
        if (!state || !info || !astc::block_size(uint32_t(info->format), &bw, &bh, &srgb) || !substitutable(*info))
            return state ? state->calls.createImage(device, info, allocator, out) : VK_ERROR_INITIALIZATION_FAILED;
        VkImageCreateInfo bc7 = *info;
        bc7.format = bc7_format(srgb);
        const VkResult result = state->calls.createImage(device, &bc7, allocator, out);
        if (result != VK_SUCCESS) return result;
        std::lock_guard lock(mutex_);
        images_[*out] = {device, info->format, bw, bh, srgb, info->extent, info->mipLevels};
        ++stats_.images;
        stats_.emulatedBytes += emulated_bytes(*info, bw, bh);
        stats_.bc7Bytes += bc7_bytes(*info);
        return result;
    }
    void destroy_image(VkDevice device, VkImage image, const VkAllocationCallbacks* allocator) {
        auto state = device_state(device);
        {
            std::lock_guard lock(mutex_);
            images_.erase(image);
        }
        if (state) state->calls.destroyImage(device, image, allocator);
    }
    VkResult create_image_view(VkDevice device, const VkImageViewCreateInfo* info, const VkAllocationCallbacks* allocator,
                               VkImageView* out) {
        auto state = device_state(device);
        if (!state) return VK_ERROR_INITIALIZATION_FAILED;
        VkImageViewCreateInfo view = *info;
        {
            std::lock_guard lock(mutex_);
            auto it = images_.find(info->image);
            int bw, bh;
            bool srgb;
            if (it != images_.end() && astc::block_size(uint32_t(info->format), &bw, &bh, &srgb)) view.format = bc7_format(srgb);
        }
        return state->calls.createImageView(device, &view, allocator, out);
    }

    // --- Buffers and memory: where the game's upload bytes are readable ---

    void allocated_memory(VkDevice device, const VkMemoryAllocateInfo* info, VkDeviceMemory memory) {
        std::lock_guard lock(mutex_);
        memories_[memory] = {device, info->memoryTypeIndex, info->allocationSize, nullptr, 0};
    }
    void free_memory(VkDeviceMemory memory) {
        std::lock_guard lock(mutex_);
        memories_.erase(memory);
    }
    void mapped(VkDeviceMemory memory, VkDeviceSize offset, void* pointer) {
        std::lock_guard lock(mutex_);
        auto it = memories_.find(memory);
        if (it != memories_.end()) { it->second.mapped = static_cast<uint8_t*>(pointer); it->second.mappedOffset = offset; }
    }
    void unmapped(VkDeviceMemory memory) {
        std::lock_guard lock(mutex_);
        auto it = memories_.find(memory);
        if (it != memories_.end()) it->second.mapped = nullptr;
    }
    void created_buffer(VkBuffer buffer, VkDeviceSize size) {
        std::lock_guard lock(mutex_);
        buffers_[buffer] = {VK_NULL_HANDLE, 0, size};
    }
    void destroyed_buffer(VkBuffer buffer) {
        std::lock_guard lock(mutex_);
        buffers_.erase(buffer);
    }
    void bound_buffer(VkBuffer buffer, VkDeviceMemory memory, VkDeviceSize offset) {
        std::lock_guard lock(mutex_);
        auto it = buffers_.find(buffer);
        if (it != buffers_.end()) { it->second.memory = memory; it->second.offset = offset; }
    }

    // --- Command buffers --------------------------------------------------

    void allocated_command_buffers(VkDevice device, VkCommandPool pool, uint32_t count, const VkCommandBuffer* buffers) {
        std::lock_guard lock(mutex_);
        for (uint32_t i = 0; i < count; ++i) pools_[buffers[i]] = pool;
        (void)device;
    }
    // Reset, begin (an implicit reset), or free: the recorded uploads are done with.
    void reset_command_buffer(VkCommandBuffer command) {
        std::shared_ptr<CommandState> state;
        std::shared_ptr<DeviceState> device;
        {
            std::lock_guard lock(mutex_);
            auto it = commands_.find(command);
            if (it == commands_.end()) return;
            state = it->second;
            commands_.erase(it);
            device = device_of(state->device);
        }
        if (device) release(*device, *state);
    }
    void freed_command_buffers(uint32_t count, const VkCommandBuffer* buffers) {
        for (uint32_t i = 0; i < count; ++i) {
            reset_command_buffer(buffers[i]);
            std::lock_guard lock(mutex_);
            pools_.erase(buffers[i]);
        }
    }
    void reset_pool(VkCommandPool pool, bool destroyed) {
        std::vector<VkCommandBuffer> members;
        {
            std::lock_guard lock(mutex_);
            for (auto& [command, owner] : pools_)
                if (owner == pool) members.push_back(command);
        }
        for (VkCommandBuffer command : members) reset_command_buffer(command);
        if (destroyed) {
            std::lock_guard lock(mutex_);
            for (VkCommandBuffer command : members) pools_.erase(command);
        }
    }
    void executed(VkCommandBuffer primary, uint32_t count, const VkCommandBuffer* secondaries) {
        std::lock_guard lock(mutex_);
        bool any = false;
        for (uint32_t i = 0; i < count && !any; ++i) any = commands_.count(secondaries[i]) != 0;
        if (!any) return;
        auto& state = command_state(primary, nullptr);
        state.secondaries.insert(state.secondaries.end(), secondaries, secondaries + count);
    }

    // Returns false if the copy is not into a substituted image and was not recorded.
    bool copy_buffer_to_image(VkCommandBuffer command, VkDevice device, VkBuffer source, VkImage target, VkImageLayout layout,
                              uint32_t count, const VkBufferImageCopy* regions) {
        Image image;
        Buffer buffer;
        Memory memory;
        std::shared_ptr<DeviceState> state;
        {
            std::lock_guard lock(mutex_);
            auto i = images_.find(target);
            if (i == images_.end()) return false;
            image = i->second;
            state = device_of(device);
            auto b = buffers_.find(source);
            if (b != buffers_.end()) buffer = b->second;
            auto m = memories_.find(buffer.memory);
            if (m != memories_.end()) memory = m->second;
        }
        if (!state) return false;
        auto upload = std::make_shared<Upload>();
        upload->image = image;
        std::vector<VkBufferImageCopy> staged;
        std::vector<Region> planned;
        VkDeviceSize stagingSize = 0;
        for (uint32_t r = 0; r < count; ++r) {
            Region region;
            if (!plan(image, regions[r], &region)) continue;
            region.bc7Offset = stagingSize;
            stagingSize += region.bc7Bytes;
            planned.push_back(region);
        }
        // The game's bytes: mapped by the game, or mapped here for the copy.
        const uint8_t* bytes = nullptr;
        bool mappedHere = false;
        VkDeviceSize first = ~VkDeviceSize(0), last = 0;
        for (auto& region : planned) {
            first = std::min(first, region.copy.bufferOffset);
            last = std::max(last, region.copy.bufferOffset + region.astcBytes);
        }
        if (!planned.empty()) {
            const VkDeviceSize begin = buffer.offset + first, end = buffer.offset + last;
            if (memory.mapped && begin >= memory.mappedOffset) {
                bytes = memory.mapped + (begin - memory.mappedOffset);
            } else if (!memory.mapped && buffer.memory && host_visible(*state, memory.type)) {
                void* pointer = nullptr;
                if (state->calls.mapMemory(device, buffer.memory, begin, end - begin, 0, &pointer) == VK_SUCCESS) {
                    bytes = static_cast<const uint8_t*>(pointer);
                    mappedHere = true;
                }
            }
            if (!bytes || end > memory.size) {
                if (mappedHere) state->calls.unmapMemory(device, buffer.memory);
                warn("upload source is not host-readable; texture left without that content");
                planned.clear();
                bytes = nullptr;
            }
        }
        if (planned.empty()) return true; // nothing that can be transcoded; the copy is dropped
        if (!mappedHere) {
            upload->liveMemory = buffer.memory;
            upload->liveMapping = memory.mapped;
            upload->live = bytes;
        }
        upload->source = std::make_shared<std::vector<uint8_t>>(bytes, bytes + (last - first));
        if (mappedHere) state->calls.unmapMemory(device, buffer.memory);
        for (auto& region : planned) region.sourceOffset = region.copy.bufferOffset - first;
        upload->regions = planned;
        dump(*upload);

        std::lock_guard lock(mutex_);
        auto& command_ = command_state(command, state.get());
        Staging staging = state->take(stagingSize, command_);
        if (!staging.buffer) {
            warn("staging memory unavailable; texture left without that content");
            return true;
        }
        upload->target = staging.mapped;
        for (auto& region : upload->regions) {
            VkBufferImageCopy copy = region.copy;
            copy.bufferOffset = staging.offset + region.bc7Offset;
            copy.bufferRowLength = 0;
            copy.bufferImageHeight = 0;
            staged.push_back(copy);
        }
        start(*upload);
        command_.uploads.push_back(upload);
        stats_.uploads += 1;
        stats_.uploadTexels += upload->texels;
        state->calls.copyBufferToImage(command, staging.buffer, target, layout, uint32_t(staged.size()), staged.data());
        return true;
    }

    // Waits for the transcodes of the command buffers about to be submitted.
    void before_submit(uint32_t count, const VkCommandBuffer* buffers) {
        std::vector<std::shared_ptr<Upload>> uploads;
        {
            std::lock_guard lock(mutex_);
            if (commands_.empty()) return;
            for (uint32_t i = 0; i < count; ++i) collect(buffers[i], uploads);
        }
        if (uploads.empty()) return;
        const auto begin = std::chrono::steady_clock::now();
        for (auto& upload : uploads) {
            upload->batch->wait();
            // The game may write its upload buffer after recording the copy;
            // it can only be read while the memory is still mapped where it was.
            bool readable = false;
            if (upload->live) {
                std::lock_guard lock(mutex_);
                auto m = memories_.find(upload->liveMemory);
                readable = m != memories_.end() && m->second.mapped == upload->liveMapping;
            }
            if (readable && std::memcmp(upload->live, upload->source->data(), upload->source->size())) {
                std::memcpy(upload->source->data(), upload->live, upload->source->size());
                start(*upload);
                upload->batch->wait();
                std::lock_guard lock(mutex_);
                ++stats_.retranscodes;
            }
        }
        std::lock_guard lock(mutex_);
        stats_.submitWaitSeconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
        report();
    }

    void warn(const char* what) {
        std::lock_guard lock(warnings_);
        if (warned_ < 16) {
            ++warned_;
            __android_log_print(ANDROID_LOG_WARN, tag, "%s", what);
        }
    }

private:
    static constexpr const char* tag = "AXRB.Textures";

    struct Image {
        VkDevice device = VK_NULL_HANDLE;
        VkFormat astc = VK_FORMAT_UNDEFINED;
        int bw = 0, bh = 0;
        bool srgb = false;
        VkExtent3D extent{};
        uint32_t mips = 1;
    };
    struct Buffer {
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkDeviceSize offset = 0, size = 0;
    };
    struct Memory {
        VkDevice device = VK_NULL_HANDLE;
        uint32_t type = 0;
        VkDeviceSize size = 0;
        uint8_t* mapped = nullptr;
        VkDeviceSize mappedOffset = 0;
    };
    // One buffer-to-image region: where its ASTC bytes and BC7 blocks are.
    struct Region {
        VkBufferImageCopy copy{};
        int width = 0, height = 0, slices = 1;      // texels; slices = layers x depth
        size_t astcRowBytes = 0, astcSliceBytes = 0;
        VkDeviceSize astcBytes = 0, sourceOffset = 0;
        size_t bc7RowBytes = 0, bc7SliceBytes = 0;
        VkDeviceSize bc7Bytes = 0, bc7Offset = 0;
    };
    struct Upload {
        Image image;
        std::vector<Region> regions;
        std::shared_ptr<std::vector<uint8_t>> source;  // the game's bytes as recorded
        const uint8_t* live = nullptr;                  // the game's mapping, to re-check at submit
        VkDeviceMemory liveMemory = VK_NULL_HANDLE;     // valid while still mapped at liveMapping
        const uint8_t* liveMapping = nullptr;
        uint8_t* target = nullptr;                      // staging memory at the upload's offset
        std::shared_ptr<Batch> batch;
        uint64_t texels = 0;
    };
    struct Chunk {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        uint8_t* mapped = nullptr;
        VkDeviceSize size = 0, used = 0;
    };
    struct Staging {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceSize offset = 0;
        uint8_t* mapped = nullptr;
    };
    struct CommandState;
    struct DeviceState {
        explicit DeviceState(const DeviceCalls& c) : calls(c) {}
        DeviceCalls calls;
        std::vector<Chunk> free;
        std::mutex chunks;

        // Staging space for one upload, from the command buffer's current
        // chunk or a new one. Callers hold the substitution lock.
        Staging take(VkDeviceSize size, CommandState& command) {
            size = (size + 15) & ~VkDeviceSize(15);
            if (command.chunks.empty() || command.chunks.back().size - command.chunks.back().used < size) {
                Chunk chunk = obtain(size);
                if (!chunk.buffer) return {};
                command.chunks.push_back(chunk);
            }
            Chunk& chunk = command.chunks.back();
            Staging staging{chunk.buffer, chunk.used, chunk.mapped + chunk.used};
            chunk.used += size;
            return staging;
        }
        Chunk obtain(VkDeviceSize size) {
            {
                std::lock_guard lock(chunks);
                for (size_t i = 0; i < free.size(); ++i)
                    if (free[i].size >= size) {
                        Chunk chunk = free[i];
                        free.erase(free.begin() + ptrdiff_t(i));
                        chunk.used = 0;
                        return chunk;
                    }
            }
            Chunk chunk;
            chunk.size = std::max<VkDeviceSize>(size, 16u << 20);
            VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
            info.size = chunk.size;
            info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
            if (calls.createBuffer(calls.device, &info, nullptr, &chunk.buffer) != VK_SUCCESS) return {};
            VkMemoryRequirements requirements;
            calls.bufferRequirements(calls.device, chunk.buffer, &requirements);
            // Host-visible and coherent, cached where available: the workers write it.
            int type = -1;
            for (VkMemoryPropertyFlags wanted : {VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
                                                                       VK_MEMORY_PROPERTY_HOST_CACHED_BIT),
                                                 VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)})
                for (uint32_t i = 0; i < calls.memory.memoryTypeCount && type < 0; ++i)
                    if ((requirements.memoryTypeBits & (1u << i)) && (calls.memory.memoryTypes[i].propertyFlags & wanted) == wanted) type = int(i);
            VkMemoryAllocateInfo allocate{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
            allocate.allocationSize = requirements.size;
            allocate.memoryTypeIndex = uint32_t(type);
            void* pointer = nullptr;
            if (type < 0 || calls.allocateMemory(calls.device, &allocate, nullptr, &chunk.memory) != VK_SUCCESS) {
                calls.destroyBuffer(calls.device, chunk.buffer, nullptr);
                return {};
            }
            if (calls.bindBufferMemory(calls.device, chunk.buffer, chunk.memory, 0) != VK_SUCCESS ||
                calls.mapMemory(calls.device, chunk.memory, 0, VK_WHOLE_SIZE, 0, &pointer) != VK_SUCCESS) {
                calls.destroyBuffer(calls.device, chunk.buffer, nullptr);
                calls.freeMemory(calls.device, chunk.memory, nullptr);
                return {};
            }
            chunk.mapped = static_cast<uint8_t*>(pointer);
            return chunk;
        }
        // Keeps up to 64 MiB of staging for reuse.
        void give_back(std::vector<Chunk>& chunks_) {
            std::lock_guard lock(chunks);
            VkDeviceSize kept = 0;
            for (auto& chunk : free) kept += chunk.size;
            for (auto& chunk : chunks_) {
                if (kept + chunk.size <= (64u << 20)) {
                    kept += chunk.size;
                    free.push_back(chunk);
                } else {
                    calls.unmapMemory(calls.device, chunk.memory);
                    calls.destroyBuffer(calls.device, chunk.buffer, nullptr);
                    calls.freeMemory(calls.device, chunk.memory, nullptr);
                }
            }
            chunks_.clear();
        }
        void destroy_pool() {
            std::vector<Chunk> all;
            {
                std::lock_guard lock(chunks);
                all.swap(free);
            }
            for (auto& chunk : all) {
                calls.unmapMemory(calls.device, chunk.memory);
                calls.destroyBuffer(calls.device, chunk.buffer, nullptr);
                calls.freeMemory(calls.device, chunk.memory, nullptr);
            }
        }
    };
    struct CommandState {
        DeviceState* device = nullptr;
        std::vector<std::shared_ptr<Upload>> uploads;
        std::vector<Chunk> chunks;
        std::vector<VkCommandBuffer> secondaries;
    };
    struct Stats {
        uint64_t images = 0, uploads = 0, uploadTexels = 0, retranscodes = 0;
        uint64_t emulatedBytes = 0, bc7Bytes = 0;
        double submitWaitSeconds = 0;
        std::chrono::steady_clock::time_point nextReport{};
    };

    // Only images used as plain sampled textures: no other format may ever
    // see their memory, and no extension structure changes their meaning.
    static bool substitutable(const VkImageCreateInfo& info) {
        const VkImageCreateFlags reinterpreting = VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT | VK_IMAGE_CREATE_BLOCK_TEXEL_VIEW_COMPATIBLE_BIT |
                                                  VK_IMAGE_CREATE_EXTENDED_USAGE_BIT | VK_IMAGE_CREATE_ALIAS_BIT |
                                                  VK_IMAGE_CREATE_SPARSE_BINDING_BIT;
        const VkImageUsageFlags plain = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        return info.imageType == VK_IMAGE_TYPE_2D && info.tiling == VK_IMAGE_TILING_OPTIMAL && !(info.flags & reinterpreting) &&
               !(info.usage & ~plain) && info.pNext == nullptr;
    }
    static uint64_t emulated_bytes(const VkImageCreateInfo& info, int bw, int bh) {
        uint64_t bytes = 0;
        for (uint32_t mip = 0; mip < info.mipLevels; ++mip) {
            const uint64_t w = std::max(1u, info.extent.width >> mip), h = std::max(1u, info.extent.height >> mip);
            bytes += w * h * 4 + (w + bw - 1) / bw * ((h + bh - 1) / bh) * 16;
        }
        return bytes * info.arrayLayers;
    }
    static uint64_t bc7_bytes(const VkImageCreateInfo& info) {
        uint64_t bytes = 0;
        for (uint32_t mip = 0; mip < info.mipLevels; ++mip)
            bytes += uint64_t((std::max(1u, info.extent.width >> mip) + 3) / 4) * ((std::max(1u, info.extent.height >> mip) + 3) / 4) * 16;
        return bytes * info.arrayLayers;
    }
    static bool host_visible(const DeviceState& state, uint32_t type) {
        return type < state.calls.memory.memoryTypeCount &&
               (state.calls.memory.memoryTypes[type].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
    }

    // Where a region's ASTC bytes are and where its BC7 blocks go. BC7 blocks
    // cover 4 x 4 texels, so the region must start on them and end on them or
    // at the edge of the mip level.
    bool plan(const Image& image, const VkBufferImageCopy& copy, Region* out) {
        const uint32_t mipWidth = std::max(1u, image.extent.width >> copy.imageSubresource.mipLevel);
        const uint32_t mipHeight = std::max(1u, image.extent.height >> copy.imageSubresource.mipLevel);
        const VkOffset3D& o = copy.imageOffset;
        const VkExtent3D& e = copy.imageExtent;
        if (o.x % 4 || o.y % 4 || (e.width % 4 && o.x + e.width != mipWidth) || (e.height % 4 && o.y + e.height != mipHeight) ||
            !e.width || !e.height) {
            warn("upload region does not fall on BC7 blocks; texture left without that content");
            return false;
        }
        Region& r = *out;
        r.copy = copy;
        r.width = int(e.width);
        r.height = int(e.height);
        r.slices = int(copy.imageSubresource.layerCount * std::max(1u, e.depth));
        const uint32_t rowTexels = copy.bufferRowLength ? copy.bufferRowLength : e.width;
        const uint32_t sliceTexels = copy.bufferImageHeight ? copy.bufferImageHeight : e.height;
        r.astcRowBytes = size_t((rowTexels + image.bw - 1) / image.bw) * 16;
        r.astcSliceBytes = r.astcRowBytes * ((sliceTexels + image.bh - 1) / image.bh);
        r.astcBytes = VkDeviceSize(r.astcSliceBytes) * (r.slices - 1) + r.astcRowBytes * ((e.height + image.bh - 1) / image.bh - 1) +
                      VkDeviceSize((e.width + image.bw - 1) / image.bw) * 16;
        r.bc7RowBytes = size_t((e.width + 3) / 4) * 16;
        r.bc7SliceBytes = r.bc7RowBytes * ((e.height + 3) / 4);
        r.bc7Bytes = VkDeviceSize(r.bc7SliceBytes) * r.slices;
        return true;
    }

    // Queues the transcode of every region in bands of about 64 K texels.
    void start(Upload& upload) {
        upload.batch = std::make_shared<Batch>();
        upload.texels = 0;
        auto source = upload.source;
        for (const Region& region : upload.regions) {
            const int unit = band_rows(upload.image.bh);
            const int band = std::max(unit, (65536 / std::max(1, region.width)) / unit * unit);
            for (int slice = 0; slice < region.slices; ++slice) {
                const AstcRegion in{source->data() + region.sourceOffset + size_t(slice) * region.astcSliceBytes, region.astcRowBytes,
                                    upload.image.bw, upload.image.bh, upload.image.srgb, region.width, region.height};
                const Bc7Output out{upload.target + region.bc7Offset + size_t(slice) * region.bc7SliceBytes, region.bc7RowBytes};
                for (int y = 0; y < region.height; y += band) {
                    const int last = std::min(region.height, y + band);
                    Workers::instance().submit(upload.batch, [in, out, y, last, source] { astc_to_bc7(in, out, y, last); });
                }
            }
            upload.texels += uint64_t(region.width) * region.height * region.slices;
        }
    }

    void collect(VkCommandBuffer command, std::vector<std::shared_ptr<Upload>>& uploads) {
        auto it = commands_.find(command);
        if (it == commands_.end()) return;
        uploads.insert(uploads.end(), it->second->uploads.begin(), it->second->uploads.end());
        for (VkCommandBuffer secondary : it->second->secondaries) collect(secondary, uploads);
    }
    void release(DeviceState& device, CommandState& state) {
        for (auto& upload : state.uploads) upload->batch->wait(); // workers may still write staging memory
        device.give_back(state.chunks);
    }
    CommandState& command_state(VkCommandBuffer command, DeviceState* device) {
        auto& slot = commands_[command];
        if (!slot) slot = std::make_shared<CommandState>();
        if (device) slot->device = device;
        return *slot;
    }
    std::shared_ptr<DeviceState> device_state(VkDevice device) {
        std::lock_guard lock(mutex_);
        return device_of(device);
    }
    std::shared_ptr<DeviceState> device_of(VkDevice device) {
        auto it = devices_.find(device);
        return it == devices_.end() ? nullptr : it->second;
    }
    std::shared_ptr<DeviceState> device_of(DeviceState* state) {
        for (auto& [handle, device] : devices_)
            if (device.get() == state) return device;
        return nullptr;
    }
    // Development aid: debug.axrb.astc_dump=N saves the first N uploaded
    // regions as .astc files in the game's cache directory.
    void dump(const Upload& upload) {
        static const int limit = [] {
            char value[PROP_VALUE_MAX]{};
            __system_property_get("debug.axrb.astc_dump", value);
            return std::atoi(value);
        }();
        static std::atomic<int> dumped{0};
        if (limit <= 0) return;
        char package[256]{};
        if (FILE* cmdline = std::fopen("/proc/self/cmdline", "re")) {
            if (!std::fgets(package, sizeof(package), cmdline)) package[0] = 0;
            std::fclose(cmdline);
        }
        for (const Region& region : upload.regions) {
            const int index = dumped.fetch_add(1);
            if (index >= limit) return;
            char path[512];
            std::snprintf(path, sizeof(path), "/data/data/%s/cache/axrb_astc_%04d_%dx%d_%s.astc", package, index, upload.image.bw,
                          upload.image.bh, upload.image.srgb ? "s" : "l");
            FILE* file = std::fopen(path, "we");
            if (!file) return;
            const uint8_t header[16] = {0x13, 0xab, 0xa1, 0x5c, uint8_t(upload.image.bw), uint8_t(upload.image.bh), 1,
                                        uint8_t(region.width), uint8_t(region.width >> 8), uint8_t(region.width >> 16),
                                        uint8_t(region.height), uint8_t(region.height >> 8), uint8_t(region.height >> 16), 1, 0, 0};
            std::fwrite(header, 1, 16, file);
            const size_t across = size_t((region.width + upload.image.bw - 1) / upload.image.bw) * 16;
            for (int row = 0; row < (region.height + upload.image.bh - 1) / upload.image.bh; ++row)
                std::fwrite(upload.source->data() + region.sourceOffset + size_t(row) * region.astcRowBytes, 1, across, file);
            std::fclose(file);
        }
    }

    void report() {
        const auto now = std::chrono::steady_clock::now();
        if (now < stats_.nextReport) return;
        stats_.nextReport = now + std::chrono::seconds(10);
        __android_log_print(ANDROID_LOG_INFO, tag,
                            "%llu ASTC images as BC7: %.1f MiB instead of %.1f MiB emulated; %llu uploads, %.1f Mtexel transcoded, "
                            "%llu re-transcoded, %.2f s waited at submit",
                            (unsigned long long)stats_.images, stats_.bc7Bytes / 1048576.0, stats_.emulatedBytes / 1048576.0,
                            (unsigned long long)stats_.uploads, stats_.uploadTexels / 1e6, (unsigned long long)stats_.retranscodes,
                            stats_.submitWaitSeconds);
    }

    std::mutex mutex_;
    std::unordered_map<VkDevice, std::shared_ptr<DeviceState>> devices_;
    std::unordered_map<VkImage, Image> images_;
    std::unordered_map<VkBuffer, Buffer> buffers_;
    std::unordered_map<VkDeviceMemory, Memory> memories_;
    std::unordered_map<VkCommandBuffer, VkCommandPool> pools_;
    std::unordered_map<VkCommandBuffer, std::shared_ptr<CommandState>> commands_;
    Stats stats_;
    std::mutex warnings_;
    int warned_ = 0;
};

inline AstcSubstitution& astc_images() { return AstcSubstitution::instance(); }

} // namespace axrb::texture
