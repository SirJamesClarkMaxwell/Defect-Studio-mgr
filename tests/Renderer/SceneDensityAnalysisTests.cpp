#include <gtest/gtest.h>

#include "IO/SceneObjectsIO.hpp"
#include "Renderer/Commands/SceneObjectsSnapshotCommand.hpp"
#include "Renderer/Scene/SceneDensityAnalysis.hpp"
#include "Renderer/Scene/SceneObjectPersistence.hpp"
#include "Renderer/Scene/SceneSystem.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		using SceneDensity = RendererWindowState::SceneDensity;

		// 8 A cubic cell, 16^3 grid, |value| concentrated on one voxel at fractional (0.25, 0.5, 0.75).
		[[nodiscard]] Ref<const DensityGrid> PointGrid()
		{
			DensityGrid density;
			density.grid.dimensions = glm::ivec3(16);
			density.grid.cell = glm::mat3(8.0f);
			density.grid.values.assign(16 * 16 * 16, 0.0f);
			density.grid.values[(4 * 16 + 8) * 16 + 12] = -2.0f;
			density.centroid = AbsoluteDensityCentroid(density.grid);
			return CreateRef<const DensityGrid>(std::move(density));
		}

		[[nodiscard]] SceneDensity LoadedDensity()
		{
			SceneDensity density;
			density.id = SceneObjectId{7};
			density.chgcarPath = Path("GeV/q-1/CHGCAR");
			density.data = PointGrid();
			density.loadState = SceneDensity::LoadState::Ready;
			return density;
		}
	} // namespace

	TEST(SceneDensityAnalysisTests, TheCentreIsTheCentroidUntilTheUserOverridesIt)
	{
		SceneDensity density;
		EXPECT_FALSE(SceneDensityAnalysisCentre(density).has_value());
		EXPECT_FALSE(SceneDensityAnalysisCentreFractional(density).has_value());

		density = LoadedDensity();
		ASSERT_TRUE(SceneDensityAnalysisCentre(density).has_value());
		EXPECT_NEAR(glm::distance(*SceneDensityAnalysisCentre(density), glm::vec3(2.0f, 4.0f, 6.0f)), 0.0f, 1e-4f);

		density.analysisCentre = glm::vec3(-1.0f, 4.0f, 10.0f); // outside the cell on purpose
		EXPECT_EQ(*SceneDensityAnalysisCentre(density), glm::vec3(-1.0f, 4.0f, 10.0f));
		const std::optional<glm::vec3> fractional = SceneDensityAnalysisCentreFractional(density);
		ASSERT_TRUE(fractional.has_value());
		EXPECT_NEAR(glm::distance(*fractional, glm::vec3(0.875f, 0.5f, 0.25f)), 0.0f, 1e-5f); // wrapped
	}

	TEST(SceneDensityAnalysisTests, TheProfileIsCachedUntilTheCentreOrTheGridChanges)
	{
		SceneDensity density = LoadedDensity();
		const DensityRadialProfile *first = SceneDensityRadialProfile(density);
		ASSERT_NE(first, nullptr);
		EXPECT_NEAR(first->total, 2.0 * 0.125, 1e-6); // |-2| * voxel volume (8^3 / 16^3)
		EXPECT_FLOAT_EQ(first->FractionWithin(0.1f), 1.0f);
		EXPECT_EQ(SceneDensityRadialProfile(density), first);

		density.analysisCentre = glm::vec3(2.0f, 4.0f, 3.0f); // 3 A from the voxel
		const DensityRadialProfile *moved = SceneDensityRadialProfile(density);
		ASSERT_NE(moved, nullptr);
		EXPECT_FLOAT_EQ(moved->FractionWithin(2.9f), 0.0f);
		EXPECT_FLOAT_EQ(moved->FractionWithin(3.1f), 1.0f);

		density.data = PointGrid(); // a reload: new grid object
		const DensityRadialProfile *reloaded = SceneDensityRadialProfile(density);
		ASSERT_NE(reloaded, nullptr);
		EXPECT_EQ(density.radialProfileGrid, density.data.get());

		SceneDensity empty;
		EXPECT_EQ(SceneDensityRadialProfile(empty), nullptr);
	}

	TEST(SceneDensityAnalysisTests, SelectedAtomsGiveTheirCentroid)
	{
		RendererWindowState window;
		for (const glm::vec3 position : {glm::vec3(0.0f), glm::vec3(2.0f, 0.0f, 0.0f), glm::vec3(0.0f, 4.0f, 0.0f)})
		{
			RendererAtomData atom;
			atom.element = "C";
			atom.cartesianPosition = position;
			window.structure.atoms.push_back(atom);
		}
		EXPECT_FALSE(SelectedAtomsCentroid(window).has_value());
		window.selectedAtomIndices = {1, 2};
		EXPECT_EQ(*SelectedAtomsCentroid(window), glm::vec3(1.0f, 2.0f, 0.0f));
	}

	TEST(SceneDensityAnalysisTests, TheCentreIsSpelledTheWaySxdefectalignReadsIt)
	{
		EXPECT_EQ(FormatSxdefectalignCentre(glm::vec3(0.468766f, 0.531233f, 0.468766f)), "0.468766,0.531233,0.468766");
		EXPECT_EQ(FormatSxdefectalignCentre(glm::vec3(1.25f, -0.25f, 0.0f)), "0.250000,0.750000,0.000000");
	}

	TEST(SceneDensityAnalysisTests, TheOverrideAndRadiusSurviveSaveLoadAndUndo)
	{
		RendererWindowState window;
		SceneDensity density = LoadedDensity();
		density.id = window.sceneRegistry.AllocateObjectId();
		density.analysisCentre = glm::vec3(1.5f, 2.5f, 3.5f);
		density.analysisRadius = 6.5f;
		window.sceneDensities.push_back(density);
		SceneDensity followsCentroid = LoadedDensity();
		followsCentroid.id = window.sceneRegistry.AllocateObjectId();
		window.sceneDensities.push_back(followsCentroid);

		SceneObjectsFile file;
		file.structures.push_back({"structures/GeV/POSCAR", ExtractPersistedSceneObjects(window)});
		std::vector<StructuredError> warnings;
		SceneObjectsFile loaded;
		std::string error;
		ASSERT_TRUE(SceneObjectsIO::Parse(SceneObjectsIO::Serialize(file), loaded, warnings, error)) << error;
		RendererWindowState reopened;
		ApplyPersistedSceneObjects(reopened, loaded.structures.front().objects, warnings);
		ASSERT_EQ(reopened.sceneDensities.size(), 2u);
		ASSERT_TRUE(reopened.sceneDensities[0].analysisCentre.has_value());
		EXPECT_EQ(*reopened.sceneDensities[0].analysisCentre, glm::vec3(1.5f, 2.5f, 3.5f));
		EXPECT_FLOAT_EQ(reopened.sceneDensities[0].analysisRadius, 6.5f);
		EXPECT_FALSE(reopened.sceneDensities[1].analysisCentre.has_value());
		EXPECT_FLOAT_EQ(reopened.sceneDensities[1].analysisRadius, 5.0f);

		const SceneObjectsSnapshot before = CaptureSceneObjectsSnapshot(window);
		window.sceneDensities[0].analysisCentre.reset();
		RestoreSceneObjectsSnapshot(window, before);
		ASSERT_TRUE(window.sceneDensities[0].analysisCentre.has_value());
	}
} // namespace DefectStudio::Tests
