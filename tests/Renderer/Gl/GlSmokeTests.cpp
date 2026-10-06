#include "Core/dspch.hpp"

#include "Renderer/Gl/GlTestContext.hpp"

#include <array>
#include <filesystem>
#include <system_error>

#include <glad/gl.h>

#include "Core/Utils/Path.hpp"
#include "Renderer/OpenGl/FrameBufferReadback.hpp"
#include "Renderer/OpenGl/OpenGlRendererBackend.hpp"

#if defined(_WIN32)
#include <windows.h>
#endif

namespace DefectStudio::Tests
{
	namespace
	{
		[[nodiscard]] Path ShaderDirectoryNextToTestExecutable()
		{
#if defined(_WIN32)
			std::array<wchar_t, 32768> executablePath{};
			const DWORD length = GetModuleFileNameW(nullptr, executablePath.data(), static_cast<DWORD>(executablePath.size()));
			if (length > 0 && length < executablePath.size())
				return Path::FromResolved(FilePath(executablePath.data()).parent_path() / "shaders");
#else
			std::error_code error;
			const FilePath executablePath = std::filesystem::read_symlink("/proc/self/exe", error);
			if (!error)
				return Path::FromResolved(executablePath.parent_path() / "shaders");
#endif
			return Path::FromResolved(FileSystem::CurrentPath() / "shaders");
		}

		[[nodiscard]] RendererStaticMeshData TriangleMesh()
		{
			RendererStaticMeshData mesh;
			mesh.positions = {{-0.5f, -0.5f, 0.0f}, {0.5f, -0.5f, 0.0f}, {0.0f, 0.5f, 1.0f}};
			mesh.normals = {{0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}};
			mesh.gradientT = {0.0f, 0.5f, 1.0f};
			mesh.indices = {0u, 1u, 2u};
			return mesh;
		}

		[[nodiscard]] RendererPrimitiveMeshAssets PrimitiveMeshes()
		{
			RendererPrimitiveMeshAssets meshes;
			meshes.sphere = TriangleMesh();
			meshes.cylinder = TriangleMesh();
			meshes.cone = TriangleMesh();
			return meshes;
		}
	} // namespace

	TEST(GlTestContextTests, ContextCanBeDestroyedAndRecreated)
	{
		{
			GlTestContext first(256, 128);
			if (!first.IsValid())
			{
#if defined(_WIN32)
				FAIL() << first.FailureReason();
#else
				GTEST_SKIP() << first.FailureReason();
#endif
			}
			EXPECT_FALSE(first.Description().empty());
		}

		GlTestContext second(256, 128);
		if (!second.IsValid())
		{
#if defined(_WIN32)
			FAIL() << second.FailureReason();
#else
			GTEST_SKIP() << second.FailureReason();
#endif
		}
		EXPECT_FALSE(second.Description().empty());
	}

	TEST(GlTestContextTests, PixelAtUsesTopDownCoordinatesAndGuardsBounds)
	{
		const std::vector<unsigned char> pixels = {
			1, 2, 3, 4, 5, 6, 7, 8,
			9, 10, 11, 12, 13, 14, 15, 16};
		EXPECT_EQ(PixelAt(pixels, 2, 2, 0, 0), (Rgba8{1, 2, 3, 4}));
		EXPECT_EQ(PixelAt(pixels, 2, 2, 1, 1), (Rgba8{13, 14, 15, 16}));
		EXPECT_EQ(PixelAt(pixels, 2, 2, 2, 0), (Rgba8{}));
		EXPECT_EQ(PixelAt({1, 2, 3}, 1, 1, 0, 0), (Rgba8{}));
		EXPECT_TRUE(ReadRgba8TopDown(0, 0, 0, 1).empty());
		EXPECT_TRUE(ReadRgba8TopDown(0, 0, 1, 0).empty());
	}

	TEST_F(GlTest, TextureReadbackIsTopDownAndRestoresReadFramebuffer)
	{
		constexpr int width = 2;
		constexpr int height = 2;
		const std::array<unsigned char, width * height * 4> bottomUpPixels = {
			0, 255, 0, 255, 0, 255, 0, 255,
			255, 0, 0, 255, 255, 0, 0, 255};

		unsigned int texture = 0;
		glGenTextures(1, &texture);
		glBindTexture(GL_TEXTURE_2D, texture);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, bottomUpPixels.data());

		unsigned int previousFramebuffer = 0;
		glGenFramebuffers(1, &previousFramebuffer);
		glBindFramebuffer(GL_READ_FRAMEBUFFER, previousFramebuffer);
		const std::vector<unsigned char> pixels = ReadTextureRgba8TopDown(texture, width, height);
		GLint restoredFramebuffer = 0;
		glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &restoredFramebuffer);

		EXPECT_EQ(static_cast<unsigned int>(restoredFramebuffer), previousFramebuffer);
		EXPECT_EQ(PixelAt(pixels, width, height, 0, 0), (Rgba8{255, 0, 0, 255}));
		EXPECT_EQ(PixelAt(pixels, width, height, 0, height - 1), (Rgba8{0, 255, 0, 255}));

		glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
		glDeleteFramebuffers(1, &previousFramebuffer);
		glDeleteTextures(1, &texture);
	}

	TEST_F(GlTest, EmptyStructureRendersKnownBackgroundAtEveryCorner)
	{
		OpenGlRendererBackend backend;
		const Result<void> initialized = backend.Initialize(ShaderDirectoryNextToTestExecutable(), PrimitiveMeshes());
		ASSERT_TRUE(initialized) << initialized.Error().technicalDetails;

		RendererViewCamera camera;
		camera.SetViewport(static_cast<float>(kWidth), static_cast<float>(kHeight));
		camera.SetOrbitState(glm::vec3(0.0f), 10.0f, 0.0f, 0.0f);
		RendererGlobalRenderSettings settings;
		settings.backgroundColor = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
		const unsigned int texture = backend.RenderWindow(
			"gl-smoke-empty", RendererStructureData{}, camera, settings, kWidth, kHeight, false, false, false, false);
		ASSERT_NE(texture, 0u);

		const std::vector<unsigned char> pixels = ReadTextureRgba8TopDown(texture, kWidth, kHeight);
		const Rgba8 background{0, 0, 0, 255};
		EXPECT_EQ(PixelAt(pixels, kWidth, kHeight, 0, 0), background);
		EXPECT_EQ(PixelAt(pixels, kWidth, kHeight, kWidth - 1, 0), background);
		EXPECT_EQ(PixelAt(pixels, kWidth, kHeight, 0, kHeight - 1), background);
		EXPECT_EQ(PixelAt(pixels, kWidth, kHeight, kWidth - 1, kHeight - 1), background);

		backend.Shutdown();
	}
} // namespace DefectStudio::Tests
