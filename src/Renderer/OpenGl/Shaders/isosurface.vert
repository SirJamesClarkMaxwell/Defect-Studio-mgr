#version 430 core

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in float aSign;

uniform mat4 u_ViewProjection;
// Non-destructive whole-scene reposition (RendererWindowState::viewOffset, export-preview-only as
// of Etap F Phase 1) - every geometry pass (atoms/bonds/cell box/grid/labels/isosurface) applies
// this same value via its own u_SceneOffset uniform, so the whole scene stays rigidly attached
// under it. Translate-only, so it doesn't touch aNormal.
uniform vec3 u_SceneOffset;

out vec3 vNormal;
out float vSign;
out vec3 vWorldPos;

void main()
{
	vec3 worldPosition = aPosition + u_SceneOffset;
	gl_Position = u_ViewProjection * vec4(worldPosition, 1.0);
	vNormal = normalize(aNormal);
	vSign = aSign;
	vWorldPos = worldPosition;
}
