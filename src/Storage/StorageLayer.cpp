#include "Core/dspch.hpp"

#include "Storage/StorageLayer.hpp"

#include "Core/Utils/Logger.hpp"

namespace DefectStudio
{
	StorageLayer::StorageLayer() : Layer("StorageLayer")
	{
	}

	void StorageLayer::OnAttach()
	{
		DS_LOG_INFO("StorageLayer attached");
	}

	void StorageLayer::OnDetach()
	{
		DS_LOG_INFO("StorageLayer detached");
	}

	void StorageLayer::OnUpdate(float deltaTime)
	{
		(void)deltaTime;
	}
} // namespace DefectStudio
