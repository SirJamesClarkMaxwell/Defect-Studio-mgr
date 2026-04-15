#include "Core/dspch.hpp"

#include "ScientificRuntime/ScientificRuntimeLayer.hpp"

#include "Core/Utils/Logger.hpp"

namespace DefectStudio
{
	ScientificRuntimeLayer::ScientificRuntimeLayer() : Layer("ScientificRuntimeLayer")
	{
	}

	void ScientificRuntimeLayer::OnAttach()
	{
		DS_LOG_INFO("ScientificRuntimeLayer attached");
	}

	void ScientificRuntimeLayer::OnDetach()
	{
		DS_LOG_INFO("ScientificRuntimeLayer detached");
	}

	void ScientificRuntimeLayer::OnUpdate(float deltaTime)
	{
		(void)deltaTime;
	}
} // namespace DefectStudio
