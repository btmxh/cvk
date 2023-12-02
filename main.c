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
  VkSurfaceKHR surface;
  VkPhysicalDevice physical_device;
  VkDevice device;

  // swapchain derived
  VkSwapchainKHR swapchain;
  VkSurfaceFormatKHR format;
  VkExtent2D extent;
  VkImage *images;
  VkImageView *image_views;
  u32 num_images;
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

  if (!surface_init(&a->w, a->instance, &a->surface)) {
    LOG_ERROR("unable to initialize window surface");
    goto fail_vk_surface;
  }

  if ((a->physical_device = physical_device_pick(a->instance, a->surface)) ==
      VK_NULL_HANDLE) {
    LOG_ERROR("unable to pick physical device");
    goto fail_vk_physical_device;
  }

  if (!device_init(a->instance, a->physical_device, a->surface, &a->device)) {
    LOG_ERROR("unable to create vulkan device");
    goto fail_vk_device;
  }

  if (!swapchain_init(&a->w, a->physical_device, a->device, a->surface,
                      VK_NULL_HANDLE, &a->swapchain, &a->format, &a->extent)) {
    LOG_ERROR("unable to create vulkan swapchain");
    goto fail_vk_swapchain;
  }

  if (!(a->images =
            swapchain_get_images(a->device, a->swapchain, &a->num_images))) {
    LOG_ERROR("unable to get vulkan swapchain images");
    goto fail_vk_swapchain_images;
  }

  a->image_views = NULL;
  if (!swapchain_image_views_init(a->device, a->images, a->num_images, a->format.format,
                        &a->image_views)) {
    LOG_ERROR("unable to create image views for swapchain images");
    goto fail_vk_swapchain_image_views;
  }

  return true;

fail_vk_swapchain_image_views:
  free(a->images);
fail_vk_swapchain_images:
  swapchain_free(a->device, a->swapchain);
fail_vk_swapchain:
  device_free(a->device);
fail_vk_device:
fail_vk_physical_device:
  surface_free(a->instance, a->surface);
fail_vk_surface:
  debug_msg_free(a->instance, a->debug_msg);
fail_vk_instance:
  window_free(&a->w);
  return false;
}

static void app_free(app *a) {
  swapchain_image_views_destroy(a->device, a->image_views, a->num_images);
  free(a->images);
  swapchain_free(a->device, a->swapchain);
  device_free(a->device);
  surface_free(a->instance, a->surface);
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
