#include "device.h"
#include "vk_utils.h"

#include "instance.h"
#include <logger.h>
#include <stdlib.h>
#include <vulkan/vulkan_core.h>

static void queue_family_indices_init(queue_family_indices *indices) {
  indices->graphics = QUEUE_INDEX_NONE;
}

static bool queue_family_indices_complete(const queue_family_indices *indices) {
  return indices->graphics >= 0;
}

static i32 rate_physical_device(VkPhysicalDevice device) {
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
  find_queue_families(device, &indices);
  if (!queue_family_indices_complete(&indices)) {
    FAIL("physical device not having support for necessary queue families");
  }

  if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
    INCREASE("physical device is discrete GPU", 1000);
  }

  INCREASE("increase score by maximum supported image size",
           properties.limits.maxImageDimension2D);

  return score;
}

VkPhysicalDevice physical_device_pick(VkInstance instance) {
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
    i32 score = rate_physical_device(device);
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

bool find_queue_families(VkPhysicalDevice device,
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
  }

  free(families);
  return true;
}

bool device_init(VkInstance instance, VkPhysicalDevice physical_device,
                 VkDevice *device) {
  queue_family_indices indices;
  find_queue_families(physical_device, &indices);

  u32 num_layers = 0;
  const char **layers = get_validation_layers(&num_layers);
  if (!layers) {
    num_layers = 0;
  }

  VkResult result;
  if ((result = vkCreateDevice(
           physical_device,
           &(VkDeviceCreateInfo){
               .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
               .pEnabledFeatures = &(VkPhysicalDeviceFeatures){},
               .ppEnabledLayerNames = layers,
               .enabledLayerCount = num_layers,
               .enabledExtensionCount = 0,
               .pQueueCreateInfos =
                   &(VkDeviceQueueCreateInfo){
                       .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                       .queueCount = 1,
                       .queueFamilyIndex = indices.graphics,
                       .pQueuePriorities = &(float){1.0f},
                   },
               .queueCreateInfoCount = 1},
           NULL, device)) != VK_SUCCESS) {
    LOG_ERROR("unable to create device: %s", vk_error_to_string(result));
    free(layers);
    return false;
  }

  free(layers);
  return true;
}

void device_free(VkDevice device) { vkDestroyDevice(device, NULL); }
