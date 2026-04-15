#pragma once

#include <string_view>

namespace DefectStudio
{
	// Shared marker describing the two runtime event lanes.
	enum class EventSystemKind
	{
		Dispatching,
		Bus
	};

	[[nodiscard]] constexpr std::string_view ToString(EventSystemKind kind)
	{
		switch (kind)
		{
		case EventSystemKind::Dispatching:
			return "DispatchingEventSystem";
		case EventSystemKind::Bus:
			return "BusEventSystem";
		default:
			return "UnknownEventSystem";
		}
	}
} // namespace DefectStudio
