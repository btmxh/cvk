#pragma once
#include "types.h"
#include <vulkan/vulkan_core.h>

bool vk_instance_init(VkInstance *inst);
void vk_instance_free(VkInstance inst);

const char** get_validation_layers(u32* num_layers);
