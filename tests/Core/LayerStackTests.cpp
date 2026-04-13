#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "Core/Platform/Events/PlatformEvent.hpp"
#include "Core/Layer.hpp"
#include "Core/LayerStack.hpp"

namespace
{
	class RecordingLayer final : public DefectStudio::Layer
	{
	public:
		RecordingLayer(std::string name, std::vector<std::string> &log, bool handlesEvent = false)
		    : DefectStudio::Layer(std::move(name)), m_Log(log), m_HandlesEvent(handlesEvent)
		{
		}

		void OnAttach() override
		{
			m_Log.push_back(GetName() + ":attach");
		}
		void OnDetach() override
		{
			m_Log.push_back(GetName() + ":detach");
		}
		void OnEvent(DefectStudio::Event &event) override
		{
			m_Log.push_back(GetName() + ":event");
			if (m_HandlesEvent)
				event.handled = true;
		}
		void OnUpdate(float) override
		{
			m_Log.push_back(GetName() + ":update");
		}

	private:
		std::vector<std::string> &m_Log;
		bool m_HandlesEvent;
	};
} // namespace

TEST(LayerStackTests, AttachDetachOrderingForLayersAndOverlays)
{
	std::vector<std::string> log;
	DefectStudio::LayerStack stack;
	auto layerA = DefectStudio::CreateUnique<RecordingLayer>("A", log);
	auto overlayB = DefectStudio::CreateUnique<RecordingLayer>("B", log);
	auto &layerARef = *layerA;
	auto &overlayBRef = *overlayB;

	stack.PushLayer(std::move(layerA));
	stack.PushOverlay(std::move(overlayB));
	stack.PopOverlay(overlayBRef);
	stack.PopLayer(layerARef);

	const std::vector<std::string> expected = {
	    "A:attach",
	    "B:attach",
	    "B:detach",
	    "A:detach",
	};
	EXPECT_EQ(log, expected);
}

TEST(LayerStackTests, IterationOrderIsLayersThenOverlays)
{
	std::vector<std::string> log;
	DefectStudio::LayerStack stack;
	auto layerA = DefectStudio::CreateUnique<RecordingLayer>("A", log);
	auto layerB = DefectStudio::CreateUnique<RecordingLayer>("B", log);
	auto overlayC = DefectStudio::CreateUnique<RecordingLayer>("C", log);

	stack.PushLayer(std::move(layerA));
	stack.PushLayer(std::move(layerB));
	stack.PushOverlay(std::move(overlayC));

	std::vector<std::string> order;
	for (const auto &layer : stack)
		order.push_back(layer->GetName());

	const std::vector<std::string> expected = {"A", "B", "C"};
	EXPECT_EQ(order, expected);
}

TEST(LayerStackTests, UpdateOrderIsLayersThenOverlays)
{
	std::vector<std::string> log;
	DefectStudio::LayerStack stack;
	auto layerA = DefectStudio::CreateUnique<RecordingLayer>("A", log);
	auto layerB = DefectStudio::CreateUnique<RecordingLayer>("B", log);
	auto overlayC = DefectStudio::CreateUnique<RecordingLayer>("C", log);

	stack.PushLayer(std::move(layerA));
	stack.PushLayer(std::move(layerB));
	stack.PushOverlay(std::move(overlayC));

	log.clear();
	for (const auto &layer : stack)
		layer->OnUpdate(0.016f);

	const std::vector<std::string> expected = {"A:update", "B:update", "C:update"};
	EXPECT_EQ(log, expected);
}

TEST(LayerStackTests, ReverseOrderEventPropagationStopsWhenHandled)
{
	std::vector<std::string> log;
	DefectStudio::LayerStack stack;
	auto layerA = DefectStudio::CreateUnique<RecordingLayer>("A", log);
	auto layerB = DefectStudio::CreateUnique<RecordingLayer>("B", log);
	auto overlayC = DefectStudio::CreateUnique<RecordingLayer>("C", log, true);

	stack.PushLayer(std::move(layerA));
	stack.PushLayer(std::move(layerB));
	stack.PushOverlay(std::move(overlayC));

	DefectStudio::WindowCloseEvent event;
	for (auto it = stack.rbegin(); it != stack.rend(); ++it)
	{
		(*it)->OnEvent(event);
		if (event.handled)
			break;
	}

	const std::vector<std::string> expected = {
	    "A:attach",
	    "B:attach",
	    "C:attach",
	    "C:event",
	};
	EXPECT_EQ(log, expected);
}
