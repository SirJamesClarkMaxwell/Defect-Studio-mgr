#include "Core/dspch.hpp"

#include "Domain/DomainLayer.hpp"

#include "Core/Logging/Logger.hpp"

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

	ProjectWorkspace &DomainLayer::Workspace() noexcept
	{
		return m_Workspace;
	}

	const ProjectWorkspace &DomainLayer::Workspace() const noexcept
	{
		return m_Workspace;
	}
} // namespace DefectStudio
