#include "Core/dspch.hpp"

#include "Renderer/Gl/GlTestContext.hpp"

#include <mutex>

#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include "Core/Logging/Logger.hpp"
#include "Renderer/OpenGl/FrameBufferReadback.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		std::mutex g_ContextMutex;

		[[nodiscard]] std::string GlString(GLenum name)
		{
			const auto *value = glGetString(name);
			return value != nullptr ? reinterpret_cast<const char *>(value) : "unavailable";
		}
	} // namespace

	GlTestContext::GlTestContext(int width, int height)
		: m_Width(width),
		  m_Height(height)
	{
		g_ContextMutex.lock();
		m_HoldsLock = true;

		if (width <= 0 || height <= 0)
		{
			m_FailureReason = "GL test context dimensions must be positive";
			return;
		}
		if (glfwInit() != GLFW_TRUE)
		{
			m_FailureReason = "glfwInit failed";
			return;
		}
		m_OwnsGlfw = true;

		glfwDefaultWindowHints();
		glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
		glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
		glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
		glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
		glfwWindowHint(GLFW_SAMPLES, 0);
		glfwWindowHint(GLFW_SRGB_CAPABLE, GLFW_FALSE);
		m_Window = glfwCreateWindow(width, height, "DefectStudio GL test", nullptr, nullptr);
		if (m_Window == nullptr)
		{
			m_FailureReason = "glfwCreateWindow failed for the hidden OpenGL 4.3 core context";
			return;
		}

		glfwMakeContextCurrent(m_Window);
		if (gladLoadGL(reinterpret_cast<GLADloadfunc>(glfwGetProcAddress)) == 0)
		{
			m_FailureReason = "gladLoadGL failed";
			glfwMakeContextCurrent(nullptr);
			return;
		}

		glDisable(GL_DITHER);
		glDisable(GL_FRAMEBUFFER_SRGB);
		m_Valid = true;
	}

	GlTestContext::~GlTestContext()
	{
		if (m_Window != nullptr)
		{
			glfwMakeContextCurrent(nullptr);
			glfwDestroyWindow(m_Window);
			m_Window = nullptr;
		}
		if (m_OwnsGlfw)
			glfwTerminate();
		if (m_HoldsLock)
			g_ContextMutex.unlock();
	}

	std::string GlTestContext::Description() const
	{
		if (!m_Valid)
			return {};
		return "GL_VENDOR=" + GlString(GL_VENDOR) + " GL_RENDERER=" + GlString(GL_RENDERER) +
			" GL_VERSION=" + GlString(GL_VERSION);
	}

	std::vector<unsigned char> ReadTextureRgba8TopDown(unsigned int texture, int width, int height)
	{
		if (texture == 0 || width <= 0 || height <= 0)
			return {};

		GLint previousFramebuffer = 0;
		glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previousFramebuffer);
		unsigned int framebuffer = 0;
		glGenFramebuffers(1, &framebuffer);
		glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer);
		glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
		if (glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		{
			glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<unsigned int>(previousFramebuffer));
			glDeleteFramebuffers(1, &framebuffer);
			return {};
		}

		std::vector<unsigned char> pixels = ReadRgba8TopDown(0, 0, width, height);
		glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<unsigned int>(previousFramebuffer));
		glDeleteFramebuffers(1, &framebuffer);
		return pixels;
	}

	Rgba8 PixelAt(const std::vector<unsigned char> &topDown, int width, int height, int x, int y)
	{
		if (width <= 0 || height <= 0 || x < 0 || x >= width || y < 0 || y >= height)
			return {};

		const std::size_t expectedSize = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u;
		if (topDown.size() < expectedSize)
			return {};
		const std::size_t offset = (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
			static_cast<std::size_t>(x)) * 4u;
		return {topDown[offset], topDown[offset + 1u], topDown[offset + 2u], topDown[offset + 3u]};
	}

	void GlTest::SetUp()
	{
		m_Context = std::make_unique<GlTestContext>(kWidth, kHeight);
		if (!m_Context->IsValid())
		{
#if defined(_WIN32)
			FAIL() << m_Context->FailureReason();
#else
			GTEST_SKIP() << m_Context->FailureReason();
#endif
		}

		static bool loggedDescription = false;
		if (!loggedDescription)
		{
			DS_LOG_INFO("GL test context: {}", m_Context->Description());
			loggedDescription = true;
		}
	}

	void GlTest::TearDown()
	{
		m_Context.reset();
	}
} // namespace DefectStudio::Tests
