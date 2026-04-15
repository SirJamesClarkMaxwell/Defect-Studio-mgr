#include "Core/dspch.hpp"

#include "IO/IOLayer.hpp"

#include "Core/Utils/Logger.hpp"

namespace DefectStudio
{
	IOLayer::IOLayer() : Layer("IOLayer")
	{
	}

	void IOLayer::OnAttach()
	{
		DS_LOG_INFO("IOLayer attached");
	}

	void IOLayer::OnDetach()
	{
		DS_LOG_INFO("IOLayer detached");
	}

	void IOLayer::OnUpdate(float deltaTime)
	{
		(void)deltaTime;
	}
} // namespace DefectStudio
