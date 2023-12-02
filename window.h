#include "types.h"
#include <stdbool.h>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

typedef struct {
  GLFWwindow *window;
} window;

bool window_init(window *w, i32 width, i32 height, const char *title);
void window_free(window *w);

bool window_should_close(window *w);
void window_poll_events();

bool surface_init(window *w, VkInstance inst, VkSurfaceKHR *surface);
void surface_free(VkInstance inst, VkSurfaceKHR surface);

bool swapchain_init(window *w, VkPhysicalDevice physical_device,
                    VkDevice device, VkSurfaceKHR surface,
                    VkSwapchainKHR old_swapchain, VkSwapchainKHR *swapchain,
                    VkSurfaceFormatKHR *format, VkExtent2D *extent);
void swapchain_free(VkDevice device, VkSwapchainKHR swapchain);
VkImage *swapchain_get_images(VkDevice device, VkSwapchainKHR swapchain,
                              u32 *num_images);
bool swapchain_image_views_init(VkDevice device, VkImage *images, u32 num_images,
                      VkFormat format, VkImageView **views);
void swapchain_image_views_destroy(VkDevice device, VkImageView* views, u32 num_images);
