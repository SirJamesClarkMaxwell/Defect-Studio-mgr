#include "Core/dspch.hpp"

#include "App/ApplicationLifecycle.hpp"

namespace DefectStudio
{
	bool ApplicationLifecycleState::TryMarkCreated()
	{
		if (m_Created)
			return false;

		m_Created = true;
		return true;
	}

	bool ApplicationLifecycleState::TryBeginShutdown()
	{
		if (!m_Created && !m_Running)
			return false;

		m_Running = false;
		m_Created = false;
		return true;
	}

	void ApplicationLifecycleState::SetRunning(bool running)
	{
		m_Running = running;
	}

	bool ApplicationLifecycleState::IsCreated() const
	{
		return m_Created;
	}

	bool ApplicationLifecycleState::IsRunning() const
	{
		return m_Running;
	}

	void HandleLifecycleEvent(Event &event, ApplicationLifecycleState &state)
	{
		EventDispatcher dispatcher(event);
		auto function = [&state](WindowCloseEvent &){state.SetRunning(false);return true;};
		dispatcher.Dispatch<WindowCloseEvent>(function);
	}
} // namespace DefectStudio
