#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

namespace DefectStudio
{
	// How the stroke occupies space. Round is a real swept tube and is the only profile that reads as
	// a solid object from any angle; the two flat profiles are ribbons expanded in the shader (S7), so
	// everything the CPU produces for them is camera-independent by construction.
	enum class StrokeProfile
	{
		Round,
		Flat,         // ribbon lying in the transported frame's normal plane
		CameraFacing, // ribbon expanded towards the eye at draw time
	};

	enum class PathLineJoin
	{
		Bevel,
		Round,
	};

	enum class PathLineCap
	{
		Butt,
		Square,
		Round,
	};

	enum class PathDepthMode
	{
		DepthTest,
		AlwaysOnTop,
	};

	// Eight kinds, mapping onto the legacy ArrowTip vocabulary where one exists: Arrow == Plain,
	// Stealth == Barbed, OpenArrow == Open. The legacy table stays in SceneArrowGeometry until S16;
	// here a decoration is an axial contour, not a hardcoded triangle count.
	enum class PathDecorationKind
	{
		None,
		Arrow,
		Stealth,
		OpenArrow,
		Bar,
		Circle,
		Square,
		Diamond,
	};

	// World-space dashes. Lengths are absolute so a dash does not change size when the path is
	// lengthened, and the pattern runs over the whole path arc length rather than per segment - that
	// is what keeps the phase continuous across a Line to Arc boundary.
	struct PathDashStyle
	{
		bool enabled = false;
		float dashLength = 0.1f;
		float gapLength = 0.05f;
		float phase = 0.0f; // world-space offset into the pattern, applied at arc length 0
	};

	struct PathGradientStop
	{
		float position = 0.0f; // normalised arc length, [0, 1]
		glm::vec3 color{1.0f};
		float alpha = 1.0f;
	};

	// Sampled over normalised arc length of the whole path, so it does not restart at a segment.
	// Stops must be finite and non-decreasing in `position`; anything else is a rejection, not a
	// sort-and-hope.
	struct PathGradient
	{
		bool enabled = false;
		std::vector<PathGradientStop> stops;
	};

	// Scales are relative to the stroke width, matching the legacy ArrowTipParameters proportions, so
	// a decoration keeps its shape when the stroke is made thicker.
	struct PathEndpointDecoration
	{
		PathDecorationKind kind = PathDecorationKind::None;
		float lengthScale = 1.0f;
		float widthScale = 1.0f;
	};

	struct PathStrokeStyle
	{
		StrokeProfile profile = StrokeProfile::Round;
		float width = 0.05f; // full width; the tube radius is half of it
		PathLineJoin join = PathLineJoin::Bevel;
		PathLineCap cap = PathLineCap::Butt;
		std::uint32_t radialSegments = 12; // Round profile only; minimum 3
		glm::vec3 color{1.0f};
		float alpha = 1.0f;
		PathDashStyle dash;
		PathGradient gradient;
		PathEndpointDecoration startDecoration;
		PathEndpointDecoration endDecoration;
		PathDepthMode depthMode = PathDepthMode::DepthTest;
	};
}
