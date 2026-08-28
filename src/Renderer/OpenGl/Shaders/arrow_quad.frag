#version 430 core

// Paired with arrow_quad.vert. Forked from label_background.frag rather than editing it in place -
// a real label background is always a plain rounded rect, it never needs a head triangle, so this
// stays a separate (small) shader instead of growing branches into the shared one.
in vec2 vLocalPos;
in vec2 vBoxHalfSize;
in float vHeadHalfWidth;
in float vHeadLength;
in vec3 vOutlineColor;
in float vOutlineWidth;
in vec4 vColor;

out vec4 FragColor;

// Inigo Quilez's plain box SDF (iquilezles.org/articles/distfunctions2d) - sharp corners, no
// rounding. Used (unioned with a circle, see main()) instead of a symmetric rounded-rect for the
// shaft: rounding all 4 corners equally left the head-side corners rounded too, and unioning that
// with the head triangle's flat base showed a visible concave notch right at the shoulder wherever
// headHalfWidth > vBoxHalfSize.y (the usual case) - the rounded corner cut inward before the wider
// triangle base "caught up". A sharp box has no such corner to cut into.
float boxSdf(vec2 p, vec2 halfSize)
{
	vec2 d = abs(p) - halfSize;
	return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0);
}

// Inigo Quilez's signed distance to a 2D triangle (iquilezles.org/articles/distfunctions2d) -
// negative inside, matching roundedRectSdf's convention above so the two can be min()'d together.
float triangleSdf(vec2 p, vec2 p0, vec2 p1, vec2 p2)
{
	vec2 e0 = p1 - p0, e1 = p2 - p1, e2 = p0 - p2;
	vec2 v0 = p - p0, v1 = p - p1, v2 = p - p2;
	vec2 pq0 = v0 - e0 * clamp(dot(v0, e0) / dot(e0, e0), 0.0, 1.0);
	vec2 pq1 = v1 - e1 * clamp(dot(v1, e1) / dot(e1, e1), 0.0, 1.0);
	vec2 pq2 = v2 - e2 * clamp(dot(v2, e2) / dot(e2, e2), 0.0, 1.0);
	float s = sign(e0.x * e2.y - e0.y * e2.x);
	vec2 d = min(min(vec2(dot(pq0, pq0), s * (v0.x * e0.y - v0.y * e0.x)),
					  vec2(dot(pq1, pq1), s * (v1.x * e1.y - v1.y * e1.x))),
					  vec2(dot(pq2, pq2), s * (v2.x * e2.y - v2.y * e2.x)));
	return -sqrt(d.x) * sign(d.y);
}

void main()
{
	// Local +x runs tail (-vBoxHalfSize.x) to tip (+vBoxHalfSize.x) - see arrow_quad.vert. The shaft
	// occupies everything left of the head's base; the head is a triangle from that base to the tip.
	// Shaft = sharp box unioned with a circle capping only the tail (a "one-sided capsule") - see
	// boxSdf's comment for why this replaced a symmetric roundedRectSdf. The circle's radius equals
	// the box's half-height and its center sits exactly one radius in from the tail tip, so the two
	// pieces meet with zero gap/overlap at y=+-vBoxHalfSize.y (the box's left edge and the circle's
	// widest point coincide there by construction, same as any capsule/stadium shape).
	float shaftMaxX = vBoxHalfSize.x - vHeadLength;
	float tailRadius = vBoxHalfSize.y;
	float tailCenterX = -vBoxHalfSize.x + tailRadius;
	float rectHalfLength = max((shaftMaxX - tailCenterX) * 0.5, 0.0001);
	float rectCenterX = tailCenterX + rectHalfLength;
	float dist = min(
		boxSdf(vLocalPos - vec2(rectCenterX, 0.0), vec2(rectHalfLength, vBoxHalfSize.y)),
		length(vLocalPos - vec2(tailCenterX, 0.0)) - tailRadius);

	if (vHeadLength > 0.0001)
	{
		float headDist = triangleSdf(
			vLocalPos,
			vec2(shaftMaxX, -vHeadHalfWidth),
			vec2(shaftMaxX, vHeadHalfWidth),
			vec2(vBoxHalfSize.x, 0.0));
		dist = min(dist, headDist);
	}

	// fwidth() of the SDF value itself, not of screen UV - standard SDF anti-aliasing, scales
	// correctly regardless of how far the arrow is from the camera.
	float aa = max(fwidth(dist), 0.0001);
	float fillMask = 1.0 - smoothstep(-aa, aa, dist);
	if (fillMask < 0.01)
		discard;

	vec3 rgb = vColor.rgb;
	// No outline (the common case, ArrowStyle::outlineWidth == 0 by default): skip the blend
	// entirely rather than fold width=0 into the formula below, same fringe-avoidance reasoning as
	// label_background.frag's own border handling.
	if (vOutlineWidth > 0.0)
	{
		float insideCoreDist = dist + vOutlineWidth;
		float coreMask = 1.0 - smoothstep(-aa, aa, insideCoreDist);
		rgb = mix(vOutlineColor, vColor.rgb, coreMask);
	}

	FragColor = vec4(rgb, vColor.a * fillMask);
}
