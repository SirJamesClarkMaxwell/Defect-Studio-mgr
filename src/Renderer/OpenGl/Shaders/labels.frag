#version 430 core

in vec2 vUv;
in vec4 vColor;

uniform sampler2D u_AtlasTexture;
uniform float u_PixelRange;

out vec4 FragColor;

// Standard msdfgen sampling technique (Chlumsky, msdfgen wiki "Using a rendered MSDF") - median
// of the 3 channels reconstructs a true signed distance even across sharp glyph corners, then
// screenPxRange() converts the atlas' fixed pixel-range into this fragment's actual on-screen
// pixel scale (via fwidth) so edges stay crisp at any zoom instead of just at the baked em-size.
float median(float r, float g, float b)
{
	return max(min(r, g), min(max(r, g), b));
}

float screenPxRange()
{
	vec2 unitRange = vec2(u_PixelRange) / vec2(textureSize(u_AtlasTexture, 0));
	vec2 screenTexSize = vec2(1.0) / fwidth(vUv);
	return max(0.5 * dot(unitRange, screenTexSize), 1.0);
}

void main()
{
	vec3 msdfSample = texture(u_AtlasTexture, vUv).rgb;
	float signedDistance = median(msdfSample.r, msdfSample.g, msdfSample.b) - 0.5;
	float screenPxDistance = screenPxRange() * signedDistance;
	float opacity = clamp(screenPxDistance + 0.5, 0.0, 1.0);

	if (opacity < 0.01)
		discard;

	FragColor = vec4(vColor.rgb, vColor.a * opacity);
}
