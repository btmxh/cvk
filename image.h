#pragma once

#include "memory.h"
#include "types.h"
#include <vk_mem_alloc.h>
#include <vulkan/vulkan_core.h>

typedef struct {
  VkCommandPool blit_command_pool;
  VkCommandBuffer blit_command_buffer;
  i32 mip_levels;
} mipmap_context;

bool image_load_from_file(VkPhysicalDevice physical_device,
                          const transfer_context *tctx, const char *path,
                          VkImageUsageFlags usage,
                          VkImageLayout transition_layout,
                          mipmap_context *mipmap, VkImage *image,
                          VmaAllocation *allocation, VkImageView *image_view,
                          VkSampler *sampler);
bool image_init_depth_buffer(VkPhysicalDevice physical_device,
                             const transfer_context *tctx, VkExtent2D size,
                             VkSampleCountFlags samples, VkImage *image,
                             VmaAllocation *image_allocation, VkFormat *format,
                             VkImageView *view);
bool image_init_msaa_buffer(const transfer_context* tctx, VkExtent2D size,
                            VkSampleCountFlags samples, VkFormat format,
                            VkImage *image, VmaAllocation *image_allocation,
                            VkImageView *view);
void image_free(const transfer_context *c, VkImage image,
                VmaAllocation allocation, VkImageView image_view,
                VkSampler sampler);
