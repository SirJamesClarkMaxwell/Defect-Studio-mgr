#pragma once

#include "Core/Diagnostics/StructuredError.hpp"
#include "Renderer/Path/PathTypes.hpp"

namespace DefectStudio
{
	// G1 continuity is an operation, not an invariant: C0 is the only thing the model guarantees
	// (plan v2 C3). MakeTangent is available when at least one neighbouring segment is Cubic.
	//
	// Rules (plan v2 section 3, deterministic, no invented directions):
	//   Cubic-Cubic      opposite rays along normalize(next - prev), handle length = 1/3 of the
	//                    adjacent chord.
	//   Cubic-Line/Arc   the cubic handle aligns to the rigid neighbour's end tangent; the rigid
	//                    segment is never retyped.
	//   Line-Arc, Line-Line, Arc-Arc    a legal corner; rejected with a StructuredError.
	//   degenerate       zero-length chord or a non-finite tangent returns a StructuredError.
	[[nodiscard]] Result<void> MakeTangent(ScenePath &path, PathElementId node);

	// Re-derives every handle whose type is Auto, using the same rules. Handles marked Free, Aligned
	// or Vector are left alone, and no segment ever changes type.
	[[nodiscard]] Result<void> ApplyAutoHandles(ScenePath &path);
}
