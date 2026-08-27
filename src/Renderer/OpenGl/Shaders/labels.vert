#version 430 core

// Shared unit quad (0,0)-(1,1), one instance per glyph.
layout(location = 0) in vec2 aVertexPosition;
layout(location = 1) in vec3 aWorldCenter;
layout(location = 2) in vec4 aLocalOffsetSize; // xy = offset, zw = size (label-local em units)
layout(location = 3) in vec4 aAtlasUvMinMax; // xy = uvMin, zw = uvMax
layout(location = 4) in vec4 aColor;
layout(location = 5) in float aRotationRadians; // in-plane rotation within the billboard's own basis
// aOutlineColor/aOutlineWidth/aCornerRadius style the label_background pass's rounded-rect quad
// (label_background.frag) - unused by labels.frag's glyph rendering, but shared here since both
// instance kinds go through this one vertex shader/instance layout.
layout(location = 6) in vec3 aOutlineColor;
layout(location = 7) in float aOutlineWidth;
layout(location = 8) in float aCornerRadius;
// aStrokeColor/aStrokeWidth style labels.frag's own glyph MSDF stroke - unused by the
// label_background pass, same sharing rationale as aOutlineColor/aOutlineWidth/aCornerRadius above.
layout(location = 9) in vec3 aStrokeColor;
layout(location = 10) in float aStrokeWidth;

uniform mat4 u_ViewProjection;
uniform mat4 u_View;
// Non-destructive whole-structure reposition (RendererWindowState::viewOffset, export-preview-only
// as of Etap F Phase 1) - covers free labels, bond/angle labels, and arrow-attached labels alike,
// since they all go through this one shader.
uniform vec3 u_SceneOffset;

out vec2 vUv;
out vec4 vColor;
out vec3 vOutlineColor;
out float vOutlineWidth;
// Local rect-space position/half-size/radius for label_background.frag's rounded-rect SDF - in the
// same post-scale world units as aLocalOffsetSize.zw, centered on the quad's own middle (not the
// glyph billboard's rotation/offset math above, which stays untouched by this).
out vec2 vLocalPos;
out vec2 vBoxHalfSize;
out float vCornerRadius;
out vec3 vStrokeColor;
out float vStrokeWidth;

void main()
{
	// Camera-facing billboard: right/up are the view matrix's own basis vectors, so the quad
	// always faces the camera regardless of orbit/roll - same trick used for the box/circle-select
	// screen-space overlays, just applied per-glyph here instead of in ImGui draw-list space.
	vec3 cameraRight = vec3(u_View[0][0], u_View[1][0], u_View[2][0]);
	vec3 cameraUp = vec3(u_View[0][1], u_View[1][1], u_View[2][1]);

	vec2 localPosition = aLocalOffsetSize.xy + aVertexPosition * aLocalOffsetSize.zw;
	// Rotate within the billboard plane (e.g. to align a bond-length label with the bond's on-
	// screen direction) before projecting onto camera right/up - a 2D rotation of the in-plane
	// coordinates is equivalent to rotating the plane itself around its own normal (the view dir).
	float cosR = cos(aRotationRadians);
	float sinR = sin(aRotationRadians);
	vec2 rotatedPosition = vec2(
		localPosition.x * cosR - localPosition.y * sinR,
		localPosition.x * sinR + localPosition.y * cosR);
	vec3 worldPosition = aWorldCenter + u_SceneOffset + cameraRight * rotatedPosition.x + cameraUp * rotatedPosition.y;

	gl_Position = u_ViewProjection * vec4(worldPosition, 1.0);
	vUv = mix(aAtlasUvMinMax.xy, aAtlasUvMinMax.zw, aVertexPosition);
	vColor = aColor;
	vOutlineColor = aOutlineColor;
	vOutlineWidth = aOutlineWidth;
	vLocalPos = (aVertexPosition - 0.5) * aLocalOffsetSize.zw;
	vBoxHalfSize = aLocalOffsetSize.zw * 0.5;
	vCornerRadius = aCornerRadius;
	vStrokeColor = aStrokeColor;
	vStrokeWidth = aStrokeWidth;
}
