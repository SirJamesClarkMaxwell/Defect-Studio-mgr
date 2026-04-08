#pragma once

#include "Core/Layer.hpp"

namespace DefectStudio
{
	class ImGuiLayer final : public Layer
	{
	public:
		ImGuiLayer();

		void OnAttach() override;
		void OnDetach() override;
		void OnImGuiRender() override;
	};
} // namespace DefectStudio
