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
} // namespace DefectStudio::RendererEvents::Viewport
