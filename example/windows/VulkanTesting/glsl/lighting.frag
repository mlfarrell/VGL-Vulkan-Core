#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec3 va_normal;
layout(location = 0) out vec4 fragColor;

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
	vec3 diffuseColor;
} ubo;

void main() {
  vec3 eyeNormal = normalize(va_normal);
  vec3 lightDir = vec3(0.0, 0.0, 1.0);
  float nDotVP = max(0.0, dot(eyeNormal, normalize(lightDir)));
  
  fragColor = vec4(ubo.diffuseColor, 1.0) * nDotVP;
}
