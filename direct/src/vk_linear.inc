// Linear mobile-compressed images expose a CPU staging buffer and a separate
// optimal desktop-compressed image. The guest never writes desktop tiled data.
struct LinearImage {
  VkDevice device{};
  VkImage image{};
  VkBuffer buffer{};
  VkDeviceMemory backing{};
  VkMemoryRequirements requirements{};
  std::vector<VkSubresourceLayout> layouts;
  std::vector<VkBufferImageCopy> regions;
  uint32_t levels{};
};
std::mutex g_linear_lock;
std::map<VkImage, std::shared_ptr<LinearImage>> g_linear;
std::shared_ptr<LinearImage> linear_image(VkImage image) {
  std::lock_guard guard(g_linear_lock);
  auto it = g_linear.find(image);
  return it == g_linear.end() ? nullptr : it->second;
}
VkResult create_linear(VkDevice device, const VkImageCreateInfo *info,
                       VkImage *out) {
  auto calls = calls_for(device);
  if (!calls)
    return VK_ERROR_FORMAT_NOT_SUPPORTED;
  if (info->imageType != VK_IMAGE_TYPE_2D || info->extent.depth != 1)
    return VK_ERROR_FORMAT_NOT_SUPPORTED;
  int bw, bh;
  bool srgb;
  texture::Codec codec;
  VkFormat target;
  if (!texture::compressed_format(info->format, bw, bh, srgb, codec, target))
    return VK_ERROR_FORMAT_NOT_SUPPORTED;
  auto linear = std::make_shared<LinearImage>();
  linear->device = device;
  linear->levels = info->mipLevels;
  VkDeviceSize size = 0;
  const uint32_t blockBytes =
      codec == texture::Codec::Astc ? 16 : texture::block_bytes(codec);
  for (uint32_t layer = 0; layer < info->arrayLayers; ++layer) {
    for (uint32_t level = 0; level < info->mipLevels; ++level) {
      uint32_t w = std::max(1u, info->extent.width >> level),
               h = std::max(1u, info->extent.height >> level);
      VkSubresourceLayout layout{};
      layout.offset = size;
      layout.rowPitch = uint64_t((w + bw - 1) / bw) * blockBytes;
      layout.size = layout.rowPitch * ((h + bh - 1) / bh);
      layout.depthPitch = layout.size;
      linear->layouts.push_back(layout);
      VkBufferImageCopy region{};
      region.bufferOffset = size;
      region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level, layer, 1};
      region.imageExtent = {w, h, 1};
      linear->regions.push_back(region);
      size += layout.size;
      size = (size + 15) & ~VkDeviceSize(15);
    }
  }
  for (auto &layout : linear->layouts)
    layout.arrayPitch = size / info->arrayLayers;
  VkBufferCreateInfo buffer{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  buffer.size = size;
  buffer.usage =
      VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
  buffer.sharingMode = info->sharingMode;
  buffer.queueFamilyIndexCount = info->queueFamilyIndexCount;
  buffer.pQueueFamilyIndices = info->pQueueFamilyIndices;
  VkResult result =
      calls->createBuffer(device, &buffer, nullptr, &linear->buffer);
  if (result != VK_SUCCESS)
    return result;
  calls->bufferRequirements(device, linear->buffer, &linear->requirements);
  auto optimal = *info;
  optimal.tiling = VK_IMAGE_TILING_OPTIMAL;
  optimal.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  optimal.usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  result = images().create_image(device, &optimal, nullptr, out);
  if (result != VK_SUCCESS) {
    calls->destroyBuffer(device, linear->buffer, nullptr);
    return result;
  }
  linear->image = *out;
  images().created_buffer(linear->buffer, size);
  std::lock_guard guard(g_linear_lock);
  g_linear[*out] = std::move(linear);
  return VK_SUCCESS;
}
uint64_t GetImageMemoryRequirements(const uint64_t *a) {
  auto linear = linear_image(a_<VkImage>(a[1]));
  if (linear)
    *a_<VkMemoryRequirements *>(a[2]) = linear->requirements;
  else
    host<PFN_vkGetImageMemoryRequirements>("vkGetImageMemoryRequirements")(
        a_<VkDevice>(a[0]), a_<VkImage>(a[1]),
        a_<VkMemoryRequirements *>(a[2]));
  return 0;
}
uint64_t image_memory_requirements2(const uint64_t *a, const char* name) {
  auto *info = a_<const VkImageMemoryRequirementsInfo2 *>(a[1]);
  auto linear = linear_image(info->image);
  auto *out = a_<VkMemoryRequirements2 *>(a[2]);
  if (!linear)
    host<PFN_vkGetImageMemoryRequirements2>(name)(
        a_<VkDevice>(a[0]), info, out);
  else {
    out->memoryRequirements = linear->requirements;
    for (auto *p = static_cast<VkBaseOutStructure *>(out->pNext); p;
         p = p->pNext)
      if (p->sType == VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS) {
        auto *dedicated = reinterpret_cast<VkMemoryDedicatedRequirements *>(p);
        dedicated->prefersDedicatedAllocation =
            dedicated->requiresDedicatedAllocation = VK_FALSE;
      }
  }
  return 0;
}
uint64_t GetImageMemoryRequirements2(const uint64_t *a) {
  return image_memory_requirements2(a, "vkGetImageMemoryRequirements2");
}
uint64_t GetImageMemoryRequirements2KHR(const uint64_t *a) {
  return image_memory_requirements2(a, "vkGetImageMemoryRequirements2KHR");
}
VkResult bind_linear(VkDevice device, VkImage image, VkDeviceMemory memory,
                     VkDeviceSize offset) {
  auto linear = linear_image(image);
  auto bind = host<PFN_vkBindImageMemory>("vkBindImageMemory");
  if (!linear)
    return bind(device, image, memory, offset);
  auto calls = calls_for(device);
  VkMemoryRequirements req{};
  host<PFN_vkGetImageMemoryRequirements>("vkGetImageMemoryRequirements")(
      device, image, &req);
  VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  allocation.allocationSize = req.size;
  while (allocation.memoryTypeIndex < calls->memory.memoryTypeCount &&
         !(req.memoryTypeBits & (1u << allocation.memoryTypeIndex)))
    ++allocation.memoryTypeIndex;
  VkResult result = VK_SUCCESS;
  if (!linear->backing) {
    result = calls->allocateMemory(device, &allocation, nullptr, &linear->backing);
    if (result != VK_SUCCESS)
      return result;
    result = bind(device, image, linear->backing, 0);
    if (result != VK_SUCCESS) {
      calls->freeMemory(device, linear->backing, nullptr);
      linear->backing = VK_NULL_HANDLE;
      return result;
    }
  }
  if (result == VK_SUCCESS)
    result = calls->bindBufferMemory(device, linear->buffer, memory, offset);
  if (result == VK_SUCCESS)
    images().bound_buffer(linear->buffer, memory, offset);
  return result;
}
uint64_t BindImageMemory(const uint64_t *a) {
  return r_(bind_linear(a_<VkDevice>(a[0]), a_<VkImage>(a[1]),
                        a_<VkDeviceMemory>(a[2]), a[3]));
}
uint64_t bind_image_memory2(const uint64_t *a, const char* name) {
  auto *infos = a_<const VkBindImageMemoryInfo *>(a[2]);
  for (uint32_t i = 0; i < a[1]; ++i) {
    VkResult result;
    if (linear_image(infos[i].image))
      result = bind_linear(a_<VkDevice>(a[0]), infos[i].image, infos[i].memory,
                           infos[i].memoryOffset);
    else
      result = host<PFN_vkBindImageMemory2>(name)(
          a_<VkDevice>(a[0]), 1, infos + i);
    if (result != VK_SUCCESS)
      return r_(result);
  }
  return r_(VK_SUCCESS);
}
uint64_t BindImageMemory2(const uint64_t *a) {
  return bind_image_memory2(a, "vkBindImageMemory2");
}
uint64_t BindImageMemory2KHR(const uint64_t *a) {
  return bind_image_memory2(a, "vkBindImageMemory2KHR");
}
uint64_t GetImageSubresourceLayout(const uint64_t *a) {
  auto linear = linear_image(a_<VkImage>(a[1]));
  auto *sub = a_<const VkImageSubresource *>(a[2]);
  if (linear) {
    size_t index = size_t(sub->arrayLayer) * linear->levels + sub->mipLevel;
    if (index < linear->layouts.size())
      *a_<VkSubresourceLayout *>(a[3]) = linear->layouts[index];
  } else
    host<PFN_vkGetImageSubresourceLayout>("vkGetImageSubresourceLayout")(
        a_<VkDevice>(a[0]), a_<VkImage>(a[1]), sub,
        a_<VkSubresourceLayout *>(a[3]));
  return 0;
}
void destroy_linear(VkDevice device, VkImage image) {
  auto linear = linear_image(image);
  if (!linear)
    return;
  auto calls = calls_for(device);
  images().destroyed_buffer(linear->buffer);
  calls->destroyBuffer(device, linear->buffer, nullptr);
  if (linear->backing)
    calls->freeMemory(device, linear->backing, nullptr);
  std::lock_guard guard(g_linear_lock);
  g_linear.erase(image);
}

bool upload_linear(VkCommandBuffer command, VkImage image,
                   VkImageLayout oldLayout, VkAccessFlags2 sourceAccess,
                   const VkImageSubresourceRange &range) {
  auto linear = linear_image(image);
  if (!linear || (!(sourceAccess & VK_ACCESS_2_HOST_WRITE_BIT) &&
                  oldLayout != VK_IMAGE_LAYOUT_PREINITIALIZED))
    return false;
  VkImageMemoryBarrier transition{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
  transition.oldLayout = oldLayout == VK_IMAGE_LAYOUT_PREINITIALIZED
                             ? VK_IMAGE_LAYOUT_UNDEFINED
                             : oldLayout;
  transition.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  transition.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  transition.srcQueueFamilyIndex = transition.dstQueueFamilyIndex =
      VK_QUEUE_FAMILY_IGNORED;
  transition.image = image;
  transition.subresourceRange = range;
  host<PFN_vkCmdPipelineBarrier>("vkCmdPipelineBarrier")(
      command, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
      VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1,
      &transition);
  std::vector<VkBufferImageCopy> selected;
  for (const auto &region : linear->regions) {
    const auto &sub = region.imageSubresource;
    if (sub.mipLevel >= range.baseMipLevel &&
        (range.levelCount == VK_REMAINING_MIP_LEVELS ||
         sub.mipLevel - range.baseMipLevel < range.levelCount) &&
        sub.baseArrayLayer >= range.baseArrayLayer &&
        (range.layerCount == VK_REMAINING_ARRAY_LAYERS ||
         sub.baseArrayLayer - range.baseArrayLayer < range.layerCount))
      selected.push_back(region);
  }
  images().copy_buffer_to_image(command, linear->device, linear->buffer, image,
                                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                static_cast<uint32_t>(selected.size()),
                                selected.data());
  return true;
}
uint64_t CmdPipelineBarrier(const uint64_t *a) {
  auto command = a_<VkCommandBuffer>(a[0]);
  auto *input = a_<const VkImageMemoryBarrier *>(a[9]);
  std::vector<VkImageMemoryBarrier> barriers;
  if (a[8])
    barriers.assign(input, input + a[8]);
  VkPipelineStageFlags source = static_cast<VkPipelineStageFlags>(a[1]);
  for (auto &barrier : barriers)
    if (upload_linear(command, barrier.image, barrier.oldLayout,
                      barrier.srcAccessMask, barrier.subresourceRange)) {
      barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
      barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
      source |= VK_PIPELINE_STAGE_TRANSFER_BIT;
    }
  host<PFN_vkCmdPipelineBarrier>("vkCmdPipelineBarrier")(
      command, source, static_cast<VkPipelineStageFlags>(a[2]),
      static_cast<VkDependencyFlags>(a[3]), static_cast<uint32_t>(a[4]),
      a_<const VkMemoryBarrier *>(a[5]), static_cast<uint32_t>(a[6]),
      a_<const VkBufferMemoryBarrier *>(a[7]),
      static_cast<uint32_t>(barriers.size()), barriers.data());
  return 0;
}
uint64_t pipeline_barrier2(const uint64_t *a, const char* name) {
  auto command = a_<VkCommandBuffer>(a[0]);
  auto info = *a_<const VkDependencyInfo *>(a[1]);
  std::vector<VkImageMemoryBarrier2> barriers;
  if (info.imageMemoryBarrierCount)
    barriers.assign(info.pImageMemoryBarriers,
                    info.pImageMemoryBarriers + info.imageMemoryBarrierCount);
  for (auto &barrier : barriers)
    if (upload_linear(command, barrier.image, barrier.oldLayout,
                      barrier.srcAccessMask, barrier.subresourceRange)) {
      barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
      barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
      barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    }
  info.pImageMemoryBarriers = barriers.data();
  // Preserve the entry point requested by the guest. A Vulkan 1.1 device can
  // enable KHR_synchronization2 without enabling the Vulkan 1.3 core command.
  // An instance lookup may still return a core trampoline in that case.
  auto next = host<PFN_vkCmdPipelineBarrier2>(name);
  next(command, &info);
  return 0;
}
uint64_t CmdPipelineBarrier2(const uint64_t *a) {
  return pipeline_barrier2(a, "vkCmdPipelineBarrier2");
}
uint64_t CmdPipelineBarrier2KHR(const uint64_t *a) {
  return pipeline_barrier2(a, "vkCmdPipelineBarrier2KHR");
}
