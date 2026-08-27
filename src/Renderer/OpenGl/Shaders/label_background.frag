#version 430 core

// Paired with labels.vert (same billboard positioning/instance layout, see
// OpenGlRendererBackend::AppendLabelBackgroundInstance) but skips the MSDF atlas sampling entirely -
// a label's background is a flat-shaded rounded rect with an optional border stroke, not glyph
// geometry.
in vec2 vLocalPos;
in vec2 vBoxHalfSize;
in float vCornerRadius;
in vec3 vOutlineColor;
in float vOutlineWidth;
in vec4 vColor;

out vec4 FragColor;

// Inigo Quilez's rounded-box SDF (iquilezles.org/articles/distfunctions2d) - p relative to the
// box's own center, radius clamped so opposite corners can never overlap.
float roundedRectSdf(vec2 p, vec2 halfSize, float radius)
{
	radius = min(radius, min(halfSize.x, halfSize.y));
	vec2 q = abs(p) - halfSize + radius;
	return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - radius;
}

void main()
{
	float dist = roundedRectSdf(vLocalPos, vBoxHalfSize, vCornerRadius);
	// fwidth() of the SDF value itself, not of screen UV - standard SDF anti-aliasing, scales
	// correctly regardless of how far the label is from the camera.
	float aa = max(fwidth(dist), 0.0001);
	float fillMask = 1.0 - smoothstep(-aa, aa, dist);
	if (fillMask < 0.01)
		discard;

	vec3 rgb = vColor.rgb;
	// No border (the common case, LabelStyle::outlineWidth == 0 by default): skip the blend
	// entirely rather than fold width=0 into the formula below - mix(vOutlineColor, vColor.rgb,
	// coreMask) would tint edge pixels toward vOutlineColor (black by default) even at width 0,
	// same class of fringe bug the glyph MSDF outline had to avoid (see labels.frag history).
	if (vOutlineWidth > 0.0)
	{
		float insideCoreDist = dist + vOutlineWidth;
		float coreMask = 1.0 - smoothstep(-aa, aa, insideCoreDist);
		rgb = mix(vOutlineColor, vColor.rgb, coreMask);
	}

	FragColor = vec4(rgb, vColor.a * fillMask);
}
