#pragma once

#include <vulkan/vk_enum_string_helper.h>
#include <vulkan/vulkan_core.h>

#define PRIvkVer "%" PRIu32 ".%" PRIu32 ".%" PRIu32 ".%" PRIu32
#define PRIvkVerArg(version)                                                   \
  VK_API_VERSION_VARIANT(version), VK_API_VERSION_MAJOR(version),              \
      VK_API_VERSION_MINOR(version), VK_API_VERSION_PATCH(version)

static const char *vk_error_to_string(VkResult result) {
  return string_VkResult(result);
}
