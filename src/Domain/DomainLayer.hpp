#pragma once

#include "Core/Layer.hpp"
#include "Domain/ProjectWorkspace.hpp"

namespace DefectStudio
{
	class DomainLayer final : public Layer
	{
	public:
		DomainLayer();

		void OnAttach() override;
		void OnDetach() override;
		void OnUpdate(float deltaTime) override;
		[[nodiscard]] ProjectWorkspace &Workspace() noexcept;
		[[nodiscard]] const ProjectWorkspace &Workspace() const noexcept;

	private:
		ProjectWorkspace m_Workspace;
	};
} // namespace DefectStudio
