#pragma once

#include "types.h"
#include <vulkan/vulkan_core.h>

bool command_pool_create(VkDevice device, u32 graphics_queue_index,
                         VkCommandPool *pool);
void command_pool_free(VkDevice device, VkCommandPool pool);

bool command_buffer_allocate(VkDevice device, VkCommandPool pool,
                             VkCommandBufferLevel level, u32 num_buffers,
                             VkCommandBuffer *buffers);
void command_buffer_free(VkDevice device, VkCommandPool pool, u32 num_buffers,
                         VkCommandBuffer* buffers);
