#version 430 core

layout(location = 0) in vec3 aPosition;
uniform mat4 u_ViewProjection;
// Non-destructive whole-structure reposition (RendererWindowState::viewOffset, export-preview-only
// as of Etap F Phase 1) - keeps the ground grid rigidly attached to atoms/bonds under the same offset.
uniform vec3 u_SceneOffset;

void main()
{
    gl_Position = u_ViewProjection * vec4(aPosition + u_SceneOffset, 1.0);
}

