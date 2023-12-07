#pragma once

#include "device.h"
#include "types.h"
#include <vk_mem_alloc.h>
#include <vulkan/vulkan_core.h>

bool vma_create(VkInstance instance, VkPhysicalDevice physical_device,
                VkDevice device, VmaAllocator *allocator);
void vma_destroy(VmaAllocator allocator);

typedef struct {
  VkDevice device;
  VmaAllocator vma;
  queue_family_indices indices;
  VkQueue graphics_queue;
  VkQueue transfer_queue;
  VkCommandPool command_pool;
  VkCommandBuffer command_buffer;
  VkFence fence;
} transfer_context;

bool transfer_context_init(VkDevice device, VmaAllocator allocator,
                           const queue_family_indices *indices,
                           transfer_context *c);
void transfer_context_free(transfer_context *c);
bool transfer_context_stage_to_buffer(const transfer_context *c,
                                      VkBuffer buffer, i32 size, i32 offset,
                                      const void *data);
bool transfer_context_stage_linear_data_to_2d_image(
    const transfer_context *c, VkImage image, VkRect2D region,
    const void *image_pixels, VkFormat format, VkImageLayout transition_layout);
