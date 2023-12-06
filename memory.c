#include "memory.h"
#include "command.h"
#include "device.h"
#include "vk_utils.h"
#include <assert.h>
#include <logger.h>
#include <vk_mem_alloc.h>
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

bool transfer_context_stage_to_buffer(const transfer_context *c,
                                      VkBuffer buffer, i32 size, i32 offset,
                                      const void *data) {
  assert(size >= 0 && offset >= 0);
  bool has_dedicated_transfer_queue =
      c->indices.transfer != VK_QUEUE_FAMILY_IGNORED;
  VkResult result;
  VkBuffer staging_buffer;
  VmaAllocation allocation;
  VmaAllocationInfo alloc_info;
  if ((result = vmaCreateBuffer(
           c->vma,
           &(VkBufferCreateInfo){
               .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
               .size = size,
               .pQueueFamilyIndices =
                   has_dedicated_transfer_queue
                       ? (u32[]){c->indices.transfer, c->indices.graphics}
                       : (u32[]){c->indices.graphics},
               .queueFamilyIndexCount = has_dedicated_transfer_queue ? 2 : 1,
               .sharingMode = has_dedicated_transfer_queue
                                  ? VK_SHARING_MODE_CONCURRENT
                                  : VK_SHARING_MODE_EXCLUSIVE,
               .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
           },
           &(VmaAllocationCreateInfo){
               .usage = VMA_MEMORY_USAGE_AUTO,
               .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                        VMA_ALLOCATION_CREATE_MAPPED_BIT,
           },
           &staging_buffer, &allocation, &alloc_info)) != VK_SUCCESS) {
    LOG_ERROR("unable to create staging buffer for buffer copy");
    goto fail_staging_buffer;
  }

  memcpy(alloc_info.pMappedData, data, size);

  vkResetCommandBuffer(c->command_buffer,
                       VK_COMMAND_BUFFER_RESET_RELEASE_RESOURCES_BIT);
  if ((result = vkBeginCommandBuffer(
           c->command_buffer,
           &(VkCommandBufferBeginInfo){
               .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
               .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
           })) != VK_SUCCESS) {
    LOG_ERROR("unable to begin recording command buffer: %s",
              vk_error_to_string(result));
    goto fail_begin_command_buffer;
  }
  vkCmdCopyBuffer(c->command_buffer, staging_buffer, buffer, 1,
                  (VkBufferCopy[]){(VkBufferCopy){
                      .srcOffset = 0,
                      .dstOffset = offset,
                      .size = size,
                  }});
  if ((result = vkEndCommandBuffer(c->command_buffer)) != VK_SUCCESS) {
    LOG_ERROR("unable to end recording command buffer: %s",
              vk_error_to_string(result));
    goto fail_end_command_buffer;
  }

  if ((result = vkResetFences(c->device, 1, &c->fence)) != VK_SUCCESS) {
    LOG_ERROR("unable to reset transfer fence: %s", vk_error_to_string(result));
    goto fail_reset_fence;
  }

  if ((result = vkQueueSubmit(c->transfer_queue, 1,
                              &(VkSubmitInfo){
                                  .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                                  .commandBufferCount = 1,
                                  .pCommandBuffers = &c->command_buffer,
                                  .waitSemaphoreCount = 0,
                                  .pWaitSemaphores = NULL,
                                  .pWaitDstStageMask = NULL,
                                  .signalSemaphoreCount = 0,
                                  .pSignalSemaphores = NULL,
                              },
                              c->fence)) != VK_SUCCESS) {
    LOG_ERROR("unable to submit copy work to transfer queue: %s",
              vk_error_to_string(result));
    goto fail_queue_submit;
  }

  if ((result = vkWaitForFences(c->device, 1, &c->fence, VK_TRUE,
                                UINT64_MAX)) != VK_SUCCESS) {
    LOG_ERROR("unable to wait for transfer fence: %s",
              vk_error_to_string(result));
    goto fail_wait;
  }

  vmaDestroyBuffer(c->vma, staging_buffer, allocation);
  return true;

fail_wait:
fail_queue_submit:
fail_reset_fence:
fail_end_command_buffer:
fail_begin_command_buffer:
  vmaDestroyBuffer(c->vma, staging_buffer, allocation);
fail_staging_buffer:
  return false;
}
