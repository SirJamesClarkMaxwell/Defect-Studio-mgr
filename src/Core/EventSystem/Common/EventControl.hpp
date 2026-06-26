#pragma once

namespace DefectStudio
{
	// Common control flags shared by both event lanes.
	struct EventControl
	{
		mutable bool handled = false;
		mutable bool stopPropagation = false;

		void ResetPropagation()
		{
			stopPropagation = false;
		}
	};
} // namespace DefectStudio
