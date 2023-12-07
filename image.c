#include "image.h"

#include "device.h"
#include "memory.h"
#include "vk_utils.h"
#include <alloca.h>
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
