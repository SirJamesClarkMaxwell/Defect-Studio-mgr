#pragma once

#include "Core/EventSystem/Common/EventControl.hpp"
#include "Core/EventSystem/Common/EventSystemCommon.hpp"

namespace DefectStudio
{
	class BusEvent : public EventControl
	{
	public:
		virtual ~BusEvent() = default;

		[[nodiscard]] static constexpr EventSystemKind GetSystemKind()
		{
			return EventSystemKind::Bus;
		}
	};
} // namespace DefectStudio
