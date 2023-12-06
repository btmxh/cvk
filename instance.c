#include "instance.h"

#include "debug_msg.h"
#include "vk_utils.h"
#include <logger.h>
#include <stdlib.h>
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

static const char **get_extensions(u32 *num_extensions) {
  static const char *debug_extensions[] = {
      VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
  };

  static const u32 num_debug_extensions =
      sizeof(debug_extensions) / sizeof(debug_extensions[0]);

  VkResult result;
  u32 num_glfw_extensions;
  const char **glfw_extensions =
      glfwGetRequiredInstanceExtensions(&num_glfw_extensions);

  u32 num_supported_extensions;
  result = vkEnumerateInstanceExtensionProperties(
      NULL, &num_supported_extensions, NULL);
  if (result != VK_SUCCESS) {
    LOG_ERROR("unable to query supported extensions: %s",
              vk_error_to_string(result));
    return NULL;
  }
  VkExtensionProperties *supported_extensions =
      malloc(num_supported_extensions * sizeof(*supported_extensions));
  if (!supported_extensions) {
    LOG_ERROR("unable to allocate memory for extension properties structs");
    return NULL;
  }

  result = vkEnumerateInstanceExtensionProperties(
      NULL, &num_supported_extensions, supported_extensions);
  if (result != VK_SUCCESS) {
    LOG_ERROR("unable to query supported extensions: %s",
              vk_error_to_string(result));
    free(supported_extensions);
    return NULL;
  }

  LOG_DEBUG("supported extensions (total %" PRIu32 "):",
            num_supported_extensions);
  for (u32 i = 0; i < num_supported_extensions; ++i) {
    LOG_DEBUG("\t%s (version " PRIvkVer ")",
              supported_extensions[i].extensionName,
              PRIvkVerArg(supported_extensions[i].specVersion));
  }

  u32 max_extensions = num_debug_extensions + num_glfw_extensions;
  const char **extensions = malloc(max_extensions * sizeof(extensions[0]));
  *num_extensions = num_glfw_extensions;
  if (extensions) {
    memcpy(extensions, glfw_extensions,
           num_glfw_extensions * sizeof(glfw_extensions[0]));
    if (debug) {
      for (u32 i = 0; i < num_debug_extensions; ++i) {
        const char *name = debug_extensions[i];
        bool supported = false;
        for (u32 j = 0; j < *num_extensions; ++j) {
          if (strcmp(extensions[j], name) == 0) {
            goto next;
          }
        }

        for (u32 j = 0; j < num_supported_extensions; ++j) {
          if (strcmp(supported_extensions[j].extensionName, name) == 0) {
            supported = true;
            break;
          }
        }

        if (!supported) {
          LOG_WARN("requested extension %s not supported", name);
          continue;
        }

        extensions[(*num_extensions)++] = name;
      next:;
      }
    }
  }

  LOG_DEBUG("enabled extensions (total %" PRIu32 "):", *num_extensions);
  for (u32 i = 0; i < *num_extensions; ++i) {
    LOG_DEBUG("\t%s", extensions[i]);
  }

  free(supported_extensions);
  return extensions;
}

const char **get_validation_layers(u32 *num_layers) {
  if(!debug) {
    *num_layers = 0;
    return NULL;
  }

  static const char *requested_validation_layers[] = {
      "VK_LAYER_KHRONOS_validation",
  };

  VkResult result;

  *num_layers = 0;
  i32 num_requested_layers = sizeof(requested_validation_layers) /
                             sizeof(requested_validation_layers[0]);

  u32 num_supported_layers;
  result = vkEnumerateInstanceLayerProperties(&num_supported_layers, NULL);
  if (result != VK_SUCCESS) {
    LOG_ERROR("unable to query supported layers: %s",
              vk_error_to_string(result));
    return NULL;
  }

  VkLayerProperties *supported_layers =
      malloc(num_supported_layers * sizeof(*supported_layers));
  if (!supported_layers) {
    LOG_ERROR("unable to allocate memory for supported layer properties structs");
    return NULL;
  }

  result = vkEnumerateInstanceLayerProperties(&num_supported_layers,
                                              supported_layers);
  if (result != VK_SUCCESS) {
    LOG_ERROR("unable to query supported layers: %s",
              vk_error_to_string(result));
    free(supported_layers);
    return NULL;
  }

  const char **layers = malloc(sizeof(requested_validation_layers));
  if (!layers) {
    LOG_ERROR("unable to allocate memory for enabled layer names array");
    free(supported_layers);
    return NULL;
  }

  LOG_DEBUG("supported layers (total %" PRIu32 "):", num_supported_layers);
  for (u32 i = 0; i < num_supported_layers; ++i) {
    LOG_DEBUG("\t%s (spec " PRIvkVer ", impl " PRIvkVer ")",
              supported_layers[i].layerName,
              PRIvkVerArg(supported_layers[i].specVersion),
              PRIvkVerArg(supported_layers[i].implementationVersion));
    LOG_DEBUG("\t\t%s", supported_layers[i].description);
  }

  for (i32 i = 0; i < num_requested_layers; ++i) {
    for (i32 j = 0; j < (i32) num_supported_layers; ++j) {
      if (strcmp(supported_layers[j].layerName,
                 requested_validation_layers[i]) == 0) {
        layers[(*num_layers)++] = requested_validation_layers[i];
        break;
      }
    }
  }

  LOG_DEBUG("enabled layers (total %" PRIu32 "):", *num_layers);
  for (u32 i = 0; i < *num_layers; ++i) {
    LOG_DEBUG("\t%s", layers[i]);
  }

  free(supported_layers);
  return layers;
}

bool vk_instance_init(VkInstance *inst) {
  VkResult result;
  u32 num_extensions, num_layers = 0;
  const char **extensions = get_extensions(&num_extensions),
             **layers = get_validation_layers(&num_layers);

  if (!extensions) {
    LOG_ERROR("error retrieving requested extensions");
    goto fail;
  }

  if (!layers) {
    LOG_ERROR("error retrieving requested layers");
    goto fail;
  }

  VkDebugUtilsMessengerCreateInfoEXT debug_msg_info;
  if (debug) {
    debug_msg_create_info(&debug_msg_info);
  }

  if ((result = vkCreateInstance(
           &(VkInstanceCreateInfo){
               .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
               .pNext = debug ? &debug_msg_info : NULL,
               .pApplicationInfo =
                   &(VkApplicationInfo){
                       .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                       .apiVersion = VK_API_VERSION_1_0,
                       .pEngineName = "No Engine",
                       .engineVersion = VK_MAKE_VERSION(1, 0, 0),
                       .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
                   },
               .enabledLayerCount = num_layers,
               .ppEnabledLayerNames = layers,
               .enabledExtensionCount = num_extensions,
               .ppEnabledExtensionNames = extensions},
           NULL, inst)) != VK_SUCCESS) {
    LOG_ERROR("error: unable to create vulkan instance: %s",
              vk_error_to_string(result));
    goto fail;
  }

  free(extensions);
  free(layers);
  return true;
fail:
  free(extensions);
  free(layers);
  return false;
}

void vk_instance_free(VkInstance inst) { vkDestroyInstance(inst, NULL); }
