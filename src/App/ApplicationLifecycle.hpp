#pragma once

#include "Core/Event.hpp"

namespace DefectStudio
{
	class ApplicationLifecycleState
	{
	public:
		[[nodiscard]] bool TryMarkCreated();
		[[nodiscard]] bool TryBeginShutdown();
		void SetRunning(bool running);
		[[nodiscard]] bool IsCreated() const;
		[[nodiscard]] bool IsRunning() const;

	private:
		bool m_Created = false;
		bool m_Running = false;
	};

	void HandleLifecycleEvent(Event &event, ApplicationLifecycleState &state);
} // namespace DefectStudio