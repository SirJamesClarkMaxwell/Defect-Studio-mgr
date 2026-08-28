#version 430 core

// Flat world-space quad for SceneArrow's Arrow2D kind. Unlike labels.vert, this has no camera-basis
// math of its own - OpenGlRendererBackend::renderSceneArrows/ComputeArrowQuadBasis already resolves
// aRight/aUp per instance on the CPU (camera-facing-plane projection of the arrow direction for
// Billboard orientation, fixed world-plane axes for FixedPlane), so both orientations share this one
// "dumb" shader. Paired with arrow_quad.frag, which turns vLocalPos/vBoxHalfSize/vHead* into a
// shaft-rect + head-triangle SDF union.
layout(location = 0) in vec2 aVertexPosition;
layout(location = 1) in vec3 aWorldCenter;
layout(location = 2) in vec3 aRight;
layout(location = 3) in vec3 aUp;
layout(location = 4) in vec2 aHalfSize;
layout(location = 5) in vec4 aColor;
layout(location = 6) in vec3 aOutlineColor;
layout(location = 7) in float aOutlineWidth;
layout(location = 8) in float aHeadHalfWidth;
layout(location = 9) in float aHeadLength;

uniform mat4 u_ViewProjection;
uniform vec3 u_SceneOffset;

out vec2 vLocalPos;
out vec2 vBoxHalfSize;
out float vHeadHalfWidth;
out float vHeadLength;
out vec3 vOutlineColor;
out float vOutlineWidth;
out vec4 vColor;

void main()
{
	// +x runs from the arrow's start (-aHalfSize.x) to its end (+aHalfSize.x) - aRight is
	// normalize(end - start), see ComputeArrowQuadBasis - so the head triangle in arrow_quad.frag
	// (apex at +aHalfSize.x) always sits at the arrow's actual end, not its start.
	vec2 localPosition = (aVertexPosition - 0.5) * aHalfSize * 2.0;
	vec3 worldPosition = aWorldCenter + u_SceneOffset + aRight * localPosition.x + aUp * localPosition.y;

	gl_Position = u_ViewProjection * vec4(worldPosition, 1.0);
	vLocalPos = localPosition;
	vBoxHalfSize = aHalfSize;
	vHeadHalfWidth = aHeadHalfWidth;
	vHeadLength = aHeadLength;
	vOutlineColor = aOutlineColor;
	vOutlineWidth = aOutlineWidth;
	vColor = aColor;
}
