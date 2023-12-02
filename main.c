#include "debug_msg.h"
#include "device.h"
#include "instance.h"
#include "window.h"
#include <GLFW/glfw3.h>
#include <logger.h>
#include <stdio.h>
#include <stdlib.h>
#include <vulkan/vk_enum_string_helper.h>
#include <vulkan/vulkan_core.h>

static void key_callback(GLFWwindow *w, int key, int scancode, int action,
                         int mods) {
  if (key == GLFW_KEY_Q && action == GLFW_PRESS) {
    glfwSetWindowShouldClose(w, true);
  }
}

typedef struct {
  window w;
  VkInstance instance;
  debug_messenger debug_msg;
  VkPhysicalDevice physical_device;
  VkDevice device;
} app;

static bool app_init(app *a) {
  if (!window_init(&a->w, 1280, 720, "vulkan")) {
    LOG_ERROR("error: unable to open window");
    return false;
  }

  glfwSetKeyCallback(a->w.window, key_callback);

  if (!vk_instance_init(&a->instance)) {
    LOG_ERROR("unable to initialize vulkan instance");
    goto fail_vk_instance;
  }

  if (!debug_msg_init(a->instance, &a->debug_msg)) {
    LOG_WARN("unable to initialize debug messenger");
  }

  if ((a->physical_device = physical_device_pick(a->instance)) ==
      VK_NULL_HANDLE) {
    LOG_ERROR("unable to pick physical device");
    goto fail_vk_physical_device;
  }

  if(!device_init(a->instance, a->physical_device, &a->device)) {
    LOG_ERROR("unable to create vulkan device");
    goto fail_vk_device;
  }

  return true;

fail_vk_device:
fail_vk_physical_device:
  debug_msg_free(a->instance, a->debug_msg);
fail_vk_instance:
  window_free(&a->w);
  return false;
}

static void app_free(app *a) {
  device_free(a->device);
  debug_msg_free(a->instance, a->debug_msg);
  vk_instance_free(a->instance);
  window_free(&a->w);
}

static void app_loop(app *a) {
  while (!window_should_close(&a->w)) {
    window_poll_events();
  }
}

int main() {
  logger_initConsoleLogger(stderr);
  logger_setLevel(LogLevel_TRACE);

  app a;
  if (!app_init(&a)) {
    LOG_ERROR("error: unable to initialize app");
    return 1;
  }

  app_loop(&a);
  app_free(&a);
}
