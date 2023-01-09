#version 450
#include"../position/glsl.h"
layout(binding=0) uniform MyUniformBufferObject {
	mat4 proj;
	ivec3 cameraPos;
} ubo;
layout(location = 0) out vec2 outTexPos;

void main() {
	const float width = 16;
	const float halfWidth = width/2;
	const int vi= gl_VertexIndex;
	const bool addX = vi == 1 || vi == 3;
	const bool addY = vi == 2 || vi == 3;
	outTexPos= vec2(addX, addY);
	gl_Position= ubo.proj * vec4(vec3(
		addX ? halfWidth : -halfWidth,
		addY ? halfWidth : -halfWidth,
		0
	) - positionScale * ubo.cameraPos, 1);
}
