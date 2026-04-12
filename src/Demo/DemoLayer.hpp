#pragma once

#include <memory>

#include "Core/Layer.hpp"

namespace DefectStudio::Demo
{
	class EventDispatcherDemo;
	class EventBusDemo;
	class JobSystemDemo;

	class DemoLayer final : public Layer
	{
	public:
		DemoLayer();
		~DemoLayer();

		void OnAttach() override;
		void OnDetach() override;
		void OnEvent(Event &event) override;
		void OnImGuiRender() override;

	private:
		std::unique_ptr<EventDispatcherDemo> m_EventDispatcherDemo;
		std::unique_ptr<EventBusDemo> m_EventBusDemo;
		std::unique_ptr<JobSystemDemo> m_JobSystemDemo;
		std::unique_ptr<class ComplexSystemsDemo> m_ComplexSystemsDemo;
	};
} // namespace DefectStudio::Demo
