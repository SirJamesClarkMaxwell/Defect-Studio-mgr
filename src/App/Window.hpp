#pragma once

#include <functional>
#include <string>

#include "Core/Event.hpp"

struct GLFWwindow;

namespace DefectStudio
{
	class Window
	{
	public:
		using EventCallback = std::function<void(EventVariant)>;

		Window() = default;
		~Window();

		Window(const Window &) = delete;
		Window &operator=(const Window &) = delete;

		bool Create(int width, int height, const std::string &title);
		void Destroy();

		bool ShouldClose() const;
		void PollEvents() const;
		void SwapBuffers() const;
		void GetFramebufferSize(int &width, int &height) const;
		void SetEventCallback(EventCallback callback);
		void DispatchEvent(EventVariant event);

		GLFWwindow *GetNativeHandle() const;

	private:
		GLFWwindow *m_Handle = nullptr;
		EventCallback m_EventCallback;
	};
} // namespace DefectStudio
