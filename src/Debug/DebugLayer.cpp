#include "Core/dspch.hpp"

#include "Debug/DebugLayer.hpp"

#include "Core/Logger.hpp"

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
	}
} // namespace DefectStudio
