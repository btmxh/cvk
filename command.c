#include "command.h"
#include "vk_utils.h"
#include <logger.h>
#include <vulkan/vulkan_core.h>

bool command_pool_create(VkDevice device, u32 graphics_queue_index,
                         VkCommandPool *pool) {
  VkResult result;
  if ((result = vkCreateCommandPool(
           device,
           &(VkCommandPoolCreateInfo){
               .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
               .flags = 0,
               .queueFamilyIndex = graphics_queue_index,
           },
           NULL, pool)) != VK_SUCCESS) {
    LOG_ERROR("unable to create command pool");
    return false;
  }

  return true;
}

void command_pool_free(VkDevice device, VkCommandPool pool) {
  vkDestroyCommandPool(device, pool, NULL);
}

bool command_buffer_allocate(VkDevice device, VkCommandPool pool,
                             VkCommandBufferLevel level, u32 num_buffers,
                             VkCommandBuffer *buffers) {
  VkResult result;
  if ((result = vkAllocateCommandBuffers(
           device,
           &(VkCommandBufferAllocateInfo){
               .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
               .level = level,
               .commandPool = pool,
               .commandBufferCount = num_buffers,
           },
           buffers)) != VK_SUCCESS) {
    LOG_ERROR("unable to allocate command buffers from command pool: %s",
              vk_error_to_string(result));
  }

  return true;
}

void command_buffer_free(VkDevice device, VkCommandPool pool, u32 num_buffers,
                         VkCommandBuffer *buffers) {
  vkFreeCommandBuffers(device, pool, num_buffers, buffers);
}

