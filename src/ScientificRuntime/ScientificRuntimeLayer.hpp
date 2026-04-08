#pragma once

#include "Core/Layer.hpp"

namespace DefectStudio
{
	class ScientificRuntimeLayer final : public Layer
	{
	public:
		ScientificRuntimeLayer();

		void OnAttach() override;
		void OnDetach() override;
		void OnUpdate(float deltaTime) override;
	};
} // namespace DefectStudio
