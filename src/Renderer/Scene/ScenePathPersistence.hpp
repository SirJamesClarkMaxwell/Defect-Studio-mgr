#pragma once

#include <vector>

#include "Core/Diagnostics/StructuredError.hpp"
#include "IO/SceneObjectsIO.hpp"
#include "Renderer/Path/PathTypes.hpp"
#include "Renderer/RendererTypes.hpp"

namespace DefectStudio
{
	// DTO <-> ScenePath, and the v1 arrow importer. The boundary is deliberate: IO parses YAML into
	// PersistedScenePath and knows nothing about what a v1 arrow meant, while everything here is
	// renderer knowledge - which profile a Billboard arrow becomes, what a tip is called now, which
	// of the old fields no longer has a home.

	// Element ids are allocated fresh, never read from the file (plan v2 C11), so two loads of the
	// same file produce different ids for the same node. Nothing may depend on them matching.
	//
	// Warnings are appended, not returned: one path can lose several things at once and the caller
	// shows the user a single report for the whole load.
	[[nodiscard]] Result<ScenePath> BuildScenePath(
		const PersistedScenePath &persisted,
		const RendererStructureData &structure,
		std::vector<StructuredError> &outWarnings);

	// The inverse. Bindings are written as stable atom references (index + element + position) the
	// same way every other anchored kind writes them, so a reordered structure file still rebinds.
	[[nodiscard]] PersistedScenePath ExtractPersistedScenePath(
		const ScenePath &path, const RendererStructureData &structure);

	// v1 -> v2. Takes the DTO rather than a runtime SceneArrow: the file is the source of truth for
	// a migration, and a v1 arrow that never became a runtime object still has to convert.
	//
	// Mapping (plan v2 section 4, and the section 2 cut list for what is lost):
	//   geometry   N points -> N-1 Line segments. A two-point arrow WITH a control point becomes one
	//              Cubic with C1 = P0 + 2/3 (Q - P0), C2 = P1 + 2/3 (Q - P1), both handles Free -
	//              that is the exact degree elevation of the quadratic, so the drawn curve does not
	//              move. A control point on a longer arrow is dropped with a warning, because v1
	//              never drew it either.
	//   profile    Line / Arrow3D -> Round. Arrow2D Billboard -> CameraFacing.
	//              Arrow2D FixedPlane -> Flat, with a warning: V1 has no per-path fixed frame normal,
	//              so the ribbon's plane comes from the transported frame instead of the stored
	//              world plane, and a path whose points do not lie in that plane will look different.
	//   width      shaftWidth is a radius in v1 and a full width here, so width = 2 * shaftWidth.
	//   depth      Arrow2D -> AlwaysOnTop (it drew in a depth-disabled late pass and must keep doing
	//              so); everything else -> DepthTest.
	//   tips       None->None, Plain->Arrow, Barbed->Stealth, Open->OpenArrow, Bar->Bar,
	//              Circle->Circle. headLength/headWidth are absolute in v1 and relative to the stroke
	//              width here, so each becomes a scale of it.
	//   colour     alpha -> style alpha; a two-stop gradient -> two stops at 0 and 1;
	//              dashed/dashLength/gapLength -> the world-space dash pattern.
	//   anchors    a start/end anchor atom -> that endpoint node's CopyPosition binding carrying
	//              atomBuffer. Interior nodes are never bound by a migration.
	//   dropped    curveSegments (tessellation is adaptive now) and a non-zero outline (v2 section 2
	//              removed per-object outline) each produce a warning naming the loss.
	//
	// Invalid-but-finite geometry is kept and reported rather than repaired; only a non-finite or
	// structurally impossible arrow (fewer than two points) is rejected outright.
	struct ScenePathMigration
	{
		ScenePath path;
		std::vector<StructuredError> warnings;
	};

	[[nodiscard]] Result<ScenePathMigration> MigrateArrowToPath(const PersistedSceneArrow &arrow);
} // namespace DefectStudio
