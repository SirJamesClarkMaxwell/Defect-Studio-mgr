#pragma once

#include <cstddef>

#include <glm/glm.hpp>

#include "Core/Diagnostics/StructuredError.hpp"
#include "Renderer/Path/PathTypes.hpp"

namespace DefectStudio
{
	enum class PathEnd
	{
		Start,
		End,
	};

	// Every operation here is atomic by construction: it works on a copy and commits only on success,
	// so a rejected operation leaves the path byte-identical. Multi-path validate-then-apply is S8.

	// Exact split - Line by lerp, Cubic by de Casteljau, Arc by sweep (t*theta / (1-t)*theta in the
	// same plane). The evaluated curve is unchanged. Returns the new node's id.
	[[nodiscard]] Result<PathElementId> InsertNode(ScenePath &path, std::size_t segment, double t);

	// Appends a node past the chosen end. The new segment inherits the terminal segment's type:
	// Cubic gets Vector handles, Arc inherits normal and sweep as editable initial values. An empty
	// path or a single node extends with a Line.
	[[nodiscard]] Result<PathElementId> ExtendEnd(ScenePath &path, PathEnd end, glm::vec3 newPosition);

	// Interior node: both neighbouring segments Line -> merge into one Line; any other combination is
	// rejected with a StructuredError (V1 does not silently refit). Endpoint: removes its adjacent
	// segment. Deleting the last remaining node is rejected - DeletePath removes the object.
	[[nodiscard]] Result<void> DeleteNode(ScenePath &path, PathElementId node);

	// Reverses node and segment order, swaps each cubic's handles and negates each arc's sweep, so the
	// evaluated geometry is identical and only the direction of travel flips. Decorations, gradient and
	// dash phase join this in S5.
	[[nodiscard]] Result<void> ReversePath(ScenePath &path);
}
