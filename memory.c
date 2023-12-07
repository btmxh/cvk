#include "memory.h"
#include "command.h"
#include "device.h"
#include "vk_utils.h"
#include <assert.h>
#include <logger.h>
#include <vk_mem_alloc.h>
// see
// https://stackoverflow.com/questions/62374711/c-inline-function-generates-undefined-symbols-error
#define inline static inline
#include <vulkan/utility/vk_format_utils.h>
#include <vulkan/vulkan_core.h>

bool vma_create(VkInstance instance, VkPhysicalDevice physical_device,
                VkDevice device, VmaAllocator *allocator) {
  VkResult result;
  if ((result = vmaCreateAllocator(
           &(VmaAllocatorCreateInfo){
               .device = device,
               .instance = instance,
               .physicalDevice = physical_device,
           },
           allocator)) != VK_SUCCESS) {
    LOG_ERROR("unable to create vulkan memory allocator: %s",
              vk_error_to_string(result));
    return false;
  }

  return true;
}
void vma_destroy(VmaAllocator allocator) { vmaDestroyAllocator(allocator); }

bool transfer_context_init(VkDevice device, VmaAllocator allocator,
                           const queue_family_indices *indices,
                           transfer_context *c) {
  c->device = device;
  c->vma = allocator;
  c->indices = *indices;

  u32 transfer_queue_index = indices->graphics;
  vkGetDeviceQueue(device, indices->graphics, 0, &c->graphics_queue);
  if (indices->transfer != VK_QUEUE_FAMILY_IGNORED) {
    LOG_INFO("dedicated transfer queue found, using it for data transfer");
    transfer_queue_index = indices->transfer;
    vkGetDeviceQueue(device, indices->transfer, 0, &c->transfer_queue);
  } else {
    c->transfer_queue = c->graphics_queue;
  }

  VkResult result;
  if (!command_pool_create(device, transfer_queue_index, &c->command_pool)) {
    LOG_ERROR("unable to create command pool for transfer queue");
    goto fail_command_pool;
  }

  if (!command_buffer_allocate(device, c->command_pool,
                               VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1,
                               &c->command_buffer)) {
    LOG_ERROR("unable to allocate command buffer for transfering");
    goto fail_command_buffer;
  }

  if ((result = vkCreateFence(device,
                              &(VkFenceCreateInfo){
                                  .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
                              },
                              NULL, &c->fence)) != VK_SUCCESS) {
    LOG_ERROR("unable to create transfer fence");
    goto fail_fence;
  }

  return true;

fail_fence:
fail_command_buffer:
  command_pool_free(device, c->command_pool);
fail_command_pool:
  return false;
}

void transfer_context_free(transfer_context *c) {
  command_pool_free(c->device, c->command_pool);
  vkDestroyFence(c->device, c->fence, NULL);
}

static bool transfer_context_begin_command_buffer(const transfer_context *c) {
  VkResult result;
  if ((result = vkResetCommandPool(c->device, c->command_pool, 0)) !=
      VK_SUCCESS) {
    LOG_ERROR("unable to reset transfer context command buffer");
    return false;
  }
  if ((result = vkBeginCommandBuffer(
           c->command_buffer,
           &(VkCommandBufferBeginInfo){
               .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
               .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
           })) != VK_SUCCESS) {
    LOG_ERROR("unable to begin recording command buffer: %s",
              vk_error_to_string(result));
    return false;
  }

  return true;
}

static bool transfer_context_create_staging_buffer(
    const transfer_context *c, i32 size, VkBuffer *buffer,
    VmaAllocation *allocation, VmaAllocationInfo *alloc_info) {
  bool has_dedicated_transfer_queue =
      c->indices.transfer != VK_QUEUE_FAMILY_IGNORED;
  VkResult result;
  if ((result = vmaCreateBuffer(
           c->vma,
           &(VkBufferCreateInfo){
               .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
               .size = size,
               .queueFamilyIndexCount = 1,
               .pQueueFamilyIndices = has_dedicated_transfer_queue
                                          ? &c->indices.transfer
                                          : &c->indices.graphics,
               .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
               .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
           },
           &(VmaAllocationCreateInfo){
               .usage = VMA_MEMORY_USAGE_AUTO,
               .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                        VMA_ALLOCATION_CREATE_MAPPED_BIT,
           },
           buffer, allocation, alloc_info)) != VK_SUCCESS) {
    LOG_ERROR("unable to create staging buffer for buffer copy: %s",
              vk_error_to_string(result));
    return false;
  }

  return true;
}

bool transfer_context_end_exec_command_buffer(const transfer_context *c) {
  VkResult result;
  if ((result = vkEndCommandBuffer(c->command_buffer)) != VK_SUCCESS) {
    LOG_ERROR("unable to end recording command buffer: %s",
              vk_error_to_string(result));
    return false;
  }
  if ((result = vkResetFences(c->device, 1, &c->fence)) != VK_SUCCESS) {
    LOG_ERROR("unable to reset transfer fence: %s", vk_error_to_string(result));
    return false;
  }

  if ((result = vkQueueSubmit(c->transfer_queue, 1,
                              &(VkSubmitInfo){
                                  .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                                  .commandBufferCount = 1,
                                  .pCommandBuffers = &c->command_buffer,
                              },
                              c->fence)) != VK_SUCCESS) {
    LOG_ERROR("unable to submit copy work to transfer queue: %s",
              vk_error_to_string(result));
    return false;
  }

  if ((result = vkWaitForFences(c->device, 1, &c->fence, VK_TRUE,
                                UINT64_MAX)) != VK_SUCCESS) {
    LOG_ERROR("unable to wait for transfer fence: %s",
              vk_error_to_string(result));
    return false;
  }

  return true;
}

bool transfer_context_stage_to_buffer(const transfer_context *c,
                                      VkBuffer buffer, i32 size, i32 offset,
                                      const void *data) {
  assert(size >= 0 && offset >= 0);
  VkBuffer staging_buffer;
  VmaAllocation allocation;
  VmaAllocationInfo alloc_info;
  if (!transfer_context_create_staging_buffer(c, size, &staging_buffer,
                                              &allocation, &alloc_info)) {
    LOG_ERROR("unable to create staging buffer");
    goto fail_staging_buffer;
  }

  memcpy(alloc_info.pMappedData, data, size);

  if (!transfer_context_begin_command_buffer(c)) {
    LOG_ERROR("unable to begin recording command buffer");
    goto fail_begin_command_buffer;
  }

  vkCmdCopyBuffer(c->command_buffer, staging_buffer, buffer, 1,
                  (VkBufferCopy[]){(VkBufferCopy){
                      .srcOffset = 0,
                      .dstOffset = offset,
                      .size = size,
                  }});

  if (!transfer_context_end_exec_command_buffer(c)) {
    LOG_ERROR("unable to execute command buffer");
    goto fail_exec;
  }

  vmaDestroyBuffer(c->vma, staging_buffer, allocation);
  return true;

fail_exec:
fail_begin_command_buffer:
  vmaDestroyBuffer(c->vma, staging_buffer, allocation);
fail_staging_buffer:
  return false;
}

static bool transfer_context_transition_image_layout(const transfer_context *c,
                                                     VkImage image,
                                                     VkFormat format,
                                                     VkImageLayout from,
                                                     VkImageLayout to) {
  (void)format;
  VkAccessFlags src_access_mask = 0, dst_access_mask = 0;
  VkPipelineStageFlags src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                       dst_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
  if (from == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
    src_access_mask = VK_ACCESS_TRANSFER_WRITE_BIT;
    src_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
  }

  if (to == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
    dst_access_mask = VK_ACCESS_TRANSFER_WRITE_BIT;
    dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
  }

  if (to == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
    dst_access_mask = VK_ACCESS_SHADER_READ_BIT;
    dst_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  }

  vkCmdPipelineBarrier(c->command_buffer, src_stage, dst_stage, 0, 0, NULL, 0,
                       NULL, 1,
                       &(VkImageMemoryBarrier){
                           .image = image,
                           .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                           .oldLayout = from,
                           .newLayout = to,
                           .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                           .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                           .subresourceRange =
                               {
                                   .baseMipLevel = 0,
                                   .levelCount = 1,
                                   .baseArrayLayer = 0,
                                   .layerCount = 1,
                                   .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                               },
                           .srcAccessMask = src_access_mask,
                           .dstAccessMask = dst_access_mask,
                       });
  return true;
}

bool transfer_context_stage_linear_data_to_2d_image(
    const transfer_context *c, VkImage image, VkRect2D region,
    const void *image_pixels, VkFormat format,
    VkImageLayout transition_layout) {
  i32 buffer_size =
      region.extent.width * region.extent.height * vkuFormatTexelSize(format);
  assert(vkuFormatPlaneCount(format) == 1);

  VkBuffer staging_buffer;
  VmaAllocation allocation;
  VmaAllocationInfo alloc_info;
  if (!transfer_context_create_staging_buffer(c, buffer_size, &staging_buffer,
                                              &allocation, &alloc_info)) {
    LOG_ERROR("unable to create staging buffer");
    goto fail_staging_buffer;
  }

  memcpy(alloc_info.pMappedData, image_pixels, buffer_size);

  if (!transfer_context_begin_command_buffer(c)) {
    LOG_ERROR("unable to begin recording command buffer");
    goto fail_begin_command_buffer;
  }

  transfer_context_transition_image_layout(
      c, image, format, VK_IMAGE_LAYOUT_UNDEFINED,
      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

  vkCmdCopyBufferToImage(c->command_buffer, staging_buffer, image,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                         &(VkBufferImageCopy){
                             .imageOffset =
                                 (VkOffset3D){
                                     .x = region.offset.x,
                                     .y = region.offset.y,
                                     .z = 0,
                                 },
                             .imageExtent =
                                 (VkExtent3D){
                                     .width = region.extent.width,
                                     .height = region.extent.height,
                                     .depth = 1,
                                 },
                             .bufferOffset = 0,
                             .bufferImageHeight = 0,
                             .bufferRowLength = 0,
                             .imageSubresource =
                                 (VkImageSubresourceLayers){
                                     .mipLevel = 0,
                                     .layerCount = 1,
                                     .baseArrayLayer = 0,
                                     .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                 },
                         });

  if (transition_layout != VK_IMAGE_LAYOUT_UNDEFINED &&
      transition_layout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
    transfer_context_transition_image_layout(
        c, image, format, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        transition_layout);
  }

  if (!transfer_context_end_exec_command_buffer(c)) {
    LOG_ERROR("unable to execute command buffer");
    goto fail_exec;
  }

  vmaDestroyBuffer(c->vma, staging_buffer, allocation);
  return true;

fail_exec:
fail_begin_command_buffer:
  vmaDestroyBuffer(c->vma, staging_buffer, allocation);
fail_staging_buffer:
  return false;
}
