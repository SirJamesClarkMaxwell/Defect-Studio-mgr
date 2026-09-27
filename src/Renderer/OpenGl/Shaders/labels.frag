#version 430 core

in vec2 vUv;
in vec4 vColor;
in vec3 vStrokeColor;
in float vStrokeWidth;
in float vSelected;

uniform sampler2D u_AtlasTexture;
uniform float u_PixelRange;
uniform vec4 u_SelectionOutlineColor;
uniform float u_SelectionOutlineWidth;

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
	// screenPxDistance is already in actual on-screen pixels (that's what screenPxRange() converts
	// to) - a change of 1.0 here is a 1px shift of the antialiased edge, same unit the "+0.5" ramp
	// below already relies on. vStrokeWidth is therefore also plain screen pixels, not normalized
	// SDF/median units - constant visible thickness regardless of zoom or glyph size, instead of
	// shrinking to sub-pixel invisibility on a small on-screen label the way a median-space width
	// would (screenPxRange scales with on-screen glyph size, so a fixed median-space width doesn't).
	float screenPxDistance = screenPxRange() * signedDistance;
	float fillOpacity = clamp(screenPxDistance + 0.5, 0.0, 1.0);
	float ownOpacity = fillOpacity;

	// No stroke (the common case, LabelStyle::strokeWidth == 0 by default): skip the blend entirely
	// rather than fold width=0 into the formula below - mix(vStrokeColor, vColor.rgb, fillOpacity)
	// would tint anti-aliased edge pixels toward vStrokeColor (black by default) even at width 0, same
	// class of fringe bug label_background.frag's border already guards against the same way.
	if (vStrokeWidth <= 0.0)
	{
		ownOpacity = fillOpacity;
	}
	else
		ownOpacity = clamp(screenPxDistance + vStrokeWidth + 0.5, 0.0, 1.0);
	float selectionOpacity = vSelected > 0.5 && u_SelectionOutlineWidth > 0.0
		? clamp(screenPxDistance + u_SelectionOutlineWidth + 0.5, 0.0, 1.0)
		: 0.0;
	if (max(ownOpacity, selectionOpacity) < 0.01)
		discard;
	vec3 ownRgb = vStrokeWidth > 0.0 ? mix(vStrokeColor, vColor.rgb, fillOpacity) : vColor.rgb;
	float ownAlpha = vColor.a * ownOpacity;
	float selectionAlpha = u_SelectionOutlineColor.a * selectionOpacity;
	vec3 rgb = selectionAlpha > ownAlpha ? u_SelectionOutlineColor.rgb : ownRgb;
	FragColor = vec4(rgb, max(ownAlpha, selectionAlpha));
}
