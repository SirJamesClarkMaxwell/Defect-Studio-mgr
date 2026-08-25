#version 430 core

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in float aSign;

uniform mat4 u_ViewProjection;
// RendererWindowState::viewOffset - atoms/bonds get this baked directly into their positions
// (StructureRendererDataBuilder/gizmo drag share that field already), but the orbital grid is a
// separate WAVECAR-derived pipeline with no such position to nudge, so it gets the same value
// here instead. Translate-only, so it doesn't touch aNormal.
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
