#include "Core/dspch.hpp"

#include "Core/Logger.hpp"
#include "Debug/DebugLayer.hpp"

namespace DefectStudio
{
	DebugLayer::DebugLayer() : Layer("DebugLayer")
	{
	}

	void DebugLayer::OnAttach()
	{
		DS_LOG_INFO("DebugLayer attached");
	}

	void DebugLayer::OnDetach()
	{
		DS_LOG_INFO("DebugLayer detached");
	}

	void DebugLayer::OnImGuiRender()
	{
		// Additional diagnostics can be added here
	}
} // namespace DefectStudio
