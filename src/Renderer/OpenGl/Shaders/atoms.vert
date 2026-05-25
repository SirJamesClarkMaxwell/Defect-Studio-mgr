#version 430 core

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec4 aInstancePositionRadius;
layout(location = 3) in vec4 aInstanceColor;

uniform mat4 u_ViewProjection;

out vec3 vNormal;
out vec4 vColor;

void main()
{
    vec3 worldPosition = aPosition * aInstancePositionRadius.w + aInstancePositionRadius.xyz;
    gl_Position = u_ViewProjection * vec4(worldPosition, 1.0);
    vNormal = normalize(aNormal);
    vColor = aInstanceColor;
}

