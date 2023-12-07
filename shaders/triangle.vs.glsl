#version 450
#pragma shader_stage(vertex)

layout(location = 0) in vec2 a_position;
layout(location = 1) in vec3 a_color;
layout(location = 0) out vec2 position;
layout(location = 1) out vec3 color;

layout(binding = 0) uniform Matrices {
  mat4 proj;
  mat4 view;
  mat4 model;
} mat;

void main() {
  gl_Position = mat.proj * mat.view * mat.model * vec4(a_position, 0.0, 1.0);
  color = a_color;
  position = a_position;
}
