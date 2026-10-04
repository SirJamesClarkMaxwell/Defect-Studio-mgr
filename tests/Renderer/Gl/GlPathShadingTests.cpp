#include "Core/dspch.hpp"

#include <array>
#include <cmath>
#include <filesystem>
#include <system_error>

#include "Renderer/Gl/GlTestContext.hpp"
#include "Renderer/OpenGl/OpenGlRendererBackend.hpp"

#if defined(_WIN32)
#include <windows.h>
#endif

namespace DefectStudio::Tests
{
	static Path PathShaderDirectory()
	{
#if defined(_WIN32)
		std::array<wchar_t, 32768> executable{};
		const auto length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
		if (length > 0 && length < executable.size())
			return Path::FromResolved(FilePath(executable.data()).parent_path() / "shaders");
#else
		std::error_code error;
		const auto executable = std::filesystem::read_symlink("/proc/self/exe", error);
		if (!error) return Path::FromResolved(executable.parent_path() / "shaders");
#endif
		return Path::FromResolved(FileSystem::CurrentPath() / "shaders");
	}

	static RendererPrimitiveMeshAssets TestPrimitives()
	{
		RendererPrimitiveMeshAssets meshes;
		// A closed sphere approximation, rather than the single-triangle primitive in the
		// older GL path fixture, so a centre pixel really tests the atom's front surface.
		meshes.sphere.positions = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
		meshes.sphere.normals = meshes.sphere.positions;
		meshes.sphere.gradientT.assign(6, 0.0f);
		for (const auto triangle : {std::array{0u, 2u, 4u}, std::array{2u, 1u, 4u},
			std::array{1u, 3u, 4u}, std::array{3u, 0u, 4u}, std::array{2u, 0u, 5u},
			std::array{1u, 2u, 5u}, std::array{3u, 1u, 5u}, std::array{0u, 3u, 5u}})
			meshes.sphere.indices.insert(meshes.sphere.indices.end(), triangle.begin(), triangle.end());
		meshes.cylinder = meshes.sphere;
		meshes.cone = meshes.sphere;
		return meshes;
	}

	static ScenePath TestLine(const RendererViewCamera &camera, const bool startAtCentre)
	{
		const auto view = camera.ViewMatrix();
		const glm::vec3 right(view[0][0], view[1][0], view[2][0]);
		ScenePath path;
		path.id = SceneObjectId{1};
		path.nodes = {{AllocateElementId(path), startAtCentre ? glm::vec3(0) : -right * 2.5f, {}},
			{AllocateElementId(path), right * 2.5f, {}}};
		path.segments = {{AllocateElementId(path), LineSegmentData{}}};
		path.style.width = 0.2f;
		path.style.radialSegments = 64;
		path.style.color = {0, 1, 0};
		return path;
	}

	static std::vector<unsigned char> DrawPath(OpenGlRendererBackend &backend, PathSystem &paths,
		RendererViewCamera &camera, const RendererGlobalRenderSettings &settings,
		const RendererStructureData &structure = RendererStructureData{})
	{
		const PathRenderInput input{&paths};
		const auto texture = backend.RenderWindow("path-shading", structure, camera, settings,
			256, 128, !structure.atoms.empty(), false, false, false, false,
			{}, {}, {}, {}, {}, {}, {}, {}, {}, {}, nullptr, nullptr, nullptr, nullptr, glm::vec3(0),
			true, 0.3f, 45.0f, true, &input);
		return ReadTextureRgba8TopDown(texture, 256, 128);
	}

	TEST_F(GlTest, PathMaterialUsesTheBondSpecularStrength)
	{
		OpenGlRendererBackend backend;
		ASSERT_TRUE(backend.Initialize(PathShaderDirectory(), TestPrimitives()));
		RendererViewCamera camera;
		camera.SetViewport(kWidth, kHeight);
		camera.SetOrbitState({}, 6, 0, 0);
		PathSystem paths;
		paths.Store().Insert(TestLine(camera, false));
		RendererGlobalRenderSettings settings;
		settings.backgroundColor = {0, 0, 0, 1};
		settings.colorSaturation = 1;
		settings.lighting.ambientIntensity = 0;
		settings.lighting.keyIntensity = 0;
		settings.lighting.fillIntensity = 0;
		settings.lighting.backIntensity = 0;
		settings.lighting.keyDirection = glm::normalize(camera.Position());
		settings.lighting.specularIntensity = 1;
		settings.lighting.shininess = 1;
		const auto pixels = DrawPath(backend, paths, camera, settings);
		const auto highlight = PixelAt(pixels, kWidth, kHeight, kWidth / 2, kHeight / 2);
		// A 0.25 material multiplier produces about 64 here; bonds use the full strength.
		EXPECT_GT(highlight.r, 230);
		EXPECT_GT(highlight.b, 230);
		settings.lighting.specularIntensity = 0;
		const auto unlit = DrawPath(backend, paths, camera, settings);
		EXPECT_EQ(PixelAt(unlit, kWidth, kHeight, kWidth / 2, kHeight / 2), (Rgba8{0, 0, 0, 255}));
		backend.Shutdown();
	}

	TEST_F(GlTest, OpaqueAndTransparentPathsStartingAtAnAtomCentreAreOccludedByItsSphere)
	{
		OpenGlRendererBackend backend;
		ASSERT_TRUE(backend.Initialize(PathShaderDirectory(), TestPrimitives()));
		RendererViewCamera camera;
		camera.SetViewport(kWidth, kHeight);
		camera.SetOrbitState({}, 6, 0, 0);
		RendererStructureData structure;
		structure.atoms.push_back({"C", {}, {1, 0, 0}, 0.8f});
		RendererGlobalRenderSettings settings;
		settings.backgroundColor = {0, 0, 0, 1};
		settings.lighting.ambientIntensity = 1;
		settings.lighting.keyIntensity = 0;
		settings.lighting.fillIntensity = 0;
		settings.lighting.backIntensity = 0;
		settings.lighting.specularIntensity = 0;
		settings.colorSaturation = 1;
		PathSystem paths;
		paths.Store().Insert(TestLine(camera, true));
		for (const float alpha : {1.0f, 0.5f})
		{
			paths.Store().MutateStyle(SceneObjectId{1}, [&](ScenePath &path) {
				path.style.alpha = alpha;
				path.style.depthMode = PathDepthMode::DepthTest;
			});
			const auto pixels = DrawPath(backend, paths, camera, settings, structure);
			const auto centre = PixelAt(pixels, kWidth, kHeight, kWidth / 2, kHeight / 2);
			EXPECT_GT(centre.r, 200);
			EXPECT_LT(centre.g, 20);
			paths.Store().MutateStyle(SceneObjectId{1}, [](ScenePath &path) {
				path.style.depthMode = PathDepthMode::AlwaysOnTop;
			});
			const auto onTop = DrawPath(backend, paths, camera, settings, structure);
			EXPECT_GT(PixelAt(onTop, kWidth, kHeight, kWidth / 2, kHeight / 2).g, centre.g + 80);
		}
		backend.Shutdown();
	}
}
