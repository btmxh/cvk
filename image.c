#include "image.h"

#include "device.h"
#include "memory.h"
#include "vk_utils.h"
#include <assert.h>
#include <logger.h>
#include <math.h>
#include <stb/stb_image.h>
#include <vulkan/vulkan_core.h>

static bool image_generate_mipmap(const transfer_context *tctx,
                                  const mipmap_context *m, VkImage image,
                                  VkExtent2D extent,
                                  VkImageLayout transition_layout) {
  VkResult result;
  if ((result = vkResetCommandPool(tctx->device, m->blit_command_pool, 0)) !=
      VK_SUCCESS) {
    LOG_ERROR("unable to reset blit command pool: %s",
              vk_error_to_string(result));
    return false;
  }
  if ((result = vkBeginCommandBuffer(
           m->blit_command_buffer,
           &(VkCommandBufferBeginInfo){
               .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
               .pInheritanceInfo = NULL,
               .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
           })) != VK_SUCCESS) {
    LOG_ERROR("unable to begin recording command buffer: %s",
              vk_error_to_string(result));
  }
  i32 src_width = extent.width, src_height = extent.height;
  for (i32 i = 0; i < m->mip_levels - 1; ++i) {
    vkCmdPipelineBarrier(m->blit_command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1,
                         &(VkImageMemoryBarrier){
                             .image = image,
                             .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                             .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                             .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                             .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                             .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                             .subresourceRange =
                                 {
                                     .baseMipLevel = i,
                                     .levelCount = 1,
                                     .baseArrayLayer = 0,
                                     .layerCount = 1,
                                     .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                 },
                             .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                             .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
                         });
    i32 dst_width = src_width > 1 ? src_width / 2 : 1;
    i32 dst_height = src_height > 1 ? src_height / 2 : 1;
    vkCmdBlitImage(m->blit_command_buffer, image,
                   VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                   &(VkImageBlit){
                       .srcSubresource =
                           {
                               .mipLevel = i,
                               .layerCount = 1,
                               .baseArrayLayer = 0,
                               .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                           },
                       .dstSubresource =
                           {
                               .mipLevel = i + 1,
                               .layerCount = 1,
                               .baseArrayLayer = 0,
                               .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                           },
                       .srcOffsets =
                           {
                               {0, 0, 0},
                               {src_width, src_height, 1},
                           },
                       .dstOffsets =
                           {
                               {0, 0, 0},
                               {dst_width, dst_height, 1},
                           },
                   },
                   VK_FILTER_LINEAR);
    if (transition_layout != VK_IMAGE_LAYOUT_UNDEFINED &&
        transition_layout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
      vkCmdPipelineBarrier(
          m->blit_command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
          VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, 0, NULL, 0, NULL, 1,
          &(VkImageMemoryBarrier){
              .image = image,
              .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
              .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
              .newLayout = transition_layout,
              .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
              .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
              .subresourceRange =
                  {
                      .baseMipLevel = i,
                      .levelCount = 1,
                      .baseArrayLayer = 0,
                      .layerCount = 1,
                      .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                  },
              .srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
              .dstAccessMask = VK_ACCESS_NONE,
          });
      src_width = dst_width;
      src_height = dst_height;
    }
  }

  if (transition_layout != VK_IMAGE_LAYOUT_UNDEFINED &&
      transition_layout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
    vkCmdPipelineBarrier(m->blit_command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, 0, NULL, 0, NULL,
                         1,
                         &(VkImageMemoryBarrier){
                             .image = image,
                             .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                             .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                             .newLayout = transition_layout,
                             .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                             .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                             .subresourceRange =
                                 {
                                     .baseMipLevel = m->mip_levels - 1,
                                     .levelCount = 1,
                                     .baseArrayLayer = 0,
                                     .layerCount = 1,
                                     .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                 },
                             .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                             .dstAccessMask = VK_ACCESS_NONE,
                         });
  }

  if ((result = vkEndCommandBuffer(m->blit_command_buffer)) != VK_SUCCESS) {
    LOG_ERROR("unable to end command buffer recording: %s",
              vk_error_to_string(result));
    return false;
  }

  if ((result = vkResetFences(tctx->device, 1, &tctx->fence)) != VK_SUCCESS) {
    LOG_ERROR("unable to reset transfer fence: %s", vk_error_to_string(result));
    return false;
  }

  if ((result = vkQueueSubmit(tctx->graphics_queue, 1,
                              &(VkSubmitInfo){
                                  .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                                  .commandBufferCount = 1,
                                  .pCommandBuffers = &m->blit_command_buffer,
                              },
                              tctx->fence)) != VK_SUCCESS) {
    LOG_ERROR("unable to submit command buffer to graphics queue: %s",
              vk_error_to_string(result));
    return false;
  }

  if ((result = vkWaitForFences(tctx->device, 1, &tctx->fence, VK_FALSE,
                                UINT64_MAX)) != VK_SUCCESS) {
    LOG_ERROR("unable to wait for command buffer to finish: %s",
              vk_error_to_string(result));
    return false;
  }

  return true;
}

bool image_load_from_file(VkPhysicalDevice physical_device,
                          const transfer_context *tctx, const char *path,
                          VkImageUsageFlags usage,
                          VkImageLayout transition_layout,
                          mipmap_context *mipmap, VkImage *image,
                          VmaAllocation *allocation, VkImageView *image_view,
                          VkSampler *sampler) {
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

  if (mipmap && mipmap->mip_levels > 1) {
    VkFormatProperties format_properties;
    vkGetPhysicalDeviceFormatProperties(physical_device, format,
                                        &format_properties);
    if (!(format_properties.linearTilingFeatures &
          VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)) {
      LOG_WARN("linear blitting not supported, mipmaping will be disabled");
      mipmap->mip_levels = 1;
    }

    i32 default_mip_levels = floor(log2(width > height ? width : height)) + 1;
    if (mipmap->mip_levels > default_mip_levels) {
      if (mipmap->mip_levels != INT32_MAX) {
        LOG_WARN(
            "too many mip levels requested, clamping to default mip levels "
            "%" PRIi32,
            default_mip_levels);
      }

      mipmap->mip_levels = default_mip_levels;
    }
  }

  assert(mipmap->mip_levels >= 1 && "at least one mip level is required");

  VkResult result;
  i32 num_unique_indices;
  VkSharingMode sharing_mode;
  u32 *unique_queue_indices = remove_duplicate_and_invalid_indices(
      (u32[]){tctx->indices.graphics, tctx->indices.transfer}, 2,
      &num_unique_indices, &sharing_mode);
  if ((result =
           vmaCreateImage(tctx->vma,
                          &(VkImageCreateInfo){
                              .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                              .extent = {width, height, 1},
                              .format = format,
                              .usage = usage | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                       VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                              .tiling = VK_IMAGE_TILING_OPTIMAL,
                              .samples = VK_SAMPLE_COUNT_1_BIT,
                              .sharingMode = sharing_mode,
                              .mipLevels = mipmap ? mipmap->mip_levels : 1,
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
          tctx, *image, mipmap->mip_levels,
          (VkRect2D){
              .offset = {0, 0},
              .extent = {width, height},
          },
          data, format,
          mipmap && mipmap->mip_levels > 1
              ? VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
              : transition_layout)) {
    LOG_ERROR("unable to stage image data to image memory");
    goto fail_stage;
  }

  if (mipmap && mipmap->mip_levels > 1) {
    image_generate_mipmap(tctx, mipmap, *image, (VkExtent2D){width, height},
                          transition_layout);
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
                         .levelCount = mipmap ? mipmap->mip_levels : 1,
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

  if (sampler) {

    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(physical_device, &properties);
    if ((result = vkCreateSampler(
             tctx->device,
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
                 .maxLod = mipmap ? mipmap->mip_levels : 1.0,
             },
             NULL, sampler)) != VK_SUCCESS) {
      LOG_ERROR("unable to create texture sampler: %s",
                vk_error_to_string(result));
      goto fail_sampler;
    }
  }

  stbi_image_free(data);
  return true;

fail_sampler:
  vkDestroyImageView(tctx->device, *image_view, NULL);
fail_image_view:
fail_stage:
  vmaDestroyImage(tctx->vma, *image, *allocation);
fail_image:
  stbi_image_free(data);
fail_stbi_load:
  return false;
}

void image_free(const transfer_context *c, VkImage image,
                VmaAllocation allocation, VkImageView image_view,
                VkSampler sampler) {
  if (sampler) {
    vkDestroySampler(c->device, sampler, NULL);
  }
  if (image_view != VK_NULL_HANDLE) {
    vkDestroyImageView(c->device, image_view, NULL);
  }
  vmaDestroyImage(c->vma, image, allocation);
}

static VkFormat pick_depth_format(VkPhysicalDevice device, VkImageTiling tiling,
                                  VkFormatFeatureFlags features) {
  VkFormat formats[] = {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT,
                        VK_FORMAT_D24_UNORM_S8_UINT};
  for (i32 i = 0; i < (i32)(sizeof(formats) / sizeof(formats[0])); ++i) {
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
                             VkSampleCountFlags samples, VkImage *image,
                             VmaAllocation *image_allocation,
                             VkFormat *depth_format, VkImageView *view) {
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
               .samples = samples,
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

  if (depth_format) {
    *depth_format = format;
  }

  return true;

fail_image_view:
  vmaDestroyImage(tctx->vma, *image, *image_allocation);
fail_image:
fail_format:
  return false;
}

bool image_init_msaa_buffer(const transfer_context *tctx, VkExtent2D size,
                            VkSampleCountFlags samples, VkFormat format,
                            VkImage *image, VmaAllocation *image_allocation,
                            VkImageView *view) {
  VkResult result;
  i32 num_unique_indices;
  VkSharingMode sharing_mode;
  u32 *unique_queue_indices = remove_duplicate_and_invalid_indices(
      (u32[]){tctx->indices.graphics, tctx->indices.transfer}, 2,
      &num_unique_indices, &sharing_mode);
  if ((result =
           vmaCreateImage(tctx->vma,
                          &(VkImageCreateInfo){
                              .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                              .extent = {size.width, size.height, 1},
                              .format = format,
                              .usage = VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT |
                                       VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                              .samples = samples,
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
    LOG_ERROR("unable to create image");
    goto fail_image;
  }

  if ((result = vkCreateImageView(
           tctx->device,
           &(VkImageViewCreateInfo){
               .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
               .image = *image,
               .subresourceRange =
                   {
                       .baseMipLevel = 0,
                       .baseArrayLayer = 0,
                       .layerCount = 1,
                       .levelCount = 1,
                       .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                   },
               .components =
                   {
                       .r = VK_COMPONENT_SWIZZLE_R,
                       .g = VK_COMPONENT_SWIZZLE_G,
                       .b = VK_COMPONENT_SWIZZLE_B,
                       .a = VK_COMPONENT_SWIZZLE_A,
                   },
               .viewType = VK_IMAGE_VIEW_TYPE_2D,
               .format = format,

           },
           NULL, view)) != VK_SUCCESS) {
    LOG_ERROR("unable to create image view for msaa color buffer: %s",
              vk_error_to_string(result));
    goto fail_image_view;
  }

  return true;

fail_image_view:
  vmaDestroyImage(tctx->vma, *image, *image_allocation);
fail_image:
  return false;
}
