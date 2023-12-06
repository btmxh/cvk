#include "shader.h"
#include "vk_utils.h"
#include <assert.h>
#include <logger.h>
#include <shaderc/shaderc.h>
#include <stdio.h>
#include <vulkan/vulkan_core.h>
#ifndef __USE_MISC
#define __USE_MISC
#endif
#include <stdlib.h>

#ifdef __unix__
#include <libgen.h>
#include <string.h>

static char *concat_path(const char *a, const char *b) {
  i32 len_a = strlen(a), len_b = strlen(b);
  assert(len_a > 0);
  bool needs_slash = a[len_a - 1] != '/';

  char *p = calloc(len_a + needs_slash + len_b + 1, 1);
  if (!p) {
    return NULL;
  }

  if (needs_slash) {
    memcpy(p, a, len_a);
    p[len_a] = '/';
    ++len_a;
  }

  memcpy(&p[len_a], b, len_b);
  p[len_a + len_b] = '\0';
  return p;
}

static char *resolve_sibling(const char *a, const char *b) {
  char *dir = malloc(strlen(a) + 1);
  if (!dir) {
    return NULL;
  }

  strcpy(dir, a);
  char *path = concat_path(dir, b);
  free(dir);

  return path;
}
#else
#warning shaderc include support disabled. Please implement your own file path functions for non-unix platforms
#endif

static char *read_file(const char *path, i32 *len) {
  FILE *file = fopen(path, "rb");
  if (!file) {
    goto fail_fopen;
  }

  fseek(file, 0, SEEK_END);
  long s_len = ftell(file);
  assert(s_len >= 0);
  *len = s_len;
  fseek(file, 0, SEEK_SET);

  char *buf = malloc(*len);
  if (!buf) {
    LOG_ERROR("unable to allocate buffer for file");
    goto fail_malloc;
  }

  fread(buf, *len, 1, file);
  assert(!ferror(file));
  assert(fgetc(file) == EOF && feof(file));

  fclose(file);
  return buf;

  free(buf);
fail_malloc:
  fclose(file);
fail_fopen:
  return NULL;
}

bool shader_compiler_init(shader_compiler *compiler) {
  compiler->compiler = shaderc_compiler_initialize();
  if (!compiler->compiler) {
    LOG_ERROR("unable to initialize shaderc shader compiler");
    return false;
  }

  return true;
}

void shader_compiler_free(shader_compiler *compiler) {
  shaderc_compiler_release(compiler->compiler);
}

static shaderc_include_result *
shader_resolver(void *user_data, const char *requested_source, int type,
                const char *requesting_source, usize include_depth) {
  (void)user_data;
  (void)include_depth;
  shaderc_include_result *result = malloc(sizeof(*result));
  if (!result) {
    return NULL;
  }

  if (type == shaderc_include_type_relative) {
    char *relative = resolve_sibling(requested_source, requesting_source);
    if (relative) {
      i32 length;
      result->content = read_file(relative, &length);
      result->content_length = length;
      if (result->content) {
        result->source_name = realpath(relative, NULL);
        result->source_name_length = strlen(result->source_name);
        return result;
      }

      free(relative);
    }
  }

  free(result);
  return NULL;
}

static void shader_releaser(void *user_data, shaderc_include_result *result) {
  (void)user_data;
  free((void *)result->content);
  free((void *)result->source_name);
}

static const char *shader_status_to_string(shaderc_compilation_status status) {
#define CASE(x)                                                                \
  case x:                                                                      \
    return #x
  switch (status) {
    CASE(shaderc_compilation_status_success);
    CASE(shaderc_compilation_status_invalid_stage); // error stage deduction
    CASE(shaderc_compilation_status_compilation_error);
    CASE(shaderc_compilation_status_internal_error); // unexpected failure
    CASE(shaderc_compilation_status_null_result_object);
    CASE(shaderc_compilation_status_invalid_assembly);
    CASE(shaderc_compilation_status_validation_error);
    CASE(shaderc_compilation_status_transformation_error);
    CASE(shaderc_compilation_status_configuration_error);
  }
  return "shader_compilation_status_unknown";
}

u32 *shader_compile_file(shader_compiler *compiler, const char *filename,
                         u32 *bytes_len) {
  i32 len;
  char *buf = read_file(filename, &len);
  if (!buf) {
    LOG_ERROR("unable to read file at path '%s'", filename);
    goto fail_read_file;
  }

  shaderc_compile_options_t opts = shaderc_compile_options_initialize();
  shaderc_compile_options_set_include_callbacks(opts, shader_resolver,
                                                shader_releaser, NULL);
  shaderc_compilation_result_t result = shaderc_compile_into_spv(
      compiler->compiler, buf, len, shaderc_glsl_infer_from_source, filename,
      "main", opts);
  free(buf);

  shaderc_compilation_status status =
      shaderc_result_get_compilation_status(result);
  if (status != shaderc_compilation_status_success) {
    LOG_ERROR("error compiling shader: %s", shader_status_to_string(status));
    LOG_ERROR("shader compilation log (%" PRIi32 " error(s), %" PRIi32
              " warning(s)):",
              shaderc_result_get_num_errors(result),
              shaderc_result_get_num_warnings(result));
    LOG_ERROR("\t%s", shaderc_result_get_error_message(result));
    goto fail_compilation;
  }

  *bytes_len = shaderc_result_get_length(result);
  shaderc_compile_options_release(opts);

  i32 num_errors = shaderc_result_get_num_errors(result),
      num_warnings = shaderc_result_get_num_warnings(result);
  const char *log = shaderc_result_get_error_message(result);
  if (num_errors > 0 || num_warnings > 0 || (log != NULL && strlen(log) > 0)) {
    LOG_DEBUG("shader compilation log (%" PRIi32 " error(s), %" PRIi32
              " warning(s)):",
              num_errors, num_warnings);
    LOG_DEBUG("\t%s", shaderc_result_get_error_message(result));
  }

  char *bytes = malloc(*bytes_len);
  if (!bytes) {
    LOG_ERROR("unable to allocate spirv bytecode buffer");
    goto fail_malloc;
  }

  memcpy(bytes, shaderc_result_get_bytes(result), *bytes_len);
  shaderc_result_release(result);
  return (u32 *)bytes;
fail_malloc:
fail_compilation:
  shaderc_result_release(result);
  shaderc_compile_options_release(opts);
fail_read_file:
  return NULL;
}

bool shader_compile_vk_module(shader_compiler *compiler, const char *filename,
                              VkDevice device, VkShaderModule *module) {
  u32 len;
  u32 *code = shader_compile_file(compiler, filename, &len);
  if (!code) {
    return false;
  }

  VkResult result;
  if ((result = vkCreateShaderModule(
           device,
           &(VkShaderModuleCreateInfo){
               .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
               .pCode = code,
               .codeSize = len,
           },
           NULL, module)) != VK_SUCCESS) {
    LOG_ERROR("unable to create shader module: %s", vk_error_to_string(result));
  }

  free(code);
  return result == VK_SUCCESS;
}

void shader_free_vk_module(VkDevice device, VkShaderModule module) {
  vkDestroyShaderModule(device, module, NULL);
}

bool shader_compile_vk_stage(shader_compiler *compiler, const char *filename,
                             VkDevice device, VkShaderStageFlagBits shader_type,
                             VkPipelineShaderStageCreateInfo *stage) {
  VkShaderModule module;
  if (!shader_compile_vk_module(compiler, filename, device, &module)) {
    LOG_ERROR("unable to compile shader into module");
    return false;
  }

  stage->sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stage->stage = shader_type;
  stage->module = module;
  stage->pName = "main";
  stage->pSpecializationInfo = NULL;
  return true;
}

void shader_free_vk_stage(VkDevice device,
                          VkPipelineShaderStageCreateInfo *stage) {
  shader_free_vk_module(device, stage->module);
}
