#pragma once

namespace DefectStudio
{
	// Common control flags shared by both event lanes.
	struct EventControl
	{
		bool handled = false;
		bool stopPropagation = false;

		void ResetPropagation()
		{
			stopPropagation = false;
		}
	};
} // namespace DefectStudio
