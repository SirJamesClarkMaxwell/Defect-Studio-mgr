#pragma once

#include <cstddef>
#include <vector>

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
	// Splits the original parameter interval into count+1 equal pieces, atomically (1..64 nodes).
	[[nodiscard]] Result<std::vector<PathElementId>> InsertNodes(ScenePath &path, std::size_t segment, std::size_t count);

	// Appends a node past the chosen end. The new segment inherits the terminal segment's type:
	// Cubic gets Vector handles, Arc inherits normal and sweep as editable initial values. An empty
	// path or a single node extends with a Line.
	[[nodiscard]] Result<PathElementId> ExtendEnd(ScenePath &path, PathEnd end, glm::vec3 newPosition);

	// Interior node: both neighbouring segments Line -> merge into one Line; any other combination is
	// rejected with a StructuredError (V1 does not silently refit). Endpoint: removes its adjacent
	// segment. Deleting the last remaining node is rejected - DeletePath removes the object.
	[[nodiscard]] Result<void> DeleteNode(ScenePath &path, PathElementId node);

	// Reverses node and segment order, swaps each cubic's handles and negates each arc's sweep, so the
	// evaluated geometry is identical and only the direction of travel flips. Endpoint decoration
	// roles stay authored; gradient and dash phase mirror to retain their physical colours/pattern.
	[[nodiscard]] Result<void> ReversePath(ScenePath &path);

	// Moves the path's origin to the centre of its authored nodes, without moving the path: every
	// node's authored position loses the centroid, and `transform.position` gains that centroid
	// ROTATED AND SCALED BY THE TRANSFORM. The resolved geometry is then identical to the last
	// float.
	//
	// The rotation and scale are not decoration. The resolver computes
	// `world = position + rotation * (scale * node)`, so adding a raw centroid to `position` while
	// subtracting it from the nodes only cancels when rotation and scale are the identity - a
	// rotated path would jump by `centroid - rotation * scale * centroid` the moment its origin was
	// centred. Identity is the common case, which is exactly why this is easy to get wrong and
	// stays wrong until someone rotates something.
	//
	// task/41 transform-2, and the user's choice of Blender's model in full. In Blender an object's
	// origin IS its Location, and its mesh lies around it - move a cube to x = 5 and the panel reads
	// 5. Here the geometry lived entirely in the node positions and `transform.position` started at
	// zero, so Location would have read 0 for every path ever drawn, no matter where it was. A field
	// that always reads zero is not a field.
	//
	// Applies to the authored positions, which are local by definition, so a bound node's stored
	// fallback moves with the rest and keeps resolving where it did. Call it when a path is created
	// and when one is loaded from a file written before paths had a transform; do NOT call it after
	// every edit, because Blender does not move an origin when a vertex moves either, and a path
	// whose origin wandered on every drag would make Location meaningless in the other direction.
	//
	// A path with no nodes is left alone.
	void MovePathOriginToCentre(ScenePath &path);
}
