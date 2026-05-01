#pragma once

#include <functional>
#include <string>

#include "Core/EventSystem/BusEventSystem/EventReceiver.hpp"
#include "Core/EventSystem/DispatchingEventSystem/PlatformEvents/PlatformEvent.hpp"
#include "Core/Utils/Memory.hpp"
#include "Core/Utils/Path.hpp"

struct GLFWwindow;

namespace DefectStudio
{
	class EventBus;
	struct WindowConfig;

	namespace AppEvents::Config
	{
		struct Applied;
	}

	class Window : public EventReceiver
	{
	public:
		using EventCallback = std::function<void(EventVariant)>;

		Window() = default;
		~Window();

		Window(const Window &) = delete;
		Window &operator=(const Window &) = delete;

		bool Create(int width, int height, const std::string &title, const Path &iconPath);
		void Destroy();
		void BindEventBus(Ref<EventBus> eventBus);

		bool ShouldClose() const;
		void PollEvents() const;
		void SwapBuffers() const;
		void GetFramebufferSize(int &width, int &height) const;
		void ApplyConfig(const WindowConfig &config, bool applyPositionAndSize = false);
		void CaptureConfig(WindowConfig &config) const;
		void SetEventCallback(EventCallback callback);
		void DispatchEvent(EventVariant event);

		GLFWwindow *GetNativeHandle() const;

	private:
		void setTitle(const std::string &title);
		void onConfigApplied(const AppEvents::Config::Applied &event);

	private:
		GLFWwindow *m_Handle = nullptr;
		EventCallback m_EventCallback;
		Ref<EventBus> m_EventBus;
	};
} // namespace DefectStudio
