#pragma once

#include <variant>

#include "Core/dspch.hpp"

// Base event type interface and macros
#include "Core/Platform/Events/PlatformEventBase.hpp"

// Concrete event implementations
#include "Core/Platform/Events/WindowEvents.hpp"
#include "Core/Platform/Events/KeyboardEvents.hpp"
#include "Core/Platform/Events/MouseEvents.hpp"
#include "Core/Platform/Events/TouchpadEvents.hpp"

namespace DefectStudio
{
	// Compile-time closed event type set for EventQueue variant storage
	using EventVariant = std::variant<
		WindowCloseEvent,
		WindowResizeEvent,
		KeyPressedEvent,
		KeyReleasedEvent,
		KeyRepeatedEvent,
		MouseButtonPressedEvent,
		MouseButtonReleasedEvent,
		MouseMovedEvent,
		MouseScrolledEvent,
		TouchpadGestureEvent
	>;

	// Compile-time guard — aktualizuj gdy dodajesz nowy typ eventu
	static_assert(std::variant_size_v<EventVariant> == 10,
		"EventVariant: dodano nowy typ eventu — zaktualizuj alias i ten assert");

} // namespace DefectStudio