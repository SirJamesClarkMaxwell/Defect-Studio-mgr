#version 430 core

layout(location = 0) in vec3 aPosition;
uniform mat4 u_ViewProjection;

void main()
{
    gl_Position = u_ViewProjection * vec4(aPosition, 1.0);
}

