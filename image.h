#pragma once

#include "memory.h"
#include "types.h"
#include <vk_mem_alloc.h>
#include <vulkan/vulkan_core.h>

bool image_load_from_file(const transfer_context *tctx, const char *path,
                          VkImage *image, VmaAllocation *image_allocation,
                          VkImageView *image_view);
void image_free(const transfer_context *c, VkImage image,
                VmaAllocation allocation, VkImageView image_view);

bool sampler_create(VkPhysicalDevice physical_device, VkDevice device,
                    VkSampler *sampler);
void sampler_free(VkDevice device, VkSampler sampler);
