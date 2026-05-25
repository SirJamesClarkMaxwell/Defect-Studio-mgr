#version 430 core

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in float aGradientT;
layout(location = 3) in vec4 aModelCol0;
layout(location = 4) in vec4 aModelCol1;
layout(location = 5) in vec4 aModelCol2;
layout(location = 6) in vec4 aModelCol3;
layout(location = 7) in vec4 aColorA;
layout(location = 8) in vec4 aColorB;

uniform mat4 u_ViewProjection;

out vec3 vNormal;
out vec4 vColorA;
out vec4 vColorB;
out float vGradientT;

void main()
{
    mat4 model = mat4(aModelCol0, aModelCol1, aModelCol2, aModelCol3);
    vec4 worldPosition = model * vec4(aPosition, 1.0);
    gl_Position = u_ViewProjection * worldPosition;

    mat3 normalMatrix = mat3(model);
    vNormal = normalize(normalMatrix * aNormal);
    vColorA = aColorA;
    vColorB = aColorB;
    vGradientT = aGradientT;
}

