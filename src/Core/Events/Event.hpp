#pragma once

namespace DefectStudio
{
	class BusEvent
	{
	public:
		BusEvent() : handled(false), stopPropagation(false)
		{
		}

		virtual ~BusEvent() = default;

		bool handled = false;
		bool stopPropagation = false;
	};
} // namespace DefectStudio
