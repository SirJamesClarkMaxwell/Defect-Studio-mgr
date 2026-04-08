#pragma once

#include "Core/Layer.hpp"

namespace DefectStudio
{
	class DomainLayer final : public Layer
	{
	public:
		DomainLayer();

		void OnAttach() override;
		void OnDetach() override;
		void OnUpdate(float deltaTime) override;
	};
} // namespace DefectStudio
