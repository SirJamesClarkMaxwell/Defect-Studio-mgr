#version 430 core

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec4 aColor;

uniform mat4 u_ViewProjection;
uniform vec3 u_SceneOffset;
uniform vec3 u_CameraPosition;
uniform int u_OutlineMode;
uniform float u_OutlineExpansion;
uniform int u_MeshOverlayMode;

out vec3 vNormal;
out vec4 vColor;
out vec3 vWorldPos;

void main()
{
	vec4 world = vec4(aPosition + u_SceneOffset, 1.0);
	if (u_OutlineMode == 1)
	{
		vec3 normal = normalize(aNormal);
		vec3 viewDirection = normalize(u_CameraPosition - world.xyz);
		vec3 silhouetteNormal = normal - viewDirection * dot(normal, viewDirection);
		world.xyz += silhouetteNormal * u_OutlineExpansion;
	}
	gl_Position = u_ViewProjection * world;
	if (u_OutlineMode == 1)
		gl_Position.z += 0.0001 * gl_Position.w;
	else if (u_MeshOverlayMode == 1)
		gl_Position.z -= 0.0002 * gl_Position.w;
	vNormal = aNormal;
	vColor = aColor;
	vWorldPos = world.xyz;
}
