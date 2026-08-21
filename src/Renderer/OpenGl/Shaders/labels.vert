#version 430 core

// Shared unit quad (0,0)-(1,1), one instance per glyph.
layout(location = 0) in vec2 aVertexPosition;
layout(location = 1) in vec3 aWorldCenter;
layout(location = 2) in vec4 aLocalOffsetSize; // xy = offset, zw = size (label-local em units)
layout(location = 3) in vec4 aAtlasUvMinMax; // xy = uvMin, zw = uvMax
layout(location = 4) in vec4 aColor;

uniform mat4 u_ViewProjection;
uniform mat4 u_View;

out vec2 vUv;
out vec4 vColor;

void main()
{
	// Camera-facing billboard: right/up are the view matrix's own basis vectors, so the quad
	// always faces the camera regardless of orbit/roll - same trick used for the box/circle-select
	// screen-space overlays, just applied per-glyph here instead of in ImGui draw-list space.
	vec3 cameraRight = vec3(u_View[0][0], u_View[1][0], u_View[2][0]);
	vec3 cameraUp = vec3(u_View[0][1], u_View[1][1], u_View[2][1]);

	vec2 localPosition = aLocalOffsetSize.xy + aVertexPosition * aLocalOffsetSize.zw;
	vec3 worldPosition = aWorldCenter + cameraRight * localPosition.x + cameraUp * localPosition.y;

	gl_Position = u_ViewProjection * vec4(worldPosition, 1.0);
	vUv = mix(aAtlasUvMinMax.xy, aAtlasUvMinMax.zw, aVertexPosition);
	vColor = aColor;
}
