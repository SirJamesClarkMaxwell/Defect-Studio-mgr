#include "Core/dspch.hpp"

#include "Presentation/EditorLayer.hpp"

#include "Core/Utils/Logger.hpp"

namespace DefectStudio
{
	EditorLayer::EditorLayer() : Layer("EditorLayer")
	{
	}

	void EditorLayer::OnAttach()
	{
		DS_LOG_INFO("EditorLayer attached");
	}

	void EditorLayer::OnDetach()
	{
		DS_LOG_INFO("EditorLayer detached");
	}

	void EditorLayer::OnImGuiRender()
	{
	}
} // namespace DefectStudio
