#pragma once

#include <optional>

#include <glm/glm.hpp>

#include "Renderer/Path/PathStyle.hpp"

namespace DefectStudio
{
	// Which line the arrow turns about. Auto is what the Add menu passes: two ends mean a C_2 about
	// the bond they share, three or more mean the defect z, which is what the cycle already did.
	enum class CurvedArrowAxisMode
	{
		Auto,
		Bond,
		DefectZ
	};

	// A ring around a bond has no radius of its own - the bond fixes only the axis and the centre -
	// so the radius has to come from somewhere. AtomRelative keeps the ring clear of the spheres as
	// they are resized; BondFraction keeps it proportional to the bond instead.
	enum class CurvedArrowRadiusRule
	{
		AtomRelative,
		BondFraction
	};

	struct CurvedArrowParameters
	{
		CurvedArrowAxisMode axisMode = CurvedArrowAxisMode::Auto;
		CurvedArrowRadiusRule radiusRule = CurvedArrowRadiusRule::AtomRelative;
		// x the larger of the two atom radii, or x the bond length, per radiusRule.
		float radiusFactor = 1.4f;
		// Equal arcs around a bond (1..6), or opposite directed non-bond arcs (1..2).
		int arrowCount = 2;
		float sweepDegrees = 150.0f;
		// Where the arc starts, measured about the axis. The panel field and the modal rotate both
		// write this one value, so the two routes cannot drift apart.
		float rotationDegrees = 0.0f;
		// Non-bond sagitta multiplier; endpoints retain their atom/vacancy clearance.
		float radiusScale = 1.0f;
		std::optional<float> endGap; // surface clearance in atom radii
		// Non-bond sweep = curvature x the angle between the ends. Curvature is clamped to [0.05, 1.5].
		// Half a C_3 step gives the existing 60-degree arc (sagitta/chord about 0.134).
		float curvature = 0.5f;
		// Rotate the non-bond arc plane about its chord, leaving both bound ends unchanged.
		float tiltDegrees = 0.0f;
		PathDecorationKind decoration = PathDecorationKind::Arrow;
		glm::vec3 color{1.0f, 0.27f, 0.0f};
		// Today's value. This task does not retune the stroke or the decoration scales.
		float strokeWidth = 0.03f;
	};
}
