#include <gtest/gtest.h>

#include "IO/SceneObjectsIO.hpp"
#include "Presentation/Panels/SceneDensityEditor.hpp"
#include "Renderer/Commands/SceneObjectsSnapshotCommand.hpp"
#include "Renderer/Scene/SceneObjectPersistence.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		using SceneDensity = RendererWindowState::SceneDensity;

		[[nodiscard]] Ref<const DensityGrid> MakeGrid(float minimum, float maximum)
		{
			DensityGrid grid;
			grid.grid.dimensions = glm::ivec3(2);
			grid.grid.values = {minimum, maximum, 0, 0, 0, 0, 0, 0};
			grid.statistics.minimum = minimum;
			grid.statistics.maximum = maximum;
			return CreateRef<const DensityGrid>(std::move(grid));
		}

		[[nodiscard]] SceneDensity MakeEditedDensity(RendererWindowState &window)
		{
			// Non-ASCII on purpose: the thesis folder is "Praca-Inżynierska" and cp1252 has no "ż".
			SceneDensity density =
				MakeSceneDensity(Path::FromUtf8("calc/Praca-Inżynierska/q-1/CHGCAR"), DensityComponent::SpinDown);
			density.id = window.sceneRegistry.AllocateObjectId();
			density.referencePath = Path("calc/GeV/q0/CHGCAR");
			density.isoValue = 0.012f;
			density.showNegative = false;
			density.positiveColor = glm::vec3(0.1f, 0.2f, 0.3f);
			density.negativeColor = glm::vec3(0.4f, 0.5f, 0.6f);
			density.alpha = 0.4f;
			density.renderable = false;
			density.data = MakeGrid(-0.1f, 0.2f);
			density.loadState = SceneDensity::LoadState::Ready;
			return density;
		}
	} // namespace

	TEST(SceneDensityTests, ComponentKeysRoundTrip)
	{
		for (const DensityComponent component : kDensityComponents)
			EXPECT_EQ(ParseDensityComponent(DensityComponentKey(component)), std::optional<DensityComponent>(component));
		EXPECT_FALSE(ParseDensityComponent("spin").has_value());
	}

	TEST(SceneDensityTests, NewDensityIsNamedAfterItsFolderAndWaitsForTheLoader)
	{
		const SceneDensity density = MakeSceneDensity(Path("data/GeV/q-1/CHGCAR"));
		EXPECT_EQ(density.displayName, "q-1/CHGCAR");
		EXPECT_EQ(density.component, DensityComponent::Magnetization);
		EXPECT_EQ(density.loadState, SceneDensity::LoadState::Pending);
		EXPECT_EQ(density.data, nullptr);
	}

	TEST(SceneDensityTests, NonAsciiPathsRoundTripThroughUtf8AndNameWithoutThrowing)
	{
		const std::string utf8 = "D:/Praca-Inżynierska/Diament/q-1/CHGCAR";
		const Path path = Path::FromUtf8(utf8);
		EXPECT_EQ(path.Utf8(), utf8);
		EXPECT_EQ(MakeSceneDensity(Path::FromUtf8("D:/Praca-Inżynierska/CHGCAR")).displayName, "Praca-Inżynierska/CHGCAR");
	}

	TEST(SceneDensityTests, DefaultIsoIsTenPercentOfThePeakMagnitude)
	{
		DensityGridStatistics statistics;
		statistics.minimum = -0.8f;
		statistics.maximum = 0.4f;
		EXPECT_FLOAT_EQ(DefaultDensityIsoValue(statistics), 0.08f);
		EXPECT_FLOAT_EQ(DefaultDensityIsoValue({}), 0.0f);
	}

	TEST(SceneDensityTests, InvalidateDropsTheGridAndTheIsoValue)
	{
		RendererWindowState window;
		SceneDensity density = MakeEditedDensity(window);
		density.loadError = "old";
		InvalidateSceneDensity(density);
		EXPECT_EQ(density.data, nullptr);
		EXPECT_EQ(density.loadState, SceneDensity::LoadState::Pending);
		EXPECT_TRUE(density.loadError.empty());
		EXPECT_FLOAT_EQ(density.isoValue, 0.0f);
	}

	TEST(SceneDensityTests, UndoSnapshotSharesTheGridAndRestoresTheComponent)
	{
		RendererWindowState window;
		window.sceneDensities.push_back(MakeEditedDensity(window));
		const Ref<const DensityGrid> loaded = window.sceneDensities.front().data;
		window.selectedSceneDensities = {window.sceneDensities.front().id};

		SceneObjectsSnapshot before = CaptureSceneObjectsSnapshot(window);
		EXPECT_EQ(before.sceneDensities.front().data, loaded); // shared, not copied
		window.sceneDensities.front().component = DensityComponent::Total;
		InvalidateSceneDensity(window.sceneDensities.front());

		RestoreSceneObjectsSnapshot(window, std::move(before));
		ASSERT_EQ(window.sceneDensities.size(), 1u);
		EXPECT_EQ(window.sceneDensities.front().component, DensityComponent::SpinDown);
		EXPECT_EQ(window.sceneDensities.front().data, loaded);
		EXPECT_EQ(window.sceneDensities.front().loadState, SceneDensity::LoadState::Ready);
		EXPECT_EQ(window.selectedSceneDensities.size(), 1u);
	}

	TEST(SceneDensityTests, EraseAndDuplicateKeepIdsDistinct)
	{
		RendererWindowState window;
		window.sceneDensities.push_back(MakeEditedDensity(window));
		const SceneObjectId original = window.sceneDensities.front().id;
		window.selectedSceneDensities = {original};

		DuplicateSelectedSceneDensities(window);
		ASSERT_EQ(window.sceneDensities.size(), 2u);
		const SceneDensity &copy = window.sceneDensities.back();
		EXPECT_NE(copy.id, original);
		EXPECT_EQ(copy.data, window.sceneDensities.front().data);
		EXPECT_EQ(window.selectedSceneDensities, std::vector<SceneObjectId>{copy.id});

		EraseSceneDensities(window, {original});
		ASSERT_EQ(window.sceneDensities.size(), 1u);
		EXPECT_NE(window.sceneDensities.front().id, original);
		EXPECT_TRUE(window.selectedSceneDensities.empty());
	}

	TEST(SceneDensityTests, PersistenceRoundTripsEverythingButTheGrid)
	{
		RendererWindowState window;
		window.sceneDensities.push_back(MakeEditedDensity(window));
		const SceneDensity saved = window.sceneDensities.front();

		SceneObjectsFile file;
		file.structures.push_back({"structures/GeV/POSCAR", ExtractPersistedSceneObjects(window)});
		std::vector<StructuredError> warnings;
		SceneObjectsFile loaded;
		std::string error;
		ASSERT_TRUE(SceneObjectsIO::Parse(SceneObjectsIO::Serialize(file), loaded, warnings, error)) << error;
		ASSERT_EQ(loaded.structures.size(), 1u);

		RendererWindowState reopened;
		ApplyPersistedSceneObjects(reopened, loaded.structures.front().objects, warnings);
		EXPECT_TRUE(warnings.empty());
		ASSERT_EQ(reopened.sceneDensities.size(), 1u);
		const SceneDensity &density = reopened.sceneDensities.front();
		EXPECT_EQ(density.displayName, saved.displayName);
		EXPECT_EQ(density.chgcarPath, saved.chgcarPath);
		EXPECT_EQ(density.referencePath, saved.referencePath);
		EXPECT_EQ(density.component, DensityComponent::SpinDown);
		EXPECT_FLOAT_EQ(density.isoValue, 0.012f);
		EXPECT_FALSE(density.showNegative);
		EXPECT_EQ(density.positiveColor, saved.positiveColor);
		EXPECT_EQ(density.negativeColor, saved.negativeColor);
		EXPECT_FLOAT_EQ(density.alpha, 0.4f);
		EXPECT_TRUE(density.visible);
		EXPECT_FALSE(density.renderable);
		EXPECT_TRUE(density.id.IsValid());
		// The grid is re-read by SceneDensityLoader, never stored.
		EXPECT_EQ(density.data, nullptr);
		EXPECT_EQ(density.loadState, SceneDensity::LoadState::Pending);
	}

	TEST(SceneDensityTests, DensityWithoutAPathIsSkippedWithAWarning)
	{
		SceneObjectsFile loaded;
		std::vector<StructuredError> warnings;
		std::string error;
		const std::string yaml =
			"formatVersion: 2\nstructures:\n  - structureKey: s\n    objects:\n      - kind: SceneDensity\n        isoValue: 0.1\n";
		ASSERT_TRUE(SceneObjectsIO::Parse(yaml, loaded, warnings, error)) << error;
		ASSERT_EQ(loaded.structures.size(), 1u);
		EXPECT_TRUE(loaded.structures.front().objects.empty());
		EXPECT_FALSE(warnings.empty());
	}
} // namespace DefectStudio::Tests
