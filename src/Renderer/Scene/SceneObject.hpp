#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace DefectStudio
{
	enum class SceneObjectKind
	{
		Atom,
		Bond,
		PinnedMeasurement,
		FreeLabel,
		SceneArrow,
	};

	[[nodiscard]] const char *SceneObjectKindName(SceneObjectKind kind);

	// Stable handle for one object in one window's scene. Monotonic per window and never reused, so
	// a stale id resolves to nothing instead of resolving to whatever object shifted into its slot -
	// which is exactly what the old index-based selection vectors did on delete.
	//
	// Per-window rather than globally unique (no Uuid): every consumer already has the window it is
	// asking about, and a small integer is far cheaper to read in a debugger.
	struct SceneObjectId
	{
		std::uint64_t value = 0; // 0 == unset/invalid; SceneRegistry allocates from 1 up

		[[nodiscard]] bool IsValid() const
		{
			return value != 0;
		}

		friend bool operator==(SceneObjectId, SceneObjectId) = default;
		friend auto operator<=>(SceneObjectId, SceneObjectId) = default;
	};

	// Carried by every mirrored entity - atom, bond, pinned measurement, free label, scene arrow.
	// The one component a consumer can enumerate to see the whole scene
	// (registry.view<SceneObjectComponent>()), instead of scanning five unrelated vectors.
	//
	// ponytail: no provenance enum and no stale flag, though the workstream file lists both. Nothing
	// reads them yet - the outliner shows name + kind, the properties panel dispatches on kind. Add
	// them when the first consumer exists (provenance when generated objects can be regenerated in
	// place, stale when an object can outlive the structure it was anchored to).
	struct SceneObjectComponent
	{
		SceneObjectId id;
		SceneObjectKind kind = SceneObjectKind::Atom;
		// Position in the flat array this entity mirrors: structure.atoms / structure.bonds /
		// windowState.pinnedMeasurements / .freeLabels / .sceneArrows. Rewritten on every sync -
		// only `id` is stable, and only `id` may be stored by anything outside a sync.
		std::size_t sourceIndex = 0;
		std::string displayName;
	};
} // namespace DefectStudio

template <>
struct std::hash<DefectStudio::SceneObjectId>
{
	[[nodiscard]] std::size_t operator()(const DefectStudio::SceneObjectId id) const noexcept
	{
		return std::hash<std::uint64_t>{}(id.value);
	}
};
