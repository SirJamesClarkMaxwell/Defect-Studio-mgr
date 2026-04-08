#include "Core/dspch.hpp"

#include "Presentation/ImGuiLayer.hpp"

#include "Core/Logger.hpp"

namespace DefectStudio
{
	ImGuiLayer::ImGuiLayer() : Layer("ImGuiLayer")
	{
	}

	void ImGuiLayer::OnAttach()
	{
		DS_LOG_INFO("ImGuiLayer attached");
	}

	void ImGuiLayer::OnDetach()
	{
		DS_LOG_INFO("ImGuiLayer detached");
	}

	void ImGuiLayer::OnImGuiRender()
	{
	}
} // namespace DefectStudio
