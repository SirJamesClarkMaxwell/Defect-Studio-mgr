#pragma once

#include "Core/Layer.hpp"

namespace DefectStudio
{
	class StorageLayer final : public Layer
	{
	public:
		StorageLayer();

		void OnAttach() override;
		void OnDetach() override;
		void OnUpdate(float deltaTime) override;
	};
} // namespace DefectStudio
