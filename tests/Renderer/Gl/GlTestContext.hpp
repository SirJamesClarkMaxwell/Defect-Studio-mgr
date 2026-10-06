#pragma once

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

struct GLFWwindow;

namespace DefectStudio::Tests
{
	// RAII hidden-window OpenGL context for structural render tests. Construction either leaves a current
	// context or records why it could not, and teardown undoes everything in reverse order of creation.
	// Only one may be alive at a time: the constructor takes a process-wide lock that the destructor
	// releases, so two test cases can never hold two contexts even if the suite order changes.
	class GlTestContext
	{
	public:
		GlTestContext(int width, int height);
		~GlTestContext();
		GlTestContext(const GlTestContext &) = delete;
		GlTestContext &operator=(const GlTestContext &) = delete;

		[[nodiscard]] bool IsValid() const { return m_Valid; }
		[[nodiscard]] const std::string &FailureReason() const { return m_FailureReason; } // empty when valid
		[[nodiscard]] int Width() const { return m_Width; }
		[[nodiscard]] int Height() const { return m_Height; }

		// GL_VENDOR / GL_RENDERER / GL_VERSION on one line, for the test log. Empty when invalid.
		[[nodiscard]] std::string Description() const;

	private:
		GLFWwindow *m_Window = nullptr;
		int m_Width = 0;
		int m_Height = 0;
		bool m_Valid = false;
		bool m_OwnsGlfw = false;
		bool m_HoldsLock = false;
		std::string m_FailureReason;
	};

	// Attaches `texture` to a scratch framebuffer and reads all of it back through the production
	// ReadRgba8TopDown, so a test and the PNG exporter cannot disagree about row order. Empty vector for
	// a zero texture or a non-positive size.
	[[nodiscard]] std::vector<unsigned char> ReadTextureRgba8TopDown(unsigned int texture, int width, int height);

	struct Rgba8
	{
		unsigned char r = 0, g = 0, b = 0, a = 0;
		[[nodiscard]] bool operator==(const Rgba8 &) const = default;
	};

	// One pixel of a top-down RGBA8 buffer, addressed with y == 0 at the TOP. Out-of-range or a buffer
	// too small for the stated size yields {0,0,0,0} rather than reading past the end.
	[[nodiscard]] Rgba8 PixelAt(const std::vector<unsigned char> &topDown, int width, int height, int x, int y);

	// Fixture for every GL test. SetUp creates the context; on Windows a missing GL context FAILS (this
	// machine is expected to have one, and a silent skip there would hide a real regression), anywhere
	// else it SKIPs with the recorded reason. The viewport is deliberately non-square so a transposed
	// readback cannot pass.
	class GlTest : public ::testing::Test
	{
	protected:
		static constexpr int kWidth = 256;
		static constexpr int kHeight = 128;

		void SetUp() override;
		void TearDown() override;

		[[nodiscard]] GlTestContext &Gl() { return *m_Context; }

	private:
		std::unique_ptr<GlTestContext> m_Context;
	};
}
