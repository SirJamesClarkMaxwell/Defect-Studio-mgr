#pragma once

#include "Core/Layer.hpp"

namespace DefectStudio
{
	class EditorLayer final : public Layer
	{
	public:
		EditorLayer();

		void OnAttach() override;
		void OnDetach() override;
		void OnImGuiRender() override;
	};
} // namespace DefectStudio
