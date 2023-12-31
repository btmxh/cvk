#pragma once

#include "types.h"
#include <vulkan/vulkan_core.h>

VkSampleCountFlagBits best_msaa_sample_count(VkPhysicalDevice physical_device);

VkPhysicalDevice physical_device_pick(VkInstance instance,
                                      VkSurfaceKHR surface);

typedef struct {
  u32 graphics;
  u32 present;
  u32 transfer;
} queue_family_indices;

bool find_queue_families(VkPhysicalDevice device, VkSurfaceKHR surface,
                         queue_family_indices *indices);

bool queue_family_indices_complete(const queue_family_indices *indices);

u32 *remove_duplicate_and_invalid_indices(u32 *indices, i32 num_indices,
                                          i32 *num_unique_indices,
                                          VkSharingMode *sharing_mode);

bool device_init(VkPhysicalDevice physical_device, VkSurfaceKHR surface,
                 VkDevice *device);
void device_free(VkDevice device);

typedef struct {
  VkSurfaceCapabilitiesKHR caps;
  VkSurfaceFormatKHR *formats;
  u32 num_formats;
  VkPresentModeKHR *present_modes;
  u32 num_present_modes;
} swap_chain_support_details;

bool query_swap_chain_support(VkPhysicalDevice device, VkSurfaceKHR surface,
                              swap_chain_support_details *details);
void swap_chain_support_details_free(swap_chain_support_details *details);
