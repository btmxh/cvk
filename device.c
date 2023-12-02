#include "device.h"
#include "vk_utils.h"

#include "instance.h"
#include <assert.h>
#include <logger.h>
#include <stdlib.h>
#include <vulkan/vulkan_core.h>

static void queue_family_indices_init(queue_family_indices *indices) {
  indices->graphics = QUEUE_INDEX_NONE;
  indices->present = QUEUE_INDEX_NONE;
}

static bool queue_family_indices_complete(const queue_family_indices *indices) {
  return indices->graphics >= 0 && indices->present >= 0;
}

static const char *required_device_extensions[] = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
};

static const u32 num_required_device_extensions =
    sizeof(required_device_extensions) / sizeof(required_device_extensions[0]);

static bool physical_device_supports_extensions(VkPhysicalDevice device,
                                                const char **extensions,
                                                u32 num_extensions) {
  VkResult result;
  u32 num_supported_extensions;
  result = vkEnumerateDeviceExtensionProperties(
      device, NULL, &num_supported_extensions, NULL);
  if (result != VK_SUCCESS) {
    LOG_ERROR("unable to enumerate device extensions");
    return false;
  }
  VkExtensionProperties *supported_extensions =
      malloc(num_supported_extensions * sizeof(*supported_extensions));
  if (!supported_extensions) {
    LOG_ERROR("unable to allocate extension properties structs");
    return false;
  }

  result = vkEnumerateDeviceExtensionProperties(
      device, NULL, &num_supported_extensions, supported_extensions);
  if (result != VK_SUCCESS) {
    LOG_ERROR("unable to enumerate device extensions");
    return false;
  }

  for (u32 i = 0; i < num_extensions; ++i) {
    bool found = false;
    for (u32 j = 0; j < num_supported_extensions; ++j) {
      if (strcmp(extensions[i], supported_extensions[j].extensionName) == 0) {
        found = true;
        break;
      }
    }

    if (!found) {
      free(supported_extensions);
      return false;
    }
  }

  free(supported_extensions);
  return true;
}

static bool swap_chain_adaquate(const swap_chain_support_details *details) {
  return details->num_formats > 0 && details->num_present_modes > 0;
}

static i32 rate_physical_device(VkPhysicalDevice device, VkSurfaceKHR surface) {
  static const i32 fail = 0, max = INT32_MAX;
  i32 score = 1;

  VkPhysicalDeviceProperties properties;
  VkPhysicalDeviceFeatures features;

  vkGetPhysicalDeviceProperties(device, &properties);
  vkGetPhysicalDeviceFeatures(device, &features);

  LOG_TRACE("Rating physical device '%s' (device ID %" PRIu32
            "), initial score: %" PRIi32,
            properties.deviceName, properties.deviceID, score);

#define INCREASE(reason, amt)                                                  \
  LOG_TRACE("\t" reason ", +%" PRIi32 " to score (now: %" PRIi32 ")", amt,     \
            score += amt);
#define FAIL(reason)                                                           \
  do {                                                                         \
    LOG_TRACE("\t" reason ", score set to %" PRIi32 ", and exit early", fail); \
    return fail;                                                               \
  } while (false);

  queue_family_indices indices;
  find_queue_families(device, surface, &indices);
  if (!queue_family_indices_complete(&indices)) {
    FAIL("physical device not having support for necessary queue families");
  }

  if (!physical_device_supports_extensions(device, required_device_extensions,
                                           num_required_device_extensions)) {
    FAIL("physical device not having support for required extensions");
  }

  swap_chain_support_details swap_chain_support = {};
  if (!query_swap_chain_support(device, surface, &swap_chain_support)) {
    FAIL("unable to query swap chain support details");
  }

  if (!swap_chain_adaquate(&swap_chain_support)) {
    swap_chain_support_details_free(&swap_chain_support);
    FAIL("swap chain support not adaquate");
  }

  if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
    INCREASE("physical device is discrete GPU", 1000);
  }

  INCREASE("increase score by maximum supported image size",
           properties.limits.maxImageDimension2D);

  swap_chain_support_details_free(&swap_chain_support);
  return score;
}

VkPhysicalDevice physical_device_pick(VkInstance instance,
                                      VkSurfaceKHR surface) {
  VkResult result;
  u32 num_physical_devices;
  result = vkEnumeratePhysicalDevices(instance, &num_physical_devices, NULL);
  if (result != VK_SUCCESS) {
    LOG_ERROR("unable to query physical devices: %s",
              vk_error_to_string(result));
    return VK_NULL_HANDLE;
  }

  VkPhysicalDevice *devices =
      malloc(num_physical_devices * sizeof(VkPhysicalDevice));
  if (!devices) {
    LOG_ERROR("unable to allocate memory for physical device array");
    return VK_NULL_HANDLE;
  }

  result = vkEnumeratePhysicalDevices(instance, &num_physical_devices, devices);
  if (result != VK_SUCCESS) {
    LOG_ERROR("unable to query physical devices: %s",
              vk_error_to_string(result));
    free(devices);
    return VK_NULL_HANDLE;
  }

  i32 best_score = 0;
  VkPhysicalDevice current_best = VK_NULL_HANDLE;
  for (u32 i = 0; i < num_physical_devices; ++i) {
    VkPhysicalDevice device = devices[i];
    i32 score = rate_physical_device(device, surface);
    if (score > best_score) {
      current_best = device;
      best_score = score;
    }
  }

  if (current_best == VK_NULL_HANDLE) {
    LOG_ERROR("no suitable physical devices found");
  } else {
    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(current_best, &properties);
    LOG_INFO("picked physical device: %s (device ID %" PRIu32
             "), score %" PRIi32,
             properties.deviceName, properties.deviceID, best_score);
  }

  free(devices);
  return current_best;
}

bool find_queue_families(VkPhysicalDevice device, VkSurfaceKHR surface,
                         queue_family_indices *indices) {
  u32 num_families;
  vkGetPhysicalDeviceQueueFamilyProperties(device, &num_families, NULL);
  VkQueueFamilyProperties *families =
      malloc(num_families * sizeof(families[0]));
  if (!families) {
    LOG_ERROR("unable to allocate memory for queue family properties structs");
    return false;
  }

  vkGetPhysicalDeviceQueueFamilyProperties(device, &num_families, families);
  for (u32 i = 0; i < num_families; ++i) {
    if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
      indices->graphics = i;
    }

    VkBool32 present_supported;
    vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface,
                                         &present_supported);
    if (present_supported) {
      indices->present = i;
    }
  }

  free(families);
  return true;
}

bool device_init(VkInstance instance, VkPhysicalDevice physical_device,
                 VkSurfaceKHR surface, VkDevice *device) {
  queue_family_indices indices;
  find_queue_families(physical_device, surface, &indices);
  assert(queue_family_indices_complete(&indices));

  u32 num_layers = 0;
  const char **layers = get_validation_layers(&num_layers);
  if (!layers) {
    num_layers = 0;
  }

#define MAX_NUM_INDICES sizeof(queue_family_indices) / sizeof(queue_index)
  u32 unique_indices[MAX_NUM_INDICES], num_unique_indices = 0;
  unique_indices[num_unique_indices++] = indices.graphics;
  if (unique_indices[0] != indices.present) {
    unique_indices[num_unique_indices++] = indices.present;
  }

  VkDeviceQueueCreateInfo queue_info[MAX_NUM_INDICES];
  float queue_priority = 1.0;
  for (u32 i = 0; i < num_unique_indices; ++i) {
    queue_info[i] = (VkDeviceQueueCreateInfo){
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueCount = 1,
        .queueFamilyIndex = unique_indices[i],
        .pQueuePriorities = &queue_priority,
    };
  }

  VkResult result;
  if ((result = vkCreateDevice(
           physical_device,
           &(VkDeviceCreateInfo){
               .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
               .pEnabledFeatures = &(VkPhysicalDeviceFeatures){},
               .ppEnabledLayerNames = layers,
               .enabledLayerCount = num_layers,
               .ppEnabledExtensionNames = required_device_extensions,
               .enabledExtensionCount = num_required_device_extensions,
               .pQueueCreateInfos = queue_info,
               .queueCreateInfoCount = num_unique_indices},
           NULL, device)) != VK_SUCCESS) {
    LOG_ERROR("unable to create device: %s", vk_error_to_string(result));
    free(layers);
    return false;
  }

  free(layers);
  return true;
}

void device_free(VkDevice device) { vkDestroyDevice(device, NULL); }

bool query_swap_chain_support(VkPhysicalDevice device, VkSurfaceKHR surface,
                              swap_chain_support_details *details) {
  VkResult result;
  result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface,
                                                     &details->caps);
  if (result != VK_SUCCESS) {
    LOG_ERROR("unable to query surface capabilities for physical device: %s",
              vk_error_to_string(result));
    return false;
  }

  details->formats = NULL;
  details->present_modes = NULL;
  result = vkGetPhysicalDeviceSurfaceFormatsKHR(
      device, surface, &details->num_formats, details->formats);
  if (result != VK_SUCCESS) {
    LOG_ERROR("unable to query surface formats: %s",
              vk_error_to_string(result));
    goto fail;
  }
  details->formats = malloc(details->num_formats * sizeof(*details->formats));
  if (!details->formats) {
    LOG_ERROR("unable to allocate memory for surface formats: %s",
              vk_error_to_string(result));
    goto fail;
  }

  result = vkGetPhysicalDeviceSurfaceFormatsKHR(
      device, surface, &details->num_formats, details->formats);
  if (result != VK_SUCCESS) {
    LOG_ERROR("unable to query surface formats: %s",
              vk_error_to_string(result));
    goto fail;
  }

  result = vkGetPhysicalDeviceSurfacePresentModesKHR(
      device, surface, &details->num_present_modes, details->present_modes);
  if (result != VK_SUCCESS) {
    LOG_ERROR("unable to query surface present modes: %s",
              vk_error_to_string(result));
    goto fail;
  }
  details->present_modes =
      malloc(details->num_present_modes * sizeof(*details->present_modes));
  if (!details->present_modes) {
    LOG_ERROR("unable to allocate memory for surface present modes: %s",
              vk_error_to_string(result));
    goto fail;
  }

  result = vkGetPhysicalDeviceSurfacePresentModesKHR(
      device, surface, &details->num_present_modes, details->present_modes);
  if (result != VK_SUCCESS) {
    LOG_ERROR("unable to query surface present modes: %s",
              vk_error_to_string(result));
    goto fail;
  }
  return true;
fail:
  free(details->formats);
  free(details->present_modes);
  return false;
}

void swap_chain_support_details_free(swap_chain_support_details *details) {
  free(details->formats);
  free(details->present_modes);
}
