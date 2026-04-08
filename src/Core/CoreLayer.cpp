#include "Core/dspch.hpp"

#include "Core/CoreLayer.hpp"

#include "Core/Logger.hpp"

namespace DefectStudio
{
	CoreLayer::CoreLayer() : Layer("CoreLayer")
	{
	}

	void CoreLayer::OnAttach()
	{
		DS_LOG_INFO("CoreLayer attached");
	}

	void CoreLayer::OnDetach()
	{
		DS_LOG_INFO("CoreLayer detached");
	}

	void CoreLayer::OnUpdate(float deltaTime)
	{
		m_AccumulatedTime += deltaTime;
	}
} // namespace DefectStudio
