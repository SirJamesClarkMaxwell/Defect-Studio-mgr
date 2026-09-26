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

	// The shape family of an endpoint decoration. Each is an axial contour (see PathDecoration.hpp),
	// not a hardcoded triangle count, so Round revolves it and Flat mirrors it from one description.
	//
	// task/41 S11i: this is the manim/TikZ tip vocabulary. Deliberately NOT one enumerator per named
	// tip - manim spells filled and hollow variants as separate classes (ArrowCircleTip vs
	// ArrowCircleFilledTip) and that doubles the list for a property that is a boolean. Here the
	// family is the kind and `PathEndpointDecoration::filled` picks solid or outline, which yields
	// the whole cross product and keeps persistence to one name per shape.
	//
	// `OpenArrow` is gone for the same reason: it was Arrow with filled == false, spelled as a
	// third thing, and it was the one kind whose `filled` the mesher did not honour. Files that
	// name it migrate to { Arrow, filled = false } - see ScenePathPersistence.
	//
	// Legacy ArrowTip mapping, still needed until S16 removes SceneArrowGeometry:
	// Plain == Arrow, Barbed == Stealth, Open == Arrow with filled == false.
	enum class PathDecorationKind
	{
		None,
		Arrow,   // straight-backed triangle. TikZ `>` / `to`, manim ArrowTriangleTip.
		Stealth, // back notched forward, so the barbs trail. TikZ `stealth`, manim StealthTip.
		Latex,   // sides bow outward and the back is swept. TikZ `latex`.
		Bar,     // a tee across the tangent, no length to speak of. TikZ `|`.
		Circle,  // a disc; the contour is a half-circle, so it reads round at any zoom.
		Square,  // a box of constant half-width. It must NOT taper - that was the S11 defect.
		Diamond, // widest at the middle, symmetric front to back.
		Kite,    // widest a third of the way back: a Diamond with a longer tail.
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
	// a decoration keeps its shape when the stroke is made thicker. The two scales are the shape
	// control the user asked for: a long thin Arrow and a stubby one are the same `kind`.
	struct PathEndpointDecoration
	{
		PathDecorationKind kind = PathDecorationKind::None;
		float lengthScale = 1.0f;
		float widthScale = 1.0f;
		// Solid body, or an outline of the same contour. False is what `OpenArrow` used to mean, and
		// it applies to every kind, not just Arrow. The outline's thickness is the stroke width, so a
		// hollow tip carries the same visual weight as the shaft it terminates.
		bool filled = true;
	};

	struct PathStrokeStyle
	{
		StrokeProfile profile = StrokeProfile::Round;
		// Which way a Flat ribbon's sheet faces. Seeds the transported frame; ignored by Round and
		// CameraFacing, which choose their own rotational axis.
		glm::vec3 ribbonNormal{0.0f, 1.0f, 0.0f};
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
