#version 450
#pragma shader_stage(vertex)

layout(location = 0) in vec3 a_position;
layout(location = 1) in vec2 a_texCoords;
layout(location = 0) out vec3 position;
layout(location = 1) out vec2 texCoords;

layout(binding = 0) uniform Matrices {
  mat4 proj;
  mat4 view;
  mat4 model;
} mat;

void main() {
  gl_Position = mat.proj * mat.view * mat.model * vec4(a_position, 1.0);
  position = a_position;
  texCoords = a_texCoords;
}
