#pragma once

#include "Core/Layer.hpp"

namespace DefectStudio
{
	class IOLayer final : public Layer
	{
	public:
		IOLayer();

		void OnAttach() override;
		void OnDetach() override;
		void OnUpdate(float deltaTime) override;
	};
} // namespace DefectStudio
