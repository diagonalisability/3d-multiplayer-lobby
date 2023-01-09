#version 450
#include"../position/glsl.h"
layout(set=0, binding=0) uniform MyUniformBufferObject {
	mat4 proj;
	ivec3 cameraPos;
} ubo;
layout(location=0) in vec3 vertPos;
layout(location=1) in vec2 inTexPos;
layout(location=2) in ivec3 instancePos;
layout(location=3) in vec4 instanceOrientation;
layout(location=0) out vec2 outTexPos;

void main() {
	gl_Position= ubo.proj * vec4(
		vertPos + positionScale*(instancePos - ubo.cameraPos),
		1.0
	);
	outTexPos= inTexPos;
}
