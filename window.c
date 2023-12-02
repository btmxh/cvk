#include "window.h"
#include "device.h"
#include "vk_utils.h"
#include <GLFW/glfw3.h>
#include <assert.h>
#include <logger.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <vulkan/vulkan_core.h>

void glfw_callback(int error, const char *desc) {
  LOG_ERROR("GLFW error callback: %d (%s)", error, desc);
}

static void set_glfw_callback() {
  static bool set = false;
  if (set) {
    return;
  }

  glfwSetErrorCallback(glfw_callback);
  set = true;
}

static i32 glfw_ref_count = 0;
bool window_init(window *w, i32 width, i32 height, const char *title) {
  assert(glfw_ref_count >= 0);
  if (glfw_ref_count++ == 0) {
    set_glfw_callback();
    if (!glfwInit()) {
      LOG_ERROR("error: unable to initialize GLFW");
      return false;
    }
  }

  glfwDefaultWindowHints();
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  w->window = glfwCreateWindow(width, height, title, NULL, NULL);
  return w->window != NULL;
}

void window_free(window *w) {
  glfwDestroyWindow(w->window);

  assert(glfw_ref_count > 0);
  if (--glfw_ref_count == 0) {
    glfwTerminate();
  }
}

bool window_should_close(window *w) { return glfwWindowShouldClose(w->window); }

void window_poll_events() { glfwPollEvents(); }

bool surface_init(window *w, VkInstance inst, VkSurfaceKHR *surface) {
  return glfwCreateWindowSurface(inst, w->window, NULL, surface) == VK_SUCCESS;
}

void surface_free(VkInstance inst, VkSurfaceKHR surface) {
  vkDestroySurfaceKHR(inst, surface, NULL);
}

bool swapchain_init(window *w, VkPhysicalDevice physical_device,
                    VkDevice device, VkSurfaceKHR surface,
                    VkSwapchainKHR old_swapchain, VkSwapchainKHR *swapchain,
                    VkSurfaceFormatKHR *format, VkExtent2D *extent) {
  swap_chain_support_details details;
  if (!query_swap_chain_support(physical_device, surface, &details)) {
    LOG_ERROR("error querying swapchain support details");
    goto fail_details;
  }

  queue_family_indices indices;
  if (!find_queue_families(physical_device, surface, &indices)) {
    LOG_ERROR("error querying queue family indices");
    goto fail_indices;
  }

  u32 indices_ptr[] = {indices.graphics, indices.present};

  u32 image_count = details.caps.minImageCount + 1;
  if (image_count < details.caps.maxImageCount &&
      details.caps.maxImageCount > 0) {
    image_count = details.caps.maxImageCount;
  }

  *format = details.formats[0];
  for (u32 i = 0; i < details.num_formats; ++i) {
    VkSurfaceFormatKHR cur_format = details.formats[i];
    if (cur_format.format == VK_FORMAT_B8G8R8A8_SRGB &&
        cur_format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
      *format = cur_format;
    }
  }

  VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;
  for (u32 i = 0; i < details.num_present_modes; ++i) {
    if (details.present_modes[i] == VK_PRESENT_MODE_MAILBOX_KHR) {
      present_mode = details.present_modes[i];
    }
  }

  *extent = details.caps.currentExtent;
  if (extent->width == UINT32_MAX) {
    int width, height;
    glfwGetFramebufferSize(w->window, &width, &height);
    extent->width = width;
    extent->height = height;

#define CLAMP(value, min, max)                                                 \
  do {                                                                         \
    if (value < min) {                                                         \
      value = min;                                                             \
    }                                                                          \
    if (value > max) {                                                         \
      value = max;                                                             \
    }                                                                          \
  } while (false);
  }

  CLAMP(extent->width, details.caps.minImageExtent.width,
        details.caps.maxImageExtent.width);
  CLAMP(extent->height, details.caps.minImageExtent.height,
        details.caps.maxImageExtent.height);

  VkResult result;
  if ((result = vkCreateSwapchainKHR(
           device,
           &(VkSwapchainCreateInfoKHR){
               .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
               .surface = surface,
               .clipped = VK_TRUE,
               .presentMode = present_mode,
               .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
               .imageExtent = *extent,
               .imageFormat = format->format,
               .oldSwapchain = old_swapchain,
               .preTransform = details.caps.currentTransform,
               .minImageCount = image_count,
               .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
               .imageColorSpace = format->colorSpace,
               .imageArrayLayers = 1,
               .imageSharingMode = indices.present == indices.graphics
                                       ? VK_SHARING_MODE_EXCLUSIVE
                                       : VK_SHARING_MODE_CONCURRENT,
               .queueFamilyIndexCount =
                   indices.present == indices.graphics ? 0 : 2,
               .pQueueFamilyIndices =
                   indices.present == indices.graphics ? NULL : indices_ptr,
           },
           NULL, swapchain)) != VK_SUCCESS) {
    LOG_ERROR("unable to create swapchain: %s", vk_error_to_string(result));
  }

  swap_chain_support_details_free(&details);
  return true;
fail_indices:
    swap_chain_support_details_free(&details);
fail_details:
    return false;
}

void swapchain_free(VkDevice device, VkSwapchainKHR swapchain) {
  vkDestroySwapchainKHR(device, swapchain, NULL);
}
VkImage *swapchain_get_images(VkDevice device, VkSwapchainKHR swapchain,
                              u32 *num_images) {
  VkResult result;
  result = vkGetSwapchainImagesKHR(device, swapchain, num_images, NULL);
  if (result != VK_SUCCESS) {
    LOG_ERROR("unable to retrieve swapchain images: %s",
              vk_error_to_string(result));
    return NULL;
  }
  VkImage *images = malloc(*num_images * sizeof(VkImage));
  if (!images) {
    LOG_ERROR("unable to allocate memory for swapchain image handles");
    return NULL;
  }

  result = vkGetSwapchainImagesKHR(device, swapchain, num_images, images);
  if (result != VK_SUCCESS) {
    LOG_ERROR("unable to retrieve swapchain images: %s",
              vk_error_to_string(result));
    free(images);
    return NULL;
  }

  return images;
}

bool swapchain_image_views_init(VkDevice device, VkImage *images,
                                u32 num_images, VkFormat format,
                                VkImageView **views) {
  bool allocated = false;
  if (!*views) {
    *views = malloc(num_images * sizeof(VkImageView));
    if (!*views) {
      LOG_ERROR("unable to allocate memory for image views");
      return false;
    }

    allocated = true;
  }

  u32 initialized_views = 0;
  VkResult result;
  while (initialized_views < num_images) {
    if ((result = vkCreateImageView(
             device,
             &(VkImageViewCreateInfo){
                 .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                 .image = images[initialized_views],
                 .viewType = VK_IMAGE_VIEW_TYPE_2D,
                 .format = format,
                 .components =
                     {
                         .r = VK_COMPONENT_SWIZZLE_IDENTITY,
                         .g = VK_COMPONENT_SWIZZLE_IDENTITY,
                         .b = VK_COMPONENT_SWIZZLE_IDENTITY,
                         .a = VK_COMPONENT_SWIZZLE_IDENTITY,
                     },
                 .subresourceRange =
                     {
                         .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                         .baseMipLevel = 0,
                         .levelCount = 1,
                         .baseArrayLayer = 0,
                         .layerCount = 1,
                     }},
             NULL, &(*views)[initialized_views])) != VK_SUCCESS) {
      LOG_ERROR("unable to create %" PRIu32 "-th image view: %s",
                initialized_views, vk_error_to_string(result));
      for (u32 i = 0; i < initialized_views; ++i) {
        vkDestroyImageView(device, (*views)[i], NULL);
      }

      if (allocated) {
        free(*views);
      }
      return false;
    }
    ++initialized_views;
  }

  return true;
}
void swapchain_image_views_destroy(VkDevice device, VkImageView *views,
                                   u32 num_images) {
  for (u32 i = 0; i < num_images; ++i) {
    vkDestroyImageView(device, views[i], NULL);
  }

  free(views);
}
