#pragma once

#include <vector>

#include "Core/Diagnostics/StructuredError.hpp"
#include "Presentation/Operators/SceneOperator.hpp"
#include "Renderer/Path/CurvedArrowParameters.hpp"
#include "Renderer/Scene/SceneObject.hpp"

namespace DefectStudio
{
	struct RendererWindowState;

	enum class CurvedArrowSelectionMode
	{
		TwoEnds,
		Bond,
		Cycle
	};

	// Shared by creation and the operator's parameter relevance rule; ignores stale atom indices.
	[[nodiscard]] CurvedArrowSelectionMode ResolveCurvedArrowSelectionMode(
		const RendererWindowState &window, CurvedArrowAxisMode axisMode);

	// Two atoms make bond-axis arrows; other two ends make one arrow; three or more atoms make
	// a positive cycle. One undo step for the entire selection.
	//
	// With the default parameters two ends give a C_2 ring about the bond they share - the arc lies
	// in the plane perpendicular to the bond, centred on its midpoint, and the path's transform is
	// bound to that bond so the ring follows the atoms. Three or more ends keep the cycle about the
	// defect z exactly as before. `CurvedArrowAxisMode::DefectZ` asks for the old two-end behaviour.
	[[nodiscard]] Result<std::vector<SceneObjectId>> AddCurvedArrowThroughSelectedAtoms(
		RendererWindowState &windowState,
		const CurvedArrowParameters &parameters = {},
		SceneOperationUndo undo = SceneOperationUndo::Push);
}
