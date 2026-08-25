#version 430 core

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in float aSign;

uniform mat4 u_ViewProjection;

out vec3 vNormal;
out float vSign;
out vec3 vWorldPos;

void main()
{
	gl_Position = u_ViewProjection * vec4(aPosition, 1.0);
	vNormal = normalize(aNormal);
	vSign = aSign;
	vWorldPos = aPosition; // no model matrix here - aPosition already is world space
}
