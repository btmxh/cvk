#version 450
#pragma shader_stage(fragment)

layout (location = 0) in vec2 position;
layout (location = 1) in vec3 color;
layout (location = 2) in vec2 tex_coords;
layout (location = 0) out vec4 out_color;

float rand(vec2 n) { 
	return fract(sin(dot(n, vec2(12.9898, 4.1414))) * 43758.5453);
}

float noise(vec2 p){
	vec2 ip = floor(p);
	vec2 u = fract(p);
	u = u*u*(3.0-2.0*u);
	
	float res = mix(
		mix(rand(ip),rand(ip+vec2(1.0,0.0)),u.x),
		mix(rand(ip+vec2(0.0,1.0)),rand(ip+vec2(1.0,1.0)),u.x),u.y);
	return res*res;
}

layout(binding = 1) uniform sampler2D tex;

void main() {
  float gray = noise(position * vec2(0.43434343243, 0.5656343434) * 239) * 0.5 + 0.5;
  out_color = vec4(gray, gray, gray, 1.0);
  out_color = vec4(color, 1.0);
  out_color = texture(tex, tex_coords);
}
