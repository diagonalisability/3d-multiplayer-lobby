#version 450
layout(location=0) in vec2 texPos;
layout(location=0) out vec4 outColour;

void main() {
	const float subdivisionsPerUnitDistance= 4.f;
	const float width= 16;
	const float grey0= 0.3;
	const float grey1= 0.4;
	const vec3 colours[]= {
		{grey0, grey0, grey0},
		{grey1, grey1, grey1}
	};
	const float multiplier= width * subdivisionsPerUnitDistance;
	outColour= vec4(colours[
		int(mod(multiplier * texPos.x, 2) < 1)
		^ int(mod(multiplier * texPos.y, 2) < 1)
	], 1);
}
