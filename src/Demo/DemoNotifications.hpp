#pragma once

#include <cstdint>

#include "Core/Utils/Memory.hpp"

namespace DefectStudio
{
	class Notifier;
}

namespace DefectStudio::Demo
{
	class DemoNotificationsPanel
	{
	public:
		explicit DemoNotificationsPanel(Ref<Notifier> notifier);
		void Render();
		void RenderToasts();

	private:
		Ref<Notifier> m_Notifier;
		std::uint32_t m_NotificationDemoCounter = 0;
	};
} // namespace DefectStudio::Demo
