#include "debug_msg.h"
#include "vk_utils.h"
#include <logger.h>
#include <vulkan/vulkan_core.h>

static VKAPI_ATTR VkBool32 VKAPI_CALL
debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
               VkDebugUtilsMessageTypeFlagsEXT messageTypes,
               const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
               void *pUserData) {
  (void)messageTypes;
  (void)pUserData;
  LogLevel level;
  switch (messageSeverity) {
  case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:
    level = LogLevel_DEBUG;
    break;
  case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
    level = LogLevel_INFO;
    break;
  case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
    level = LogLevel_WARN;
    break;
  case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
    level = LogLevel_ERROR;
    break;
  default:
    LOG_ERROR("invalid message severity: %d", messageSeverity);
    level = LogLevel_ERROR;
  }

  logger_log(level, __FILENAME__, __LINE__, "%s", pCallbackData->pMessage);
  return VK_FALSE;
}

static VkDebugUtilsMessageSeverityFlagsEXT log_severity_flag() {
  VkDebugUtilsMessageSeverityFlagsEXT flags = 0;
  if (logger_isEnabled(LogLevel_ERROR)) {
    flags |= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
  }
  if (logger_isEnabled(LogLevel_WARN)) {
    flags |= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT;
  }
  if (logger_isEnabled(LogLevel_INFO)) {
    flags |= VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT;
  }
  if (logger_isEnabled(LogLevel_DEBUG)) {
    flags |= VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT;
  }

  return flags;
}

static const VkDebugUtilsMessageTypeFlagsEXT all_message_types =
    VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
    VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
    VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT |
    VK_DEBUG_UTILS_MESSAGE_TYPE_DEVICE_ADDRESS_BINDING_BIT_EXT;

void debug_msg_create_info(VkDebugUtilsMessengerCreateInfoEXT *info) {
  memset(info, 0, sizeof *info);
  info->sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
  info->messageSeverity = log_severity_flag();
  info->messageType = all_message_types;
  info->pfnUserCallback = debug_callback;
}

bool debug_msg_init(VkInstance inst, debug_messenger *msg) {
  if (!debug) {
    msg->debug_messenger = VK_NULL_HANDLE;
    return true;
  }

  PFN_vkCreateDebugUtilsMessengerEXT func =
      (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
          inst, "vkCreateDebugUtilsMessengerEXT");
  VkResult result;
  if (func) {
    VkDebugUtilsMessengerCreateInfoEXT info;
    debug_msg_create_info(&info);
    if ((result = func(inst, &info, NULL, &msg->debug_messenger)) ==
        VK_SUCCESS) {
      return true;
    }

    LOG_WARN("unable to create VK_EXT_debug_utils debug messenger: %s",
             vk_error_to_string(result));
  } else {
    LOG_WARN("extension VK_EXT_debug_utils not supported");
  }

  msg->debug_messenger = VK_NULL_HANDLE;
  return false;
}

void debug_msg_free(VkInstance inst, debug_messenger msg) {
  if (msg.debug_messenger == VK_NULL_HANDLE) {
    return;
  }

  PFN_vkDestroyDebugUtilsMessengerEXT func =
      (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
          inst, "vkDestroyDebugUtilsMessengerEXT");
  if (func) {
    func(inst, msg.debug_messenger, NULL);
  }
}
