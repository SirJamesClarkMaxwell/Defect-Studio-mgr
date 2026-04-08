#include "Core/dspch.hpp"

#include "Domain/DomainLayer.hpp"

#include "Core/Logger.hpp"

namespace DefectStudio
{
	DomainLayer::DomainLayer() : Layer("DomainLayer")
	{
	}

	void DomainLayer::OnAttach()
	{
		DS_LOG_INFO("DomainLayer attached");
	}

	void DomainLayer::OnDetach()
	{
		DS_LOG_INFO("DomainLayer detached");
	}

	void DomainLayer::OnUpdate(float deltaTime)
	{
		(void)deltaTime;
	}
} // namespace DefectStudio
