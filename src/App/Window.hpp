#pragma once

#include <string>

struct GLFWwindow;

namespace DefectStudio
{
	class Window
	{
	public:
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

		GLFWwindow *GetNativeHandle() const;

	private:
		GLFWwindow *m_Handle = nullptr;
	};
} // namespace DefectStudio
