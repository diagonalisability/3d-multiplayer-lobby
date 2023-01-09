#version 450
layout(set=0, binding=1) uniform sampler2D texSampler;
layout(location=0) in vec2 texPos;
layout(location=0) out vec4 outColour;

void main() {
	outColour= texture(texSampler, texPos);
}
