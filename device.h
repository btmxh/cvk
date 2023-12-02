#pragma once

#include "types.h"
#include <vulkan/vulkan_core.h>

VkPhysicalDevice physical_device_pick(VkInstance instance);

typedef i64 queue_index;
#define QUEUE_INDEX_NONE -1

typedef struct {
  queue_index graphics;
} queue_family_indices;

bool find_queue_families(VkPhysicalDevice device,
                         queue_family_indices *indices);

bool device_init(VkInstance instance, VkPhysicalDevice physical_device,
                     VkDevice *device);
void device_free(VkDevice device);
