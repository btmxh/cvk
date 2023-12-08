#include "image.h"

#include "device.h"
#include "memory.h"
#include "vk_utils.h"
#include <logger.h>
#include <stb/stb_image.h>
#include <vulkan/vulkan_core.h>

bool image_load_from_file(const transfer_context *tctx, const char *path,
                          VkImage *image, VmaAllocation *allocation,
                          VkImageView *image_view) {
  int width, height, num_channels;
  stbi_uc *data = stbi_load(path, &width, &height, &num_channels, STBI_default);
  if (!data) {
    LOG_ERROR("unable to load image data from file: %s", stbi_failure_reason());
    goto fail_stbi_load;
  }

  VkFormat format;
  switch (num_channels) {
  case STBI_grey:
    format = VK_FORMAT_R8_SRGB;
    break;
  case STBI_grey_alpha:
    format = VK_FORMAT_R8G8_SRGB;
    break;
  case STBI_rgb:
    format = VK_FORMAT_R8G8B8_SRGB;
    break;
  case STBI_rgb_alpha:
    format = VK_FORMAT_R8G8B8A8_SRGB;
    break;
  default:
    LOG_ERROR("invalid num_channels value: %d", num_channels);
    goto fail_stbi_load;
  }

  VkResult result;
  i32 num_unique_indices;
  VkSharingMode sharing_mode;
  u32 *unique_queue_indices = remove_duplicate_and_invalid_indices(
      (u32[]){tctx->indices.graphics, tctx->indices.transfer}, 2,
      &num_unique_indices, &sharing_mode);
  if ((result = vmaCreateImage(tctx->vma,
                               &(VkImageCreateInfo){
                                   .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                                   .extent = {width, height, 1},
                                   .format = format,
                                   .usage = VK_IMAGE_USAGE_SAMPLED_BIT |
                                            VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                                   .tiling = VK_IMAGE_TILING_OPTIMAL,
                                   .samples = VK_SAMPLE_COUNT_1_BIT,
                                   .sharingMode = sharing_mode,
                                   .mipLevels = 1,
                                   .arrayLayers = 1,
                                   .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                                   .pQueueFamilyIndices = unique_queue_indices,
                                   .queueFamilyIndexCount = num_unique_indices,
                                   .imageType = VK_IMAGE_TYPE_2D,
                               },
                               &(VmaAllocationCreateInfo){
                                   .usage = VMA_MEMORY_USAGE_AUTO,
                               },
                               image, allocation, NULL)) != VK_SUCCESS) {
    LOG_ERROR("unable to create image");
    goto fail_image;
  }

  if (!transfer_context_stage_linear_data_to_2d_image(
          tctx, *image,
          (VkRect2D){
              .offset = {0, 0},
              .extent = {width, height},
          },
          data, format, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)) {
    LOG_ERROR("unable to stage image data to image memory");
    goto fail_stage;
  }

  if (image_view) {
    VkComponentMapping swizzle;
    switch (num_channels) {
    case STBI_grey:
      swizzle.r = VK_COMPONENT_SWIZZLE_R;
      swizzle.g = VK_COMPONENT_SWIZZLE_R;
      swizzle.b = VK_COMPONENT_SWIZZLE_R;
      swizzle.a = VK_COMPONENT_SWIZZLE_ONE;
      break;
    case STBI_grey_alpha:
      swizzle.r = VK_COMPONENT_SWIZZLE_R;
      swizzle.g = VK_COMPONENT_SWIZZLE_R;
      swizzle.b = VK_COMPONENT_SWIZZLE_R;
      swizzle.a = VK_COMPONENT_SWIZZLE_G;
      break;
    case STBI_rgb:
      swizzle.r = VK_COMPONENT_SWIZZLE_R;
      swizzle.g = VK_COMPONENT_SWIZZLE_G;
      swizzle.b = VK_COMPONENT_SWIZZLE_B;
      swizzle.a = VK_COMPONENT_SWIZZLE_ONE;
      break;
    case STBI_rgb_alpha:
      swizzle.r = VK_COMPONENT_SWIZZLE_R;
      swizzle.g = VK_COMPONENT_SWIZZLE_G;
      swizzle.b = VK_COMPONENT_SWIZZLE_B;
      swizzle.a = VK_COMPONENT_SWIZZLE_A;
      break;
    default:
      LOG_ERROR("invalid num_channels value: %d", num_channels);
      goto fail_stbi_load;
    }
    if ((result = vkCreateImageView(
             tctx->device,
             &(VkImageViewCreateInfo){
                 .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                 .format = format,
                 .image = *image,
                 .subresourceRange =
                     {
                         .baseMipLevel = 0,
                         .baseArrayLayer = 0,
                         .layerCount = 1,
                         .levelCount = 1,
                         .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                     },
                 .viewType = VK_IMAGE_VIEW_TYPE_2D,
                 .components = swizzle,
             },
             NULL, image_view)) != VK_SUCCESS) {
      LOG_ERROR("unable to create image view: %s", vk_error_to_string(result));
      goto fail_image_view;
    }
  }

  stbi_image_free(data);
  return true;

fail_image_view:
fail_stage:
  vmaDestroyImage(tctx->vma, *image, *allocation);
fail_image:
  stbi_image_free(data);
fail_stbi_load:
  return false;
}

void image_free(const transfer_context *c, VkImage image,
                VmaAllocation allocation, VkImageView image_view) {
  vmaDestroyImage(c->vma, image, allocation);
  if (image_view != VK_NULL_HANDLE) {
    vkDestroyImageView(c->device, image_view, NULL);
  }
}

bool sampler_create(VkPhysicalDevice physical_device, VkDevice device,
                    VkSampler *sampler) {
  VkPhysicalDeviceProperties properties;
  vkGetPhysicalDeviceProperties(physical_device, &properties);
  VkResult result;
  if ((result = vkCreateSampler(
           device,
           &(VkSamplerCreateInfo){
               .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
               .minFilter = VK_FILTER_LINEAR,
               .magFilter = VK_FILTER_LINEAR,
               .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
               .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
               .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
               .anisotropyEnable = VK_TRUE,
               .maxAnisotropy = properties.limits.maxSamplerAnisotropy,
               .borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
               .unnormalizedCoordinates = VK_FALSE,
               .compareEnable = VK_FALSE,
               .compareOp = VK_COMPARE_OP_ALWAYS,
               .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
               .mipLodBias = 0.0,
               .minLod = 0.0,
               .maxLod = 0.0,
           },
           NULL, sampler)) != VK_SUCCESS) {
    LOG_ERROR("unable to create texture sampler: %s",
              vk_error_to_string(result));
    return false;
  }

  return true;
}

void sampler_free(VkDevice device, VkSampler sampler) {
  vkDestroySampler(device, sampler, NULL);
}

static VkFormat pick_depth_format(VkPhysicalDevice device, VkImageTiling tiling,
                                  VkFormatFeatureFlags features) {
  VkFormat formats[] = {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT,
                        VK_FORMAT_D24_UNORM_S8_UINT};
  for (i32 i = 0; i < (i32) (sizeof(formats) / sizeof(formats[0])); ++i) {
    VkFormat format = formats[i];
    VkFormatProperties properties;
    vkGetPhysicalDeviceFormatProperties(device, format, &properties);

    if (tiling == VK_IMAGE_TILING_LINEAR &&
        features == (features & properties.linearTilingFeatures)) {
      return format;
    }
    if (tiling == VK_IMAGE_TILING_OPTIMAL &&
        features == (features & properties.optimalTilingFeatures)) {
      return format;
    }
  }

  return VK_FORMAT_MAX_ENUM;
}

bool image_init_depth_buffer(VkPhysicalDevice physical_device,
                             const transfer_context *tctx, VkExtent2D size,
                             VkImage *image, VmaAllocation *image_allocation,
                             VkFormat* depth_format, VkImageView *view) {
  VkResult result;
  VkFormat format =
      pick_depth_format(physical_device, VK_IMAGE_TILING_OPTIMAL,
                        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
  i32 num_unique_indices;
  VkSharingMode sharing_mode;
  u32 *unique_queue_indices = remove_duplicate_and_invalid_indices(
      (u32[]){tctx->indices.graphics, tctx->indices.transfer}, 2,
      &num_unique_indices, &sharing_mode);
  if (format == VK_FORMAT_MAX_ENUM) {
    LOG_ERROR("unable to find depth format");
    goto fail_format;
  }

  if ((result = vmaCreateImage(
           tctx->vma,
           &(VkImageCreateInfo){
               .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
               .extent = {size.width, size.height, 1},
               .format = format,
               .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
               .tiling = VK_IMAGE_TILING_OPTIMAL,
               .samples = VK_SAMPLE_COUNT_1_BIT,
               .sharingMode = sharing_mode,
               .mipLevels = 1,
               .arrayLayers = 1,
               .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
               .pQueueFamilyIndices = unique_queue_indices,
               .queueFamilyIndexCount = num_unique_indices,
               .imageType = VK_IMAGE_TYPE_2D,
           },
           &(VmaAllocationCreateInfo){
               .usage = VMA_MEMORY_USAGE_AUTO,
           },
           image, image_allocation, NULL)) != VK_SUCCESS) {
    LOG_ERROR("unable to create image: %s", vk_error_to_string(result));
    goto fail_image;
  }

  if ((result = vkCreateImageView(
           tctx->device,
           &(VkImageViewCreateInfo){
               .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
               .image = *image,
               .format = format,
               .viewType = VK_IMAGE_VIEW_TYPE_2D,
               .components =
                   {
                       .r = VK_COMPONENT_SWIZZLE_R,
                       .g = VK_COMPONENT_SWIZZLE_G,
                       .b = VK_COMPONENT_SWIZZLE_B,
                       .a = VK_COMPONENT_SWIZZLE_A,
                   },
               .subresourceRange =
                   {
                       .baseMipLevel = 0,
                       .baseArrayLayer = 0,
                       .layerCount = 1,
                       .levelCount = 1,
                       .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
                   }},
           NULL, view)) != VK_SUCCESS) {
    LOG_ERROR("unable to create image view for depth buffer: %s",
              vk_error_to_string(result));
    goto fail_image_view;
  }

  if(depth_format) {
    *depth_format = format;
  }

  return true;

fail_image_view:
  vmaDestroyImage(tctx->vma, *image, *image_allocation);
fail_image:
fail_format:
  return false;
}

void image_free_depth_buffer(const transfer_context *tctx, VkImage image,
                             VmaAllocation allocation, VkImageView view) {
  if (view != VK_NULL_HANDLE) {
    vkDestroyImageView(tctx->device, view, NULL);
  }
  vmaDestroyImage(tctx->vma, image, allocation);
}
