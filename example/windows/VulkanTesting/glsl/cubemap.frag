#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec4 color;
layout(location = 1) in vec4 pos;
layout(location = 0) out vec4 outColor;
layout(set = 0, binding = 1) uniform samplerCube cubemap;

void main() {
	vec3 n = normalize(pos.xyz);

    //outColor = color;
	outColor = texture(cubemap, n);
}