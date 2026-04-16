#include "Core/dspch.hpp"

#include <imgui.h>

#include "Core/Utils/Memory.hpp"
#include "Demo/EventBusDemo.hpp"
#include "Demo/EventBusPublisherDemo.hpp"
#include "Demo/EventBusSubscriberDemo.hpp"

namespace DefectStudio::Demo
{
	EventBusDemo::EventBusDemo(Ref<EventBus> bus)
		: m_Bus(std::move(bus))
	{
		m_Publisher = CreateUnique<EventBusPublisherDemo>(m_Bus);
		m_Subscriber = CreateUnique<EventBusSubscriberDemo>(m_Bus);
	}

	EventBusDemo::~EventBusDemo()
	{
		m_Publisher.reset();
		m_Subscriber.reset();
	}

	void EventBusDemo::Render()
	{
		if (ImGui::CollapsingHeader("Publisher", ImGuiTreeNodeFlags_DefaultOpen) && m_Publisher)
			m_Publisher->Render();

		if (ImGui::CollapsingHeader("Subscriber", ImGuiTreeNodeFlags_DefaultOpen) && m_Subscriber)
			m_Subscriber->Render();
	}
} // namespace DefectStudio::Demo
