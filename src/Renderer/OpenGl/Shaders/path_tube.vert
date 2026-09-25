#version 430 core

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec4 aColor;

uniform mat4 u_ViewProjection;
uniform vec3 u_SceneOffset;

out vec3 vNormal;
out vec4 vColor;
out vec3 vWorldPos;

void main()
{
	vec4 world = vec4(aPosition + u_SceneOffset, 1.0);
	gl_Position = u_ViewProjection * world;
	vNormal = aNormal;
	vColor = aColor;
	vWorldPos = world.xyz;
}
