#include "Core/dspch.hpp"

#include "Renderer/Gl/GlTestContext.hpp"

#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>

#include <stb_image_write.h>

#include "Core/Utils/Path.hpp"
#include "Renderer/OpenGl/OpenGlRendererBackend.hpp"
#include "Renderer/RendererWindowState.hpp"
#include "ScientificRuntime/Python/VaspDensityGridBridge.hpp"

#if defined(_WIN32)
#include <windows.h>
#endif

namespace DefectStudio::Tests
{
	namespace
	{
		using SceneDensity = RendererWindowState::SceneDensity;

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

		// 32^3 grid in a 4 A cube: a positive Gaussian at (1.2, 2, 2) and a negative one at (2.8, 2, 2),
		// so from +z the yellow blob is on the left and the cyan one on the right.
		[[nodiscard]] Ref<const DensityGrid> TwoBlobGrid()
		{
			DensityGrid density;
			OrbitalGridData &grid = density.grid;
			grid.dimensions = glm::ivec3(32);
			grid.cell = glm::mat3(4.0f);
			grid.values.resize(32 * 32 * 32);
			for (int i = 0; i < 32; ++i)
				for (int j = 0; j < 32; ++j)
					for (int k = 0; k < 32; ++k)
					{
						const glm::vec3 r = glm::vec3(i, j, k) * (4.0f / 32.0f);
						const auto blob = [&r](const glm::vec3 &centre) {
							const glm::vec3 d = r - centre;
							return std::exp(-glm::dot(d, d) / 0.25f);
						};
						grid.values[(i * 32 + j) * 32 + k] = blob({1.2f, 2.0f, 2.0f}) - blob({2.8f, 2.0f, 2.0f});
					}
			density.statistics.minimum = -1.0f;
			density.statistics.maximum = 1.0f;
			return CreateRef<const DensityGrid>(std::move(density));
		}

		[[nodiscard]] std::vector<unsigned char> Render(OpenGlRendererBackend &backend, RendererViewCamera &camera,
			const std::vector<SceneDensity> &densities, const int width, const int height,
			const RendererStructureData &structure = {}, const bool showAtoms = false)
		{
			RendererGlobalRenderSettings settings;
			settings.backgroundColor = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
			const unsigned int texture = backend.RenderWindow(
				"gl-density", structure, camera, settings, width, height, showAtoms, false, false, false, false,
				{}, {}, {}, {}, {}, {}, {}, {}, {}, {}, nullptr, nullptr, nullptr, nullptr, glm::vec3(0.0f),
				true, 0.3f, 45.0f, true, nullptr, true, densities, {});
			return ReadTextureRgba8TopDown(texture, width, height);
		}

		struct ColourCounts
		{
			std::size_t yellowLeft = 0, yellowRight = 0, cyanLeft = 0, cyanRight = 0;
		};

		[[nodiscard]] ColourCounts CountColours(const std::vector<unsigned char> &pixels, const int width, const int height)
		{
			ColourCounts counts;
			for (int y = 0; y < height; ++y)
				for (int x = 0; x < width; ++x)
				{
					const Rgba8 p = PixelAt(pixels, width, height, x, y);
					const bool left = x < width / 2;
					if (p.r > 60 && p.g > 40 && p.b < p.g / 2)
						++(left ? counts.yellowLeft : counts.yellowRight);
					else if (p.b > 60 && p.g > 40 && p.r < p.b / 2)
						++(left ? counts.cyanLeft : counts.cyanRight);
				}
			return counts;
		}

		void WriteArtifact(const char *name, const std::vector<unsigned char> &pixels, const int width, const int height)
		{
			std::error_code error;
			const FilePath directory = FileSystem::CurrentPath() / "build" / "test-artifacts";
			std::filesystem::create_directories(directory, error);
			stbi_write_png((directory / name).string().c_str(), width, height, 4, pixels.data(), width * 4);
		}
	} // namespace

	TEST_F(GlTest, DensityDrawsBothSignsInTheirOwnColoursAndHidesTheNegativeOnRequest)
	{
		OpenGlRendererBackend backend;
		ASSERT_TRUE(backend.Initialize(ShaderDirectory(), PrimitiveMeshes()));
		RendererViewCamera camera;
		camera.SetViewport(static_cast<float>(kWidth), static_cast<float>(kHeight));
		camera.SetOrbitState(glm::vec3(2.0f), 9.0f, 0.0f, 0.0f);

		SceneDensity density;
		density.id = SceneObjectId{1};
		density.data = TwoBlobGrid();
		density.isoValue = 0.3f;
		density.alpha = 1.0f;
		density.loadState = SceneDensity::LoadState::Ready;

		std::vector<unsigned char> pixels = Render(backend, camera, {density}, kWidth, kHeight);
		WriteArtifact("density_two_blobs.png", pixels, kWidth, kHeight);
		const ColourCounts both = CountColours(pixels, kWidth, kHeight);
		EXPECT_GT(both.yellowLeft, 100u);
		EXPECT_GT(both.cyanRight, 100u);
		EXPECT_EQ(both.yellowRight, 0u);
		EXPECT_EQ(both.cyanLeft, 0u);

		density.showNegative = false;
		pixels = Render(backend, camera, {density}, kWidth, kHeight);
		const ColourCounts positiveOnly = CountColours(pixels, kWidth, kHeight);
		EXPECT_GT(positiveOnly.yellowLeft, 100u);
		EXPECT_EQ(positiveOnly.cyanRight, 0u);

		density.visible = false;
		pixels = Render(backend, camera, {density}, kWidth, kHeight);
		const ColourCounts hidden = CountColours(pixels, kWidth, kHeight);
		EXPECT_EQ(hidden.yellowLeft + hidden.cyanRight, 0u);
		backend.Shutdown();
	}

	// Visual check on a real calculation, off by default (a 180^3 CHGCAR takes ~13 s to parse):
	//   set DS_DENSITY_CHGCAR=<path to a spin-polarised CHGCAR>
	// writes build/test-artifacts/density_real_chgcar.png - the spin density at 10 % of its peak,
	// viewed down z over the whole cell.
	TEST_F(GlTest, RealChgcarSpinDensityRendersToArtifact)
	{
		const char *chgcar = std::getenv("DS_DENSITY_CHGCAR");
		if (chgcar == nullptr || *chgcar == '\0')
			GTEST_SKIP() << "DS_DENSITY_CHGCAR not set";
		const Result<DensityGrid> loaded =
			VaspDensityGridBridge{}.LoadDensityGrid(Path(chgcar), DensityComponent::Magnetization);
		ASSERT_TRUE(loaded) << loaded.Error().technicalDetails;

		constexpr int width = 800;
		constexpr int height = 800;
		OpenGlRendererBackend backend;
		ASSERT_TRUE(backend.Initialize(ShaderDirectory(), PrimitiveMeshes()));
		const glm::mat3 &cell = loaded->grid.cell;
		const glm::vec3 centre = 0.5f * (cell[0] + cell[1] + cell[2]);
		RendererViewCamera camera;
		camera.SetViewport(static_cast<float>(width), static_cast<float>(height));
		camera.SetOrbitState(centre, 2.2f * glm::length(cell[0]), 0.4f, 0.3f);

		SceneDensity density;
		density.id = SceneObjectId{1};
		density.isoValue = 0.1f * std::max(std::abs(loaded->statistics.minimum), std::abs(loaded->statistics.maximum));
		density.data = CreateRef<const DensityGrid>(*loaded);
		density.alpha = 0.85f;
		const std::vector<unsigned char> pixels = Render(backend, camera, {density}, width, height);
		WriteArtifact("density_real_chgcar.png", pixels, width, height);
		const ColourCounts counts = CountColours(pixels, width, height);
		EXPECT_GT(counts.yellowLeft + counts.yellowRight, 0u);
		backend.Shutdown();
	}
} // namespace DefectStudio::Tests
