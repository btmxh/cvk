#pragma once

#include "types.h"
#include <shaderc/shaderc.h>
#include <vulkan/vulkan_core.h>

typedef struct {
  shaderc_compiler_t compiler;
} shader_compiler;

bool shader_compiler_init(shader_compiler *compiler);
void shader_compiler_free(shader_compiler *compiler);

u32 *shader_compile_file(shader_compiler *compiler, const char *filename,
                         u32 *bytes_len);

bool shader_compile_vk_module(shader_compiler *compiler, const char *filename,
                              VkDevice device, VkShaderModule *module);
void shader_free_vk_module(VkDevice device, VkShaderModule module);
bool shader_compile_vk_stage(shader_compiler *compiler, const char *filename,
                             VkDevice device, VkShaderStageFlagBits shader_type,
                             VkPipelineShaderStageCreateInfo *stage);
void shader_free_vk_stage(VkDevice device,
                          VkPipelineShaderStageCreateInfo *stage);
