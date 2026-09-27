#version 430 core

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aTangent;
layout(location = 2) in vec3 aNormal;
layout(location = 3) in vec4 aColor;
layout(location = 4) in float aSide;
layout(location = 5) in float aHalfWidth;

uniform mat4 u_ViewProjection;
uniform vec3 u_SceneOffset;
uniform float u_HalfWidth;
uniform int u_CameraFacing;
uniform vec3 u_CameraPosition;
uniform int u_OutlineMode;
uniform float u_OutlineExpansion;

out vec3 vNormal;
out vec4 vColor;
out vec3 vWorldPos;

void main()
{
	vec3 world = aPosition + u_SceneOffset;
	vec3 tangent = normalize(aTangent);
	vec3 offsetDir;
	if (u_CameraFacing == 1)
		offsetDir = cross(tangent, normalize(u_CameraPosition - world));
	else
		// The frame normal itself, not its cross product with the tangent: PathStrokeMesher offsets a
		// ribbon decoration's vertices along exactly this axis on the CPU (AppendDecoration), so the
		// shaft has to widen in the same plane or an arrowhead would stand perpendicular to its shaft.
		offsetDir = normalize(aNormal);
	float offsetLength = length(offsetDir);
	if (offsetLength > 0.000001)
		offsetDir /= offsetLength;
	else
		offsetDir = vec3(0.0);
	float halfWidth = u_CameraFacing == 1 ? aHalfWidth : u_HalfWidth;
	if (u_OutlineMode == 1)
		halfWidth += u_OutlineExpansion;
	world += aSide * halfWidth * offsetDir;
	vNormal = u_CameraFacing == 1 ? normalize(u_CameraPosition - world) : normalize(aNormal);
	vColor = aColor;
	vWorldPos = world;
	gl_Position = u_ViewProjection * vec4(world, 1.0);
}
