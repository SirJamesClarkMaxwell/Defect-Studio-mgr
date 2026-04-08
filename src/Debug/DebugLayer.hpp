#pragma once

#include "Core/Layer.hpp"

namespace DefectStudio
{
	class DebugLayer final : public Layer
	{
	public:
		DebugLayer();

		void OnAttach() override;
		void OnDetach() override;
		void OnImGuiRender() override;
	};
} // namespace DefectStudio
