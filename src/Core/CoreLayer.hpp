#pragma once

#include "Core/Layer.hpp"

namespace DefectStudio
{
	class CoreLayer final : public Layer
	{
	public:
		CoreLayer();

		void OnAttach() override;
		void OnDetach() override;
		void OnUpdate(float deltaTime) override;

	private:
		float m_AccumulatedTime = 0.0f;
	};
} // namespace DefectStudio
