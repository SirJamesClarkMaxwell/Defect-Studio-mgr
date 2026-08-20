#pragma once

#include "Core/EventSystem/BusEventSystem/Event.hpp"
#include "Core/Utils/Path.hpp"
#include "Renderer/RendererConfig.hpp"
#include "Renderer/RendererTypes.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace DefectStudio::RendererEvents::Config
{
	struct Applied final : public BusEvent
	{
		Applied() = default;
		Applied(RendererConfig rendererConfig, bool persisted)
			: config(std::move(rendererConfig)), wasPersisted(persisted)
		{
		}

		RendererConfig config;
		bool wasPersisted = false;
	};
} // namespace DefectStudio::RendererEvents::Config

namespace DefectStudio::RendererEvents::Viewport
{
	// Emitowane przez RendererPanel gdy viewport jest aktywny.
	// RendererLayer subskrybuje i deleguje do kamery.

	struct OrbitDelta final : public BusEvent
	{
		std::string windowId;
		float dx = 0.0f;
		float dy = 0.0f;
	};

	struct PanDelta final : public BusEvent
	{
		std::string windowId;
		float dx = 0.0f;
		float dy = 0.0f;
	};

	struct ZoomDelta final : public BusEvent
	{
		std::string windowId;
		float amount = 0.0f;
	};

	struct FocusChanged final : public BusEvent
	{
		std::string windowId;
		bool focused = false;
	};

	struct AlignToAxisRequested final : public BusEvent
	{
		std::string windowId;
		int axis = 0;
	};

	enum class OrbitDirection
	{
		Left,
		Right,
		Up,
		Down
	};

	enum class RollDirection
	{
		Left,
		Right
	};

	enum class ZoomDirection
	{
		In,
		Out
	};

	struct OrbitDirectionRequested final : public BusEvent
	{
		std::string windowId;
		OrbitDirection direction = OrbitDirection::Left;
	};

	struct OrbitQuarterTurnRequested final : public BusEvent
	{
		std::string windowId;
		OrbitDirection direction = OrbitDirection::Left;
	};

	struct RollDirectionRequested final : public BusEvent
	{
		std::string windowId;
		RollDirection direction = RollDirection::Left;
	};

	struct ZoomDirectionRequested final : public BusEvent
	{
		std::string windowId;
		ZoomDirection direction = ZoomDirection::In;
	};

	struct OrbitStepRequested final : public BusEvent
	{
		std::string windowId;
		float dx = 0.0f;
		float dy = 0.0f;
	};

	struct RollStepRequested final : public BusEvent
	{
		std::string windowId;
		float delta = 0.0f;
	};

	struct ZoomStepRequested final : public BusEvent
	{
		std::string windowId;
		float amount = 0.0f;
	};

	struct FocusSelectedAtomRequested final : public BusEvent
	{
		std::string windowId;
	};

	struct UndoViewRequested final : public BusEvent
	{
		std::string windowId;
	};

	struct RedoViewRequested final : public BusEvent
	{
		std::string windowId;
	};

	struct SaveCurrentViewRequested final : public BusEvent
	{
		std::string windowId;
	};

	struct CycleSavedViewRequested final : public BusEvent
	{
		std::string windowId;
		int direction = 1;
	};

	struct ExportImageRequested final : public BusEvent
	{
		std::string windowId;
	};

	struct ViewTransitionRequested final : public BusEvent
	{
		std::string windowId;
		RendererViewSnapshot targetView;
		std::string sourceAction;
	};

	struct ProjectionToggleRequested final : public BusEvent
	{
		std::string windowId;
	};

	struct AtomSelectionRequested final : public BusEvent
	{
		std::string windowId;
		std::optional<std::size_t> atomIndex;
		bool additive = false;
	};

	// Toggles the active drag-select tool: pressing the chord for the already-active tool turns
	// it back off (mode None), pressing the other one switches directly.
	struct SelectionToolToggleRequested final : public BusEvent
	{
		std::string windowId;
		SelectionToolMode tool = SelectionToolMode::None;
	};

	enum class RegionSelectMode
	{
		Replace,
		Add,
		Subtract
	};

	// Published by RendererPanel once a box/circle drag completes, with the already hit-tested
	// (and visibility-filtered) atom indices - RendererLayer only applies set semantics, it does
	// not redo any projection math.
	struct RegionSelectionRequested final : public BusEvent
	{
		std::string windowId;
		std::vector<std::size_t> atomIndices;
		RegionSelectMode mode = RegionSelectMode::Replace;
	};

	struct HideSelectionRequested final : public BusEvent
	{
		std::string windowId;
	};

	struct ShowAllRequested final : public BusEvent
	{
		std::string windowId;
	};

	struct SelectionInvertRequested final : public BusEvent
	{
		std::string windowId;
	};

	// Captures the target window's current view (camera + selection + visibility) as the
	// session-scoped default - see RendererLayer::m_SessionDefaultView.
	struct SetAsDefaultViewRequested final : public BusEvent
	{
		std::string windowId;
	};

	struct ApplyDefaultViewRequested final : public BusEvent
	{
		std::string windowId;
	};

	// TODO(T08.6.3): temporary debug trigger to validate the orbital-grid-extraction ->
	// marching-tetrahedra -> render pipeline end-to-end. Loads a hardcoded fixture orbital
	// synchronously (blocking) and overlays it on the target window. Remove once the real
	// electronic-structure panel drives this instead.
	struct LoadTestOrbitalRequested final : public BusEvent
	{
		std::string windowId;
	};

	// Same fixture/band as LoadTestOrbitalRequested, but meshed via the GPU compute-shader port
	// (isosurface_march.comp) instead of the CPU reference - lets the two be compared visually on
	// the same structure to validate the port. See RendererLayer::onLoadTestOrbitalGpuRequested.
	struct LoadTestOrbitalGpuRequested final : public BusEvent
	{
		std::string windowId;
	};
} // namespace DefectStudio::RendererEvents::Viewport

namespace DefectStudio::RendererEvents::Windows
{
	// Opens filePath as a new renderer window at runtime (e.g. Project Tree "Open Defect").
	// Handled off the main thread via JobSystem - see RendererLayer::onOpenStructureRequested.
	struct OpenStructureRequested final : public BusEvent
	{
		Path filePath;
		std::string displayName;
	};
} // namespace DefectStudio::RendererEvents::Windows
