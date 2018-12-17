#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec4 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 texcoord;

layout(location = 0) out vec3 va_normal;

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
	vec3 diffuseColor;
} ubo;

out gl_PerVertex {
    vec4 gl_Position;
};
	
void main() 
{
	mat4 modelViewMatrix = ubo.view * ubo.model;
	mat3 normalMatrix = transpose(inverse(mat3(modelViewMatrix)));

	va_normal = normalize(normalMatrix * normal);
    gl_Position = ubo.proj * ubo.view * ubo.model * position;
}