#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "Core/Diagnostics/StructuredError.hpp"
#include "IO/SceneObjectsIO.hpp"
#include "Renderer/RendererWindowState.hpp"

namespace DefectStudio
{
	// A persisted atom reference binds only when all three hold: the index exists, the element
	// matches, and the atom is within this distance of the stored position.
	inline constexpr float kSceneAtomLinkToleranceAngstrom = 0.05f;

	[[nodiscard]] bool AtomReferenceBinds(
		const RendererStructureData &structure,
		const PersistedAtomRef &reference,
		float toleranceAngstrom = kSceneAtomLinkToleranceAngstrom);

	// Index to bind a persisted reference to: reference.index when AtomReferenceBinds holds there,
	// otherwise the nearest atom with the same element within the tolerance (atoms were reordered,
	// e.g. by the species sort when the POSCAR was saved). nullopt when neither exists.
	[[nodiscard]] std::optional<std::size_t> ResolveAtomReference(
		const RendererStructureData &structure,
		const PersistedAtomRef &reference,
		float toleranceAngstrom = kSceneAtomLinkToleranceAngstrom);

	// Random 128-bit key as 32 lowercase hex chars.
	[[nodiscard]] std::string GenerateScenePersistKey();

	// Gives every pinned measurement / free label / scene arrow with an empty persistKey a fresh one.
	// Called from SceneSystem's annotation sync, so every creation site is covered in one place.
	void EnsureScenePersistKeys(RendererWindowState &window);

	// Window -> file. A linked pin stores its atoms' current index/element/position; a pin with
	// linkBroken stores its frozen atom data instead. Legacy runtime arrows are not extracted.
	[[nodiscard]] std::vector<PersistedSceneObject> ExtractPersistedSceneObjects(const RendererWindowState &window);
	// Save entry point: assign stable keys before extracting any binding target.
	[[nodiscard]] std::vector<PersistedSceneObject> ExtractPersistedSceneObjects(RendererWindowState &window);

	// File -> window. Replaces the window's scene objects, migrates v1 arrows to paths, clears their
	// selections (the global UndoStack is left alone). Every object gets a freshly allocated SceneObjectId
	// from window.sceneRegistry (file ids are never reused); persistKey is kept as-is (a new one is
	// generated when empty). Pin atom indices come from ResolveAtomReference, so a reordered structure
	// rebinds silently. A pin whose atom references do not all resolve gets linkBroken = true, its
	// frozen atom data filled from the file, and one Severity::Warning StructuredError with code
	// "scene_objects.link_broken" is appended to outWarnings.
	void ApplyPersistedSceneObjects(
		RendererWindowState &window,
		const std::vector<PersistedSceneObject> &objects,
		std::vector<StructuredError> &outWarnings);

	// Several windows showing one structure -> one saved list. Input is ordered least to most
	// recently focused. Deduplicated by persistKey, the most recently focused window's copy wins and
	// keeps the position of the key's first appearance; objects with an empty persistKey are all kept.
	[[nodiscard]] std::vector<PersistedSceneObject> MergeWindowSceneObjects(
		const std::vector<std::vector<PersistedSceneObject>> &windowsLeastToMostRecentlyFocused);
} // namespace DefectStudio
