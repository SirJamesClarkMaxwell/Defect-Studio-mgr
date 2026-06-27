#pragma once

#include "Core/EventSystem/BusEventSystem/Event.hpp"

#include <string>

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
} // namespace DefectStudio::RendererEvents::Viewport
