#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec4 inPosition;
layout(location = 0) out vec4 color;
layout(location = 1) out vec4 pos;

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
} ubo;

out gl_PerVertex {
    vec4 gl_Position;
};

float rand(float n) 
{
	return fract(sin(n) * 43758.5453123);
}

float noise(float p)
{
	float fl = floor(p);
	float fc = fract(p);
	return mix(rand(fl), rand(fl + 1.0), fc);
}
	
void main() 
{
  gl_Position = ubo.proj * ubo.view * ubo.model * inPosition;
	pos = ubo.model * inPosition;
	color = vec4(noise(gl_VertexIndex), noise(gl_VertexIndex+1), noise(gl_VertexIndex+2), 1.0);
}
