#pragma once

#include "types.h"
#include <vulkan/vulkan_core.h>

typedef struct {
  VkDebugUtilsMessengerEXT debug_messenger;
} debug_messenger;

void debug_msg_create_info(VkDebugUtilsMessengerCreateInfoEXT *info);
bool debug_msg_init(VkInstance inst, debug_messenger *msg);
void debug_msg_free(VkInstance inst, debug_messenger msg);
