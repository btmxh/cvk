#include "command.h"
#include "debug_msg.h"
#include "device.h"
#include "image.h"
#include "instance.h"
#include "memory.h"
#include "shader.h"
#include "vk_utils.h"
#include "watch_linux.h"
#include "window.h"
#include <GLFW/glfw3.h>
#include <assert.h>
#include <cglm/affine.h>
#include <cglm/cam.h>
#include <cglm/mat4.h>
#include <cglm/util.h>
#include <logger.h>
#include <stb/stb_image.h>
#include <stdalign.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <vk_mem_alloc.h>
#include <vulkan/vk_enum_string_helper.h>
#include <vulkan/vulkan_core.h>

typedef struct {
  vec2 pos;
  vec3 color;
} vertex;

const vertex vertices[] = {
    (vertex){.pos = {-0.5, -0.5}, .color = {1.0, 0.0, 0.0}},
    (vertex){.pos = {0.5, -0.5}, .color = {0.0, 1.0, 0.0}},
    (vertex){.pos = {0.5, 0.5}, .color = {0.0, 0.0, 1.0}},
    (vertex){.pos = {-0.5, 0.5}, .color = {1.0, 1.0, 1.0}},
};

const u32 vertex_indices[] = {
    0, 1, 2, 2, 3, 0,
};

typedef struct {
  mat4 proj;
  mat4 view;
  mat4 model;
} uniform_matrices;

static void key_callback(GLFWwindow *w, int key, int scancode, int action,
                         int mods) {
  (void)scancode;
  (void)mods;
  if (key == GLFW_KEY_Q && action == GLFW_PRESS) {
    glfwSetWindowShouldClose(w, true);
  }
}

#define MAX_FRAMES_IN_FLIGHT 2

typedef struct {
  // windowing
  window w;
  bool recreate_swapchain;

  VkInstance instance;
  debug_messenger debug_msg;
  VkSurfaceKHR surface;
  VkPhysicalDevice physical_device;
  VkDevice device;
  VkQueue graphics_queue;
  VkQueue present_queue;

  // swapchain derived
  VkSwapchainKHR swapchain;
  VkSurfaceFormatKHR format;
  VkExtent2D extent;
  VkImage *images;
  VkImageView *image_views;
  u32 num_images;
  VkFramebuffer *framebuffers;
  present_sync_objects sync_objects[MAX_FRAMES_IN_FLIGHT];
  VkBuffer uniform_buffers[MAX_FRAMES_IN_FLIGHT];
  VmaAllocation uniform_buffer_allocation[MAX_FRAMES_IN_FLIGHT];
  VmaAllocationInfo uniform_buffer_allocation_info[MAX_FRAMES_IN_FLIGHT];
  VkDescriptorPool descriptor_pool;
  VkDescriptorSet descriptor_sets[MAX_FRAMES_IN_FLIGHT];
  VkDescriptorSetLayout descriptor_set_layout;
  u32 current_frame;

  // shader-related
  shader_compiler shaderc;
  watch file_watch;

  // pipeline
  VkPipelineLayout graphics_pipeline_layout;
  VkRenderPass render_pass;
  VkPipeline graphics_pipeline;

  // command
  VkCommandPool command_pools[MAX_FRAMES_IN_FLIGHT];
  VkCommandBuffer command_buffers[MAX_FRAMES_IN_FLIGHT];

  // memory-related
  VmaAllocator vk_allocator;
  transfer_context transfer;
  VkBuffer vertex_buffer;
  VmaAllocation vertex_buffer_allocation;
  VkBuffer index_buffer;
  VmaAllocation index_buffer_allocation;
  VkImage texture;
  VmaAllocation texture_allocation;
  VkImageView texture_view;
  VkSampler texture_sampler;
} app;

static void framebuffer_resize_callback(GLFWwindow *w, int width, int height) {
  (void)width;
  (void)height;
  app *a = glfwGetWindowUserPointer(w);
  a->recreate_swapchain = true;
}

static bool create_graphics_pipeline(app *a) {
  VkPipelineShaderStageCreateInfo stages[2] = {};
  i32 shader_counter = 0;
  if (!shader_compile_vk_stage(&a->shaderc, "shaders/triangle.vs.glsl",
                               a->device, VK_SHADER_STAGE_VERTEX_BIT,
                               &stages[shader_counter++]) ||
      !shader_compile_vk_stage(&a->shaderc, "shaders/triangle.fs.glsl",
                               a->device, VK_SHADER_STAGE_FRAGMENT_BIT,
                               &stages[shader_counter++])) {
    goto fail_shader_compilation;
  }

  VkResult result;
  if ((result = vkCreatePipelineLayout(
           a->device,
           &(VkPipelineLayoutCreateInfo){
               .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
               .setLayoutCount = 1,
               .pSetLayouts = &a->descriptor_set_layout,
               .pushConstantRangeCount = 0,
               .pPushConstantRanges = NULL,
           },
           NULL, &a->graphics_pipeline_layout)) != VK_SUCCESS) {
    LOG_ERROR("unable to create graphics pipeline layout: %s",
              vk_error_to_string(result));
    goto fail_pipeline_layout;
  }

  if ((result = vkCreateRenderPass(
           a->device,
           &(VkRenderPassCreateInfo){
               .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
               .attachmentCount = 1,
               .pAttachments =
                   &(VkAttachmentDescription){
                       .format = a->format.format,
                       .samples = VK_SAMPLE_COUNT_1_BIT,
                       .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                       .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                       .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                       .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                       .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                       .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                   },
               .subpassCount = 1,
               .pSubpasses =
                   &(VkSubpassDescription){
                       .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
                       .colorAttachmentCount = 1,
                       .pColorAttachments =
                           &(VkAttachmentReference){
                               .layout =
                                   VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                               .attachment = 0,
                           }},
               .dependencyCount = 1,
               .pDependencies =
                   &(VkSubpassDependency){
                       .srcSubpass = VK_SUBPASS_EXTERNAL,
                       .dstSubpass = 0,
                       .srcStageMask =
                           VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                       .srcAccessMask = 0,
                       .dstStageMask =
                           VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                       .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                   },
           },
           NULL, &a->render_pass)) != VK_SUCCESS) {
    LOG_ERROR("unable to create render pass: %s", vk_error_to_string(result));
    goto fail_render_pass;
  }

  if ((result = vkCreateGraphicsPipelines(
           a->device, VK_NULL_HANDLE, 1,
           &(VkGraphicsPipelineCreateInfo){
               .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
               .layout = a->graphics_pipeline_layout,
               .pStages = stages,
               .stageCount = sizeof(stages) / sizeof(stages[0]),
               .subpass = 0,
               .renderPass = a->render_pass,
               .pDynamicState =
                   &(VkPipelineDynamicStateCreateInfo){
                       .sType =
                           VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
                       .dynamicStateCount = 0,
                       .pDynamicStates = NULL,
                   },
               .pColorBlendState =
                   &(VkPipelineColorBlendStateCreateInfo){
                       .sType =
                           VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
                       .logicOp = VK_LOGIC_OP_COPY,
                       .attachmentCount = 1,
                       .pAttachments =
                           &(VkPipelineColorBlendAttachmentState){
                               .blendEnable = VK_TRUE,
                               .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
                               .dstColorBlendFactor =
                                   VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                               .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
                               .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
                               .colorBlendOp = VK_BLEND_OP_ADD,
                               .alphaBlendOp = VK_BLEND_OP_ADD,
                               .colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                                                 VK_COLOR_COMPONENT_G_BIT |
                                                 VK_COLOR_COMPONENT_B_BIT |
                                                 VK_COLOR_COMPONENT_A_BIT,
                           },
                       .logicOpEnable = VK_FALSE,
                       .blendConstants = {},
                   },
               .pViewportState =
                   &(VkPipelineViewportStateCreateInfo){
                       .sType =
                           VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
                       .scissorCount = 1,
                       .pScissors =
                           &(VkRect2D){
                               .offset = {0, 0},
                               .extent = a->extent,
                           },
                       .viewportCount = 1,
                       .pViewports =
                           &(VkViewport){
                               .x = 0,
                               .y = 0,
                               .width = a->extent.width,
                               .height = a->extent.height,
                               .minDepth = 0.0,
                               .maxDepth = 1.0,
                           },
                   },
               .pMultisampleState =
                   &(VkPipelineMultisampleStateCreateInfo){
                       .sType =
                           VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
                       .pSampleMask = NULL,
                       .minSampleShading = 1.0,
                       .alphaToOneEnable = VK_FALSE,
                       .alphaToCoverageEnable = VK_FALSE,
                       .sampleShadingEnable = VK_FALSE,
                       .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
                   },
               .pVertexInputState =
                   &(VkPipelineVertexInputStateCreateInfo){
                       .sType =
                           VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
                       .vertexBindingDescriptionCount = 1,
                       .pVertexBindingDescriptions =
                           (VkVertexInputBindingDescription[]){
                               (VkVertexInputBindingDescription){
                                   .stride = sizeof(vertex),
                                   .binding = 0,
                                   .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
                               },
                           },
                       .vertexAttributeDescriptionCount = 2,
                       .pVertexAttributeDescriptions =
                           (VkVertexInputAttributeDescription[]){
                               (VkVertexInputAttributeDescription){
                                   .binding = 0,
                                   .offset = offsetof(vertex, pos),
                                   .format = VK_FORMAT_R32G32_SFLOAT,
                                   .location = 0,
                               },
                               (VkVertexInputAttributeDescription){
                                   .binding = 0,
                                   .offset = offsetof(vertex, color),
                                   .format = VK_FORMAT_R32G32B32_SFLOAT,
                                   .location = 1,
                               }},
                   },
               .basePipelineHandle = VK_NULL_HANDLE,
               .basePipelineIndex = -1,
               .pDepthStencilState = NULL,
               .pTessellationState = NULL,
               .pInputAssemblyState =
                   &(VkPipelineInputAssemblyStateCreateInfo){
                       .sType =
                           VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
                       .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                       .primitiveRestartEnable = VK_FALSE,
                   },
               .pRasterizationState =
                   &(VkPipelineRasterizationStateCreateInfo){
                       .sType =
                           VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
                       .cullMode = VK_CULL_MODE_BACK_BIT,
                       .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
                       .lineWidth = 1.0,
                       .polygonMode = VK_POLYGON_MODE_FILL,
                       .depthBiasEnable = VK_FALSE,
                       .depthClampEnable = VK_FALSE,
                       .depthBiasClamp = 0.0,
                       .depthBiasSlopeFactor = 0.0,
                       .depthBiasConstantFactor = 0.0,
                       .rasterizerDiscardEnable = VK_FALSE,
                   },
           },
           NULL, &a->graphics_pipeline)) != VK_SUCCESS) {
    LOG_ERROR("unable to create graphics pipeline");
    goto fail_graphics_pipeline;
  }

  for (i32 i = 0; i < shader_counter; ++i) {
    shader_free_vk_stage(a->device, &stages[i]);
  }
  return true;

fail_graphics_pipeline:
  vkDestroyRenderPass(a->device, a->render_pass, NULL);
fail_render_pass:
  vkDestroyPipelineLayout(a->device, a->graphics_pipeline_layout, NULL);
fail_pipeline_layout:
fail_shader_compilation:
  for (i32 i = 0; i < shader_counter; ++i) {
    shader_free_vk_stage(a->device, &stages[i]);
  }

  return false;
}

static void free_graphics_pipeline(app *a) {
  vkDestroyPipeline(a->device, a->graphics_pipeline, NULL);
  vkDestroyRenderPass(a->device, a->render_pass, NULL);
  vkDestroyPipelineLayout(a->device, a->graphics_pipeline_layout, NULL);
}

static bool init_swapchain_related(app *a) {
  VkSwapchainKHR old_swapchain = a->swapchain;
  if (!swapchain_init(&a->w, a->physical_device, a->device, a->surface,
                      old_swapchain, &a->swapchain, &a->format, &a->extent)) {
    LOG_ERROR("unable to create vulkan swapchain");
    goto fail_vk_swapchain;
  }

  if (!(a->images =
            swapchain_get_images(a->device, a->swapchain, &a->num_images))) {
    LOG_ERROR("unable to get vulkan swapchain images");
    goto fail_vk_swapchain_images;
  }

  a->image_views = NULL;
  if (!swapchain_image_views_init(a->device, a->images, a->num_images,
                                  a->format.format, &a->image_views)) {
    LOG_ERROR("unable to create image views for swapchain images");
    goto fail_vk_swapchain_image_views;
  }

  if (!create_graphics_pipeline(a)) {
    LOG_ERROR("unable to initialize graphics pipeline for app");
    goto fail_graphics_pipeline;
  }

  if (!framebuffers_init(a->device, a->num_images, a->image_views, &a->extent,
                         a->render_pass, &a->framebuffers)) {
    LOG_ERROR("unable to initialize present framebuffers");
    goto fail_framebuffers;
  }

  return true;

  framebuffers_free(a->device, a->num_images, a->framebuffers);
fail_framebuffers:
  free_graphics_pipeline(a);
fail_graphics_pipeline:
  swapchain_image_views_destroy(a->device, a->image_views, a->num_images);
fail_vk_swapchain_image_views:
  free(a->images);
fail_vk_swapchain_images:
  swapchain_free(a->device, a->swapchain);
fail_vk_swapchain:
  return false;
}

static void free_swapchain_related(app *a) {
  framebuffers_free(a->device, a->num_images, a->framebuffers);
  free_graphics_pipeline(a);
  swapchain_image_views_destroy(a->device, a->image_views, a->num_images);
  free(a->images);
  swapchain_free(a->device, a->swapchain);
}

static bool recreate_swapchain_related(app *a) {
  int width, height;
  glfwGetFramebufferSize(a->w.window, &width, &height);
  while (width == 0 || height == 0) {
    glfwGetFramebufferSize(a->w.window, &width, &height);
    glfwWaitEvents();
  }

  vkDeviceWaitIdle(a->device);
  free_swapchain_related(a);
  a->swapchain = NULL;
  return init_swapchain_related(a);
}

static bool app_init(app *a) {
  if (!window_init(&a->w, 1280, 720, "vulkan")) {
    LOG_ERROR("error: unable to open window");
    return false;
  }

  glfwSetWindowUserPointer(a->w.window, a);
  a->recreate_swapchain = false;

  glfwSetKeyCallback(a->w.window, key_callback);
  glfwSetFramebufferSizeCallback(a->w.window, framebuffer_resize_callback);

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

  if (!device_init(a->physical_device, a->surface, &a->device)) {
    LOG_ERROR("unable to create vulkan device");
    goto fail_vk_device;
  }

  queue_family_indices indices;
  if (!find_queue_families(a->physical_device, a->surface, &indices)) {
    LOG_ERROR("unable to find queue family indices");
    goto fail_queue_indices;
  }

  vkGetDeviceQueue(a->device, indices.graphics, 0, &a->graphics_queue);
  vkGetDeviceQueue(a->device, indices.present, 0, &a->present_queue);

  if (!shader_compiler_init(&a->shaderc)) {
    LOG_ERROR("unable to initialize shader compiler");
    goto fail_shaderc;
  }

  VkResult result;
  if (!vma_create(a->instance, a->physical_device, a->device,
                  &a->vk_allocator)) {
    LOG_ERROR("unable to create vulkan memory allocator");
    goto fail_vma;
  }

  if (!transfer_context_init(a->device, a->vk_allocator, &indices,
                             &a->transfer)) {
    LOG_ERROR("unable to create vulkan memory transfer context");
    goto fail_transfer;
  }

  i32 num_unique_indices;
  VkSharingMode sharing_mode;
  u32 *unique_queue_indices = remove_duplicate_and_invalid_indices(
      (u32[]){indices.transfer, indices.graphics}, 2, &num_unique_indices,
      &sharing_mode);
  assert(num_unique_indices > 0);

  if ((result =
           vmaCreateBuffer(a->vk_allocator,
                           &(VkBufferCreateInfo){
                               .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                               .size = sizeof(vertices),
                               .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                                        VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                               .pQueueFamilyIndices = unique_queue_indices,
                               .queueFamilyIndexCount = num_unique_indices,
                               .sharingMode = sharing_mode,
                           },
                           &(VmaAllocationCreateInfo){
                               .usage = VMA_MEMORY_USAGE_AUTO,
                           },
                           &a->vertex_buffer, &a->vertex_buffer_allocation,
                           NULL)) != VK_SUCCESS) {
    LOG_ERROR("unable to allocate vertex buffer: %s",
              vk_error_to_string(result));
    goto fail_vertex_buffer;
  }

  if (!transfer_context_stage_to_buffer(&a->transfer, a->vertex_buffer,
                                        sizeof(vertices), 0, vertices)) {
    LOG_ERROR("unable to stage vertex data to vertex buffer");
    goto fail_stage_vertex_buffer;
  }

  if ((result =
           vmaCreateBuffer(a->vk_allocator,
                           &(VkBufferCreateInfo){
                               .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                               .size = sizeof(vertex_indices),
                               .usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                                        VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                               .sharingMode = sharing_mode,
                               .queueFamilyIndexCount = num_unique_indices,
                               .pQueueFamilyIndices = unique_queue_indices,
                           },
                           &(VmaAllocationCreateInfo){
                               .usage = VMA_MEMORY_USAGE_AUTO,
                           },
                           &a->index_buffer, &a->index_buffer_allocation,
                           NULL)) != VK_SUCCESS) {
    LOG_ERROR("unable to allocate index buffer: %s",
              vk_error_to_string(result));
    goto fail_index_buffer;
  }

  if (!transfer_context_stage_to_buffer(&a->transfer, a->index_buffer,
                                        sizeof(vertex_indices), 0,
                                        vertex_indices)) {
    LOG_ERROR("unable to stage index data to index buffer");
    goto fail_stage_index_buffer;
  }

  i32 num_uniform_buffers = 0;
  while (num_uniform_buffers < MAX_FRAMES_IN_FLIGHT) {
    if ((result = vmaCreateBuffer(
             a->vk_allocator,
             &(VkBufferCreateInfo){
                 .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                 .size = sizeof(uniform_matrices),
                 .usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT |
                          VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                 .sharingMode = sharing_mode,
                 .queueFamilyIndexCount = num_unique_indices,
                 .pQueueFamilyIndices = unique_queue_indices,
             },
             &(VmaAllocationCreateInfo){
                 .usage = VMA_MEMORY_USAGE_AUTO,
                 .flags =
                     VMA_ALLOCATION_CREATE_MAPPED_BIT |
                     VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
             },
             &a->uniform_buffers[num_uniform_buffers],
             &a->uniform_buffer_allocation[num_uniform_buffers],
             &a->uniform_buffer_allocation_info[num_uniform_buffers])) !=
        VK_SUCCESS) {
      LOG_ERROR("unable to allocate %d-th uniform buffer: %s",
                num_uniform_buffers + 1, vk_error_to_string(result));
      goto fail_uniform_buffers;
    }

    ++num_uniform_buffers;
  }

  if ((result = vkCreateDescriptorPool(
           a->device,
           &(VkDescriptorPoolCreateInfo){
               .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
               .poolSizeCount = 1,
               .pPoolSizes =
                   (VkDescriptorPoolSize[]){
                       (VkDescriptorPoolSize){
                           .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                           .descriptorCount = MAX_FRAMES_IN_FLIGHT,
                       },
                       (VkDescriptorPoolSize){
                           .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                           .descriptorCount = MAX_FRAMES_IN_FLIGHT,
                       }},
               .maxSets = MAX_FRAMES_IN_FLIGHT,
           },
           NULL, &a->descriptor_pool)) != VK_SUCCESS) {
    LOG_ERROR("unable to create descriptor pool: %s",
              vk_error_to_string(result));
    goto fail_descriptor_pool;
  }

  if ((result = vkCreateDescriptorSetLayout(
           a->device,
           &(VkDescriptorSetLayoutCreateInfo){
               .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
               .bindingCount = 1,
               .pBindings =
                   (VkDescriptorSetLayoutBinding[]){
                       {
                           .binding = 0,
                           .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                           .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
                           .descriptorCount = 1,
                           .pImmutableSamplers = NULL,
                       },
                       (VkDescriptorSetLayoutBinding){
                           .binding = 1,
                           .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
                           .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                           .descriptorCount = 1,
                           .pImmutableSamplers = &a->texture_sampler,
                       }}},
           NULL, &a->descriptor_set_layout)) != VK_SUCCESS) {
    LOG_ERROR("unable to create descriptor set layout: %s",
              vk_error_to_string(result));
    goto fail_descriptor_layout;
  }

  VkDescriptorSetLayout layouts[MAX_FRAMES_IN_FLIGHT];
  for (i32 i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
    layouts[i] = a->descriptor_set_layout;
  }
  if ((result = vkAllocateDescriptorSets(
           a->device,
           &(VkDescriptorSetAllocateInfo){
               .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
               .descriptorPool = a->descriptor_pool,
               .descriptorSetCount = MAX_FRAMES_IN_FLIGHT,
               .pSetLayouts = layouts,
           },
           a->descriptor_sets)) != VK_SUCCESS) {
    LOG_ERROR("unable to allocate descriptor sets from descriptor pool");
    goto fail_descriptor_sets;
  }

  for (i32 i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
    vkUpdateDescriptorSets(
        a->device, 1,
        &(VkWriteDescriptorSet){
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .descriptorCount = 1,
            .dstSet = a->descriptor_sets[i],
            .dstBinding = 0,
            .pImageInfo = NULL,
            .pBufferInfo =
                &(VkDescriptorBufferInfo){
                    .offset = 0,
                    .range = sizeof(uniform_matrices),
                    .buffer = a->uniform_buffers[i],
                },
            .dstArrayElement = 0,
            .pTexelBufferView = NULL,
        },
        0, NULL);
  }

  if (!image_load_from_file(&a->transfer, "resources/plst.png", &a->texture,
                            &a->texture_allocation, &a->texture_view)) {
    LOG_ERROR("unable to load texture");
    goto fail_image_load;
  }

  if (!sampler_create(a->physical_device, a->device, &a->texture_sampler)) {
    LOG_ERROR("unable to create texture sampler");
    goto fail_sampler;
  }

  a->swapchain = VK_NULL_HANDLE;
  if (!init_swapchain_related(a)) {
    LOG_ERROR("unable to initialize swapchain-dependent vulkan objects");
    goto fail_vk_swapchain;
  }

  i32 num_command_pools = 0;
  while (num_command_pools < MAX_FRAMES_IN_FLIGHT) {
    if (!command_pool_create(a->device, indices.graphics,
                             &a->command_pools[num_command_pools])) {
      LOG_ERROR("unable to create %" PRIi32 "-th command pool",
                num_command_pools + 1);
      goto fail_command_pools;
    }

    if (!command_buffer_allocate(a->device, a->command_pools[num_command_pools],
                                 VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1,
                                 &a->command_buffers[num_command_pools])) {
      LOG_ERROR("unable to allocate %" PRIi32
                "-th command buffer from command pool",
                num_command_pools + 1);
      goto fail_command_pools;
    }

    ++num_command_pools;
  }

  u32 num_sync_objects = 0;
  while (num_sync_objects < MAX_FRAMES_IN_FLIGHT) {
    if (!present_sync_objects_init(a->device,
                                   &a->sync_objects[num_sync_objects++])) {
      LOG_ERROR("unable to create %" PRIu32 "-th sync objects",
                num_sync_objects);
      goto fail_present_sync_objects;
    }
  }

  a->current_frame = 0;

  if (!watch_init(&a->file_watch)) {
    LOG_WARN("unable to initialize shader file watch");
    goto fail_file_watch;
  }

  watch_add(&a->file_watch, "shaders/");

  return true;

  watch_free(&a->file_watch);
fail_file_watch:
fail_present_sync_objects:
  for (u32 i = 0; i < num_sync_objects; ++i) {
    present_sync_objects_free(a->device, &a->sync_objects[i]);
  }
fail_command_pools:
  for (i32 i = 0; i < num_command_pools; ++i) {
    command_pool_free(a->device, a->command_pools[i]);
  }
  free_swapchain_related(a);
fail_vk_swapchain:
  sampler_free(a->device, a->texture_sampler);
fail_sampler:
  image_free(&a->transfer, a->texture, a->texture_allocation, a->texture_view);
fail_image_load:
fail_descriptor_sets:
fail_descriptor_layout:
  vkDestroyDescriptorPool(a->device, a->descriptor_pool, NULL);
fail_descriptor_pool:
fail_uniform_buffers:
  for (i32 i = 0; i < num_uniform_buffers; ++i) {
    vmaDestroyBuffer(a->vk_allocator, a->uniform_buffers[i],
                     a->uniform_buffer_allocation[i]);
  }
fail_stage_index_buffer:
  vmaDestroyBuffer(a->vk_allocator, a->index_buffer,
                   a->index_buffer_allocation);
fail_index_buffer:
fail_stage_vertex_buffer:
  vmaDestroyBuffer(a->vk_allocator, a->vertex_buffer,
                   a->vertex_buffer_allocation);
fail_vertex_buffer:
  transfer_context_free(&a->transfer);
fail_transfer:
  vma_destroy(a->vk_allocator);
fail_vma:
  shader_compiler_free(&a->shaderc);
fail_shaderc:
  device_free(a->device);
fail_queue_indices:
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
  vkDeviceWaitIdle(a->device);
  watch_free(&a->file_watch);

  for (u32 i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
    present_sync_objects_free(a->device, &a->sync_objects[i]);
  }
  for (i32 i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
    command_pool_free(a->device, a->command_pools[i]);
  }
  free_swapchain_related(a);
  sampler_free(a->device, a->texture_sampler);
  image_free(&a->transfer, a->texture, a->texture_allocation, a->texture_view);
  vkDestroyDescriptorSetLayout(a->device, a->descriptor_set_layout, NULL);
  vkDestroyDescriptorPool(a->device, a->descriptor_pool, NULL);
  for (i32 i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
    vmaDestroyBuffer(a->vk_allocator, a->uniform_buffers[i],
                     a->uniform_buffer_allocation[i]);
  }
  vmaDestroyBuffer(a->vk_allocator, a->index_buffer,
                   a->index_buffer_allocation);
  vmaDestroyBuffer(a->vk_allocator, a->vertex_buffer,
                   a->vertex_buffer_allocation);
  transfer_context_free(&a->transfer);
  vmaDestroyAllocator(a->vk_allocator);
  shader_compiler_free(&a->shaderc);
  device_free(a->device);
  surface_free(a->instance, a->surface);
  debug_msg_free(a->instance, a->debug_msg);
  vk_instance_free(a->instance);
  window_free(&a->w);
}

const char *watch_shader_files[] = {"triangle.vs.glsl", "triangle.fs.glsl"};

static void app_loop(app *a) {
  while (!window_should_close(&a->w)) {
    window_poll_events();

    watch_event e;
    bool reload = false;
    while (watch_poll(&a->file_watch, &e)) {
      if (!a->recreate_swapchain) {
        for (i32 i = 0; i < (i32)(sizeof(watch_shader_files) /
                                  sizeof(watch_shader_files[0]));
             ++i) {
          if (strcmp(e.name, watch_shader_files[i]) == 0) {
            reload = true;
          }
        }
      }

      watch_event_free(&a->file_watch, &e);
    }

    if (reload) {
      LOG_INFO("reloading shaders");
      a->recreate_swapchain = reload;
    }

    u32 frame_index = a->current_frame;
    present_sync_objects *sync_obj = &a->sync_objects[frame_index];
    VkResult result;
    if ((result = vkWaitForFences(a->device, 1, &sync_obj->in_flight, VK_TRUE,
                                  UINT64_MAX)) != VK_SUCCESS) {
      LOG_ERROR("unable to wait for and/or reset in flight fence for frame "
                "index %" PRIu32 ": %s",
                frame_index, vk_error_to_string(result));
      return;
    }
    u32 image_index;
    ;
    if ((result = vkAcquireNextImageKHR(
             a->device, a->swapchain, UINT64_MAX, sync_obj->image_available,
             VK_NULL_HANDLE, &image_index)) != VK_SUCCESS) {
      if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        if (!recreate_swapchain_related(a)) {
          LOG_ERROR("unable to recreate swapchain");
          return;
        }
      } else if (result != VK_SUBOPTIMAL_KHR) {
        LOG_ERROR("unable to acquire presentation image: %s",
                  vk_error_to_string(result));
        return;
      }
    }

    // update uniform buffers
    {
      uniform_matrices mat;
      glm_mat4_identity(mat.proj);
      glm_mat4_identity(mat.view);
      glm_mat4_identity(mat.model);

      double time = glfwGetTime();
      glm_perspective(glm_rad(45.0), (float)a->extent.width / a->extent.height,
                      0.1, 10.0, mat.proj);
      mat.proj[1][1] *= -1;
      glm_lookat((vec3){1, 1, 1}, (vec3){0, 0, 0}, (vec3){0, 0, 1}, mat.view);
      glm_rotate_make(mat.model, time * GLM_PI_4, (vec3){0, 0, 1});

      memcpy(a->uniform_buffer_allocation_info[frame_index].pMappedData, &mat,
             sizeof(mat));
    }

    if ((result = vkResetFences(a->device, 1,
                                &a->sync_objects[frame_index].in_flight)) !=
        VK_SUCCESS) {
      LOG_ERROR("unable to wait for and/or reset in flight fence for frame "
                "index %" PRIu32 ": %s",
                frame_index, vk_error_to_string(result));
      return;
    }

    if ((result = vkResetCommandPool(a->device, a->command_pools[frame_index],
                                     0)) != VK_SUCCESS) {
      LOG_ERROR("unable to reset %" PRIi32 "-th present command pool",
                frame_index);
      return;
    }
    VkCommandBuffer command_buffer = a->command_buffers[frame_index];
    // record command buffer
    {
      if ((result = vkBeginCommandBuffer(
               command_buffer,
               &(VkCommandBufferBeginInfo){
                   .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
               })) != VK_SUCCESS) {
        LOG_ERROR("unable to begin command buffer");
        return;
      }

      vkCmdBeginRenderPass(
          command_buffer,
          &(VkRenderPassBeginInfo){
              .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
              .renderPass = a->render_pass,
              .renderArea =
                  {
                      .offset = {0, 0},
                      .extent = a->extent,
                  },
              .framebuffer = a->framebuffers[image_index],
              .clearValueCount = 1,
              .pClearValues = (VkClearValue[]){(VkClearValue){
                  .color =
                      {
                          .float32 = {0, 0, 0, 1},
                      },
              }}},
          VK_SUBPASS_CONTENTS_INLINE);
      {
        vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          a->graphics_pipeline);
        vkCmdBindVertexBuffers(command_buffer, 0, 1, &a->vertex_buffer,
                               &(VkDeviceSize){0});
        vkCmdBindIndexBuffer(command_buffer, a->index_buffer, 0,
                             VK_INDEX_TYPE_UINT32);
        vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                a->graphics_pipeline_layout, 0, 1,
                                &a->descriptor_sets[frame_index], 0, NULL);
        vkCmdDrawIndexed(command_buffer,
                         sizeof(vertex_indices) / sizeof(vertex_indices[0]), 1,
                         0, 0, 0);
      }

      vkCmdEndRenderPass(command_buffer);

      vkEndCommandBuffer(command_buffer);
    }

    // submit queue
    {
      if ((vkQueueSubmit(
              a->graphics_queue, 1,
              &(VkSubmitInfo){
                  .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                  .commandBufferCount = 1,
                  .pCommandBuffers = &command_buffer,
                  .waitSemaphoreCount = 1,
                  .pWaitSemaphores = (VkSemaphore[]){sync_obj->image_available},
                  .signalSemaphoreCount = 1,
                  .pSignalSemaphores =
                      (VkSemaphore[]){sync_obj->render_finished},
                  .pWaitDstStageMask =
                      (VkPipelineStageFlags[]){
                          VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT},
              },
              sync_obj->in_flight)) != VK_SUCCESS) {
        LOG_ERROR("unable to submit draw command buffer: %s",
                  vk_error_to_string(result));
        return;
      }

      if ((result = vkQueuePresentKHR(
               a->present_queue,
               &(VkPresentInfoKHR){
                   .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
                   .swapchainCount = 1,
                   .pSwapchains = &a->swapchain,
                   .pImageIndices = &image_index,
                   .waitSemaphoreCount = 1,
                   .pWaitSemaphores =
                       (VkSemaphore[]){sync_obj->render_finished},
               })) != VK_SUCCESS ||
          a->recreate_swapchain) {
        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR ||
            a->recreate_swapchain) {
          if (!recreate_swapchain_related(a)) {
            LOG_ERROR("unable to recreate swapchain");
            return;
          }
          a->recreate_swapchain = false;
        } else {
          LOG_ERROR("unable to present rendered result: %s",
                    vk_error_to_string(result));
          return;
        }
      }
    }

    a->current_frame = (frame_index + 1) % MAX_FRAMES_IN_FLIGHT;
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
