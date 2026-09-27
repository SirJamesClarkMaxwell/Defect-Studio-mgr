#version 430 core

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec4 aInstancePositionRadius;
layout(location = 3) in vec4 aInstanceColor;
layout(location = 4) in float aSelected;
layout(location = 5) in float aOutlineExpansion;

uniform mat4 u_ViewProjection;
// Non-destructive whole-structure reposition (RendererWindowState::viewOffset, export-preview-only
// as of Etap F Phase 1) - render-time-only, never baked into aInstancePositionRadius itself.
uniform vec3 u_SceneOffset;
uniform int u_OutlineMode;

out vec3 vNormal;
out vec4 vColor;
out vec3 vWorldPos;
out float vSelected;

void main()
{
	float radius = aInstancePositionRadius.w + (u_OutlineMode == 1 ? aOutlineExpansion : 0.0);
	vec3 worldPosition = aPosition * radius
		+ aInstancePositionRadius.xyz + u_SceneOffset;
	gl_Position = u_ViewProjection * vec4(worldPosition, 1.0);
	vNormal = normalize(aNormal);
	vColor = aInstanceColor;
	vWorldPos = worldPosition;
	vSelected = aSelected;
}
