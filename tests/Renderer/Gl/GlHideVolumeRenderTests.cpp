#include "Core/dspch.hpp"

#include "Renderer/Gl/GlTestContext.hpp"

#include <array>
#include <cmath>
#include <utility>

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
			const std::vector<SceneHideVolume> &volumes, const std::vector<std::size_t> &selected,
			const RendererWindowState &scene = {})
		{
			RendererGlobalRenderSettings settings;
			settings.backgroundColor = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
			const unsigned int texture = backend.RenderWindow(
				"gl-hide-volume", scene.structure, camera, settings, kImageWidth, kImageHeight, false, false, false, false, false,
				scene.pinnedMeasurements, {}, scene.freeLabels, {}, scene.sceneOrbitals, {}, scene.scenePlanes, {}, {}, {},
				nullptr, nullptr, nullptr, nullptr, scene.viewOffset,
				false, 0.3f, 45.0f, true, nullptr, true, scene.sceneDensities, {}, volumes, selected);
			const std::vector<unsigned char> pixels = ReadTextureRgba8TopDown(texture, kImageWidth, kImageHeight);
			std::size_t tinted = 0;
			for (int y = 0; y < kImageHeight; ++y)
				for (int x = 0; x < kImageWidth; ++x)
					if (PixelAt(pixels, kImageWidth, kImageHeight, x, y).r > 20)
						++tinted;
			return tinted;
		}

		[[nodiscard]] Ref<const DensityGrid> BlobGrid()
		{
			DensityGrid density;
			auto &grid = density.grid;
			grid.dimensions = glm::ivec3(24);
			grid.cell = glm::mat3(4.0f);
			grid.values.resize(24 * 24 * 24);
			for (int i = 0; i < 24; ++i)
				for (int j = 0; j < 24; ++j)
					for (int k = 0; k < 24; ++k)
					{
						const glm::vec3 delta = glm::vec3(i, j, k) * (4.0f / 24.0f) - glm::vec3(2.0f);
						grid.values[(i * 24 + j) * 24 + k] = std::exp(-glm::dot(delta, delta) / 0.5f);
					}
			return CreateRef<const DensityGrid>(std::move(density));
		}
	} // namespace

	TEST_F(GlTest, EveryVisibleHideVolumeHasAnOutlineAndSelectionMakesItThicker)
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
			const auto unselected = CountTinted(backend, camera, {volume}, {});
			EXPECT_GT(unselected, 100u) << static_cast<int>(kind);
			EXPECT_GT(CountTinted(backend, camera, {volume}, {0}), unselected) << static_cast<int>(kind);
		}
		volume.visible = false;
		EXPECT_EQ(CountTinted(backend, camera, {volume}, {}), 0u);
		EXPECT_EQ(CountTinted(backend, camera, {volume}, {0}), 0u);
		backend.Shutdown();
	}

	TEST_F(GlTest, DensityIsClippedPerFragmentByActiveVolumesInStructureSpace)
	{
		OpenGlRendererBackend backend;
		ASSERT_TRUE(backend.Initialize(ShaderDirectory(), PrimitiveMeshes()));
		RendererViewCamera camera;
		camera.SetViewport(static_cast<float>(kImageWidth), static_cast<float>(kImageHeight));
		RendererWindowState scene;
		scene.structure.lattice = glm::mat3(4.0f);
		scene.viewOffset = glm::vec3(3.0f, -1.0f, 0.5f);
		camera.SetOrbitState(glm::vec3(2.0f) + scene.viewOffset, 9.0f, 0.0f, 0.0f);
		RendererWindowState::SceneDensity density;
		density.id = SceneObjectId{1};
		density.data = BlobGrid();
		density.isoValue = 0.3f;
		density.positiveColor = glm::vec3(1.0f, 0.2f, 0.1f);
		density.alpha = 1.0f;
		density.showNegative = false;
		scene.sceneDensities.push_back(density);
		const auto baseline = CountTinted(backend, camera, {}, {}, scene);
		ASSERT_GT(baseline, 100u);

		SceneHideVolume volume;
		volume.visible = false;
		volume.renderable = false;
		volume.center = glm::vec3(2.0f);
		volume.halfExtents = glm::vec3(1.5f);
		for (const auto kind : {HideVolumeKind::Sphere, HideVolumeKind::Box, HideVolumeKind::Cylinder})
		{
			volume.kind = kind;
			EXPECT_EQ(CountTinted(backend, camera, {volume}, {}, scene), 0u);
			volume.invert = true;
			EXPECT_EQ(CountTinted(backend, camera, {volume}, {}, scene), baseline);
			volume.invert = false;
		}
		volume.kind = HideVolumeKind::Sphere;
		volume.center.x = 2.55f;
		volume.halfExtents = glm::vec3(0.75f);
		const auto partial = CountTinted(backend, camera, {volume}, {}, scene);
		EXPECT_GT(partial, 0u);
		EXPECT_LT(partial, baseline);
		volume.active = false;
		EXPECT_EQ(CountTinted(backend, camera, {volume}, {}, scene), baseline);
		EXPECT_EQ(CountTinted(backend, camera, {}, {}, scene), baseline);
		volume.active = true;
		volume.frame = HideVolumeFrame::Fractional;
		volume.center = glm::vec3(0.5f);
		volume.halfExtents = glm::vec3(0.375f);
		EXPECT_EQ(CountTinted(backend, camera, {volume}, {}, scene), 0u);

		// A later user of the same shader must not inherit the density's clip count.
		RendererWindowState::ScenePlane plane;
		plane.center = glm::vec3(2.0f);
		// Facing the camera - the default +z normal is edge-on from this orbit and draws nothing.
		plane.normal = glm::normalize(camera.Position() - camera.Target());
		plane.tangent = glm::normalize(glm::cross(std::abs(plane.normal.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0), plane.normal));
		plane.halfExtents = glm::vec2(0.25f);
		plane.color = glm::vec3(1.0f, 0.2f, 0.1f);
		plane.alpha = 1.0f;
		plane.showBorder = false;
		scene.scenePlanes.push_back(plane);
		scene.sceneDensities[0].visible = false;
		const auto planeOnly = CountTinted(backend, camera, {}, {}, scene);
		ASSERT_GT(planeOnly, 0u);
		scene.sceneDensities[0].visible = true;
		EXPECT_EQ(CountTinted(backend, camera, {volume}, {}, scene), planeOnly);
		backend.Shutdown();
	}

	TEST_F(GlTest, CoveredVacanciesAndLabelsAreHiddenButInactiveVolumesDoNotHideThem)
	{
		OpenGlRendererBackend backend;
		ASSERT_TRUE(backend.Initialize(ShaderDirectory(), PrimitiveMeshes()));
		RendererViewCamera camera;
		camera.SetViewport(static_cast<float>(kImageWidth), static_cast<float>(kImageHeight));
		camera.SetOrbitState(glm::vec3(2.0f), 9.0f, 0.0f, 0.0f);
		RendererWindowState scene;
		RendererVacancyData vacancy;
		vacancy.cartesianPosition = glm::vec3(2.0f);
		vacancy.color = glm::vec3(1.0f, 0.2f, 0.1f);
		scene.structure.vacancies.push_back(vacancy);
		const auto marker = CountTinted(backend, camera, {}, {}, scene);
		ASSERT_GT(marker, 0u);
		SceneHideVolume volume;
		volume.center = glm::vec3(2.0f);
		volume.visible = false;
		EXPECT_EQ(CountTinted(backend, camera, {volume}, {}, scene), 0u);
		volume.active = false;
		EXPECT_EQ(CountTinted(backend, camera, {volume}, {}, scene), marker);

		RendererWindowState::FreeLabel label;
		label.worldPosition = glm::vec3(2.0f);
		scene.freeLabels.push_back(label);
		scene.structure.atoms.resize(2);
		scene.structure.atoms[0].cartesianPosition = glm::vec3(1.5f, 2.0f, 2.0f);
		scene.structure.atoms[1].cartesianPosition = glm::vec3(2.5f, 2.0f, 2.0f);
		RendererWindowState::PinnedMeasurement pin;
		pin.atomIndices = {0, 1};
		scene.pinnedMeasurements.push_back(pin);
		(void)CountTinted(backend, camera, {}, {}, scene);
		auto picks = backend.GetLabelPickQuads("gl-hide-volume");
		ASSERT_EQ(picks.free.size(), 1u);
		ASSERT_EQ(picks.pinned.size(), 1u);
		ASSERT_TRUE(picks.free[0].valid);
		ASSERT_TRUE(picks.pinned[0].valid);
		volume.active = true;
		EXPECT_EQ(CountTinted(backend, camera, {volume}, {}, scene), 0u);
		picks = backend.GetLabelPickQuads("gl-hide-volume");
		EXPECT_FALSE(picks.free[0].valid);
		EXPECT_FALSE(picks.pinned[0].valid);
		volume.active = false;
		(void)CountTinted(backend, camera, {volume}, {}, scene);
		picks = backend.GetLabelPickQuads("gl-hide-volume");
		EXPECT_TRUE(picks.free[0].valid);
		EXPECT_TRUE(picks.pinned[0].valid);
		scene.freeLabels[0].anchorAtom = 0;
		scene.structure.atoms[0].visible = false;
		(void)CountTinted(backend, camera, {}, {}, scene);
		EXPECT_FALSE(backend.GetLabelPickQuads("gl-hide-volume").free[0].valid);
		backend.Shutdown();
	}
} // namespace DefectStudio::Tests
