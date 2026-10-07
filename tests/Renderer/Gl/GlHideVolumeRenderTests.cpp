#include "Core/dspch.hpp"

#include "Renderer/Gl/GlTestContext.hpp"

#include <array>

#include "Core/Utils/Path.hpp"
#include "Renderer/OpenGl/OpenGlRendererBackend.hpp"
#include "Renderer/RendererWindowState.hpp"

#if defined(_WIN32)
#include <windows.h>
#endif

namespace DefectStudio::Tests
{
	namespace
	{
		constexpr int kImageWidth = 256;
		constexpr int kImageHeight = 128;

		[[nodiscard]] Path ShaderDirectory()
		{
#if defined(_WIN32)
			std::array<wchar_t, 32768> executablePath{};
			const DWORD length = GetModuleFileNameW(nullptr, executablePath.data(), static_cast<DWORD>(executablePath.size()));
			if (length > 0 && length < executablePath.size())
				return Path::FromResolved(FilePath(executablePath.data()).parent_path() / "shaders");
#endif
			return Path::FromResolved(FileSystem::CurrentPath() / "shaders");
		}

		[[nodiscard]] RendererPrimitiveMeshAssets PrimitiveMeshes()
		{
			RendererStaticMeshData mesh;
			mesh.positions = {{-0.5f, -0.5f, 0.0f}, {0.5f, -0.5f, 0.0f}, {0.0f, 0.5f, 1.0f}};
			mesh.normals = {{0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}};
			mesh.gradientT = {0.0f, 0.5f, 1.0f};
			mesh.indices = {0u, 1u, 2u};
			return {mesh, mesh, mesh};
		}

		[[nodiscard]] std::size_t CountTinted(OpenGlRendererBackend &backend, RendererViewCamera &camera,
			const std::vector<SceneHideVolume> &volumes, const std::vector<std::size_t> &selected)
		{
			RendererStructureData structure;
			structure.lattice = glm::mat3(4.0f);
			RendererGlobalRenderSettings settings;
			settings.backgroundColor = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
			const unsigned int texture = backend.RenderWindow(
				"gl-hide-volume", structure, camera, settings, kImageWidth, kImageHeight, false, false, false, false, false,
				{}, {}, {}, {}, {}, {}, {}, {}, {}, {}, nullptr, nullptr, nullptr, nullptr, glm::vec3(0.0f),
				true, 0.3f, 45.0f, true, nullptr, true, {}, {}, volumes, selected);
			const std::vector<unsigned char> pixels = ReadTextureRgba8TopDown(texture, kImageWidth, kImageHeight);
			std::size_t tinted = 0;
			for (int y = 0; y < kImageHeight; ++y)
				for (int x = 0; x < kImageWidth; ++x)
					if (PixelAt(pixels, kImageWidth, kImageHeight, x, y).r > 20)
						++tinted;
			return tinted;
		}
	} // namespace

	// The overlay is drawn only while the volume is selected - a volume drawn whenever its eye is on
	// would sit on top of the defect it frames.
	TEST_F(GlTest, AHideVolumeIsDrawnOnlyWhileSelected)
	{
		OpenGlRendererBackend backend;
		ASSERT_TRUE(backend.Initialize(ShaderDirectory(), PrimitiveMeshes()));
		RendererViewCamera camera;
		camera.SetViewport(static_cast<float>(kImageWidth), static_cast<float>(kImageHeight));
		camera.SetOrbitState(glm::vec3(2.0f), 12.0f, 0.0f, 0.0f);

		SceneHideVolume volume;
		volume.id = SceneObjectId{1};
		volume.center = glm::vec3(2.0f);
		volume.halfExtents = glm::vec3(1.5f);
		volume.color = glm::vec3(1.0f, 0.2f, 0.1f);
		volume.alpha = 0.8f;

		for (const HideVolumeKind kind : {HideVolumeKind::Sphere, HideVolumeKind::Box, HideVolumeKind::Cylinder})
		{
			volume.kind = kind;
			EXPECT_GT(CountTinted(backend, camera, {volume}, {0}), 500u) << static_cast<int>(kind);
		}
		EXPECT_EQ(CountTinted(backend, camera, {volume}, {}), 0u);
		volume.visible = false;
		EXPECT_EQ(CountTinted(backend, camera, {volume}, {0}), 0u);
		backend.Shutdown();
	}
} // namespace DefectStudio::Tests
