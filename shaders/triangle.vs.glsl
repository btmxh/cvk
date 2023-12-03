#version 450
#pragma shader_stage(vertex)

vec2 positions[3] = vec2[] (
  vec2(0.0, -0.5),
  vec2(0.5, 0.5),
  vec2(-0.5, 0.5)
);

const vec3 colors[] = vec3[](
  vec3(1, 0, 0),
  vec3(0, 1, 0),
  vec3(0, 0, 1)
);

layout(location = 0) out vec2 position;
layout(location = 1) out vec3 color;

void main() {
  gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
  color = colors[gl_VertexIndex];
  position = positions[gl_VertexIndex];
}
