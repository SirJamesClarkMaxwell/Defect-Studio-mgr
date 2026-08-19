#pragma once

#include "Core/EventSystem/BusEventSystem/Event.hpp"
#include "Core/Utils/Path.hpp"
#include "Renderer/RendererConfig.hpp"
#include "Renderer/RendererTypes.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <utility>

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
