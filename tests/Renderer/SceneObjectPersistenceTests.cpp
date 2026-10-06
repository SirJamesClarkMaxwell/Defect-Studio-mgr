#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>

#include "IO/SceneObjectsIO.hpp"
#include "Renderer/Scene/SceneObjectPersistence.hpp"
#include "Renderer/Scene/ScenePathPersistence.hpp"
#include "Renderer/Scene/SceneSystem.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		[[nodiscard]] RendererWindowState MakeWindow()
		{
			RendererWindowState window;
			for (const auto &[element, position] : {std::pair{"C", glm::vec3(0.0f)}, {"N", glm::vec3(1.5f, 0.0f, 0.0f)}, {"C", glm::vec3(0.0f, 1.5f, 0.0f)}})
			{
				RendererAtomData atom;
				atom.element = element;
				atom.cartesianPosition = position;
				window.structure.atoms.push_back(atom);
			}
			return window;
		}

		[[nodiscard]] PersistedPinnedMeasurement MakeBondPin(const std::string &key)
		{
			PersistedPinnedMeasurement pin;
			pin.persistKey = key;
			pin.atomRefs = {{0, "C", glm::vec3(0.0f)}, {1, "N", glm::vec3(1.5f, 0.0f, 0.0f)}};
			return pin;
		}

		[[nodiscard]] PersistedFreeLabel MakeLabel(const std::string &key, const std::string &text)
		{
			PersistedFreeLabel label;
			label.persistKey = key;
			label.text = text;
			return label;
		}
	} // namespace

	TEST(SceneObjectPersistenceTests, AtomReferenceNeedsIndexElementAndPosition)
	{
		const RendererWindowState window = MakeWindow();
		EXPECT_TRUE(AtomReferenceBinds(window.structure, {1, "N", glm::vec3(1.52f, 0.0f, 0.0f)}));
		EXPECT_FALSE(AtomReferenceBinds(window.structure, {5, "N", glm::vec3(1.5f, 0.0f, 0.0f)})); // index gone
		EXPECT_FALSE(AtomReferenceBinds(window.structure, {1, "C", glm::vec3(1.5f, 0.0f, 0.0f)})); // element differs
		// Same element moved into the index by a reorder: position check still fails.
		EXPECT_FALSE(AtomReferenceBinds(window.structure, {2, "C", glm::vec3(0.0f)}));
	}

	TEST(SceneObjectPersistenceTests, ResolveAtomReferenceFallsBackToElementAndPosition)
	{
		const RendererWindowState window = MakeWindow();
		EXPECT_EQ(ResolveAtomReference(window.structure, {1, "N", glm::vec3(1.5f, 0.0f, 0.0f)}), std::optional<std::size_t>(1));
		// Reordered: index 2 now holds a different C, the saved one moved to index 0.
		EXPECT_EQ(ResolveAtomReference(window.structure, {2, "C", glm::vec3(0.01f, 0.0f, 0.0f)}), std::optional<std::size_t>(0));
		EXPECT_EQ(ResolveAtomReference(window.structure, {9, "N", glm::vec3(1.5f, 0.0f, 0.0f)}), std::optional<std::size_t>(1));
		// Element changed at that position (unsaved substitution): no binding.
		EXPECT_EQ(ResolveAtomReference(window.structure, {1, "C", glm::vec3(1.5f, 0.0f, 0.0f)}), std::nullopt);
		EXPECT_EQ(ResolveAtomReference(window.structure, {0, "C", glm::vec3(5.0f, 5.0f, 5.0f)}), std::nullopt);
	}

	TEST(SceneObjectPersistenceTests, ApplyRebindsPinToReorderedAtoms)
	{
		RendererWindowState window = MakeWindow();
		PersistedPinnedMeasurement pin;
		pin.persistKey = "sorted";
		pin.atomRefs = {{2, "C", glm::vec3(0.0f)}, {0, "N", glm::vec3(1.5f, 0.0f, 0.0f)}};

		std::vector<StructuredError> warnings;
		ApplyPersistedSceneObjects(window, {pin}, warnings);

		EXPECT_TRUE(warnings.empty());
		ASSERT_EQ(window.pinnedMeasurements.size(), 1u);
		EXPECT_FALSE(window.pinnedMeasurements[0].linkBroken);
		EXPECT_EQ(window.pinnedMeasurements[0].atomIndices, (std::vector<std::size_t>{0, 1}));
	}

	TEST(SceneObjectPersistenceTests, PersistKeysAreFilledOnceAndAreHex128)
	{
		RendererWindowState window = MakeWindow();
		window.freeLabels.push_back({});
		ScenePath path;
		const auto generated = SceneSystem::AppendScenePath(window, path);
		path.persistKey = "keepme";
		const auto kept = SceneSystem::AppendScenePath(window, path);
		EnsureScenePersistKeys(window);

		const std::string key = window.freeLabels[0].persistKey;
		ASSERT_EQ(key.size(), 32u);
		EXPECT_TRUE(std::all_of(key.begin(), key.end(), [](unsigned char c) { return std::isdigit(c) || (c >= 'a' && c <= 'f'); }));
		EXPECT_NE(window.paths->Store().Find(generated)->persistKey, key);
		EXPECT_EQ(window.paths->Store().Find(kept)->persistKey, "keepme");
		EnsureScenePersistKeys(window);
		EXPECT_EQ(window.freeLabels[0].persistKey, key);
	}

	TEST(SceneObjectPersistenceTests, ApplyAllocatesFreshIdsAndKeepsKeys)
	{
		RendererWindowState window = MakeWindow();
		const SceneObjectId taken = window.sceneRegistry.AllocateObjectId();
		window.freeLabels.push_back({});
		window.freeLabels.back().id = taken;
		window.selectedFreeLabels.push_back(taken);

		std::vector<StructuredError> warnings;
		ApplyPersistedSceneObjects(window, {MakeLabel("k1", "one"), MakeLabel("", "two"), MakeBondPin("k3")}, warnings);

		EXPECT_TRUE(warnings.empty());
		ASSERT_EQ(window.freeLabels.size(), 2u);
		ASSERT_EQ(window.pinnedMeasurements.size(), 1u);
		EXPECT_EQ(window.freeLabels[0].text, "one");
		EXPECT_EQ(window.freeLabels[0].persistKey, "k1");
		EXPECT_EQ(window.freeLabels[1].persistKey.size(), 32u);
		EXPECT_TRUE(window.freeLabels[0].id.IsValid());
		EXPECT_NE(window.freeLabels[0].id, taken);
		EXPECT_NE(window.freeLabels[0].id, window.freeLabels[1].id);
		EXPECT_NE(window.pinnedMeasurements[0].id, window.freeLabels[1].id);
		EXPECT_EQ(window.pinnedMeasurements[0].atomIndices, (std::vector<std::size_t>{0, 1}));
		EXPECT_FALSE(window.pinnedMeasurements[0].linkBroken);
		EXPECT_TRUE(window.selectedFreeLabels.empty());
	}

	TEST(SceneObjectPersistenceTests, PinSavedBrokenHealsWhenReferencesResolveAgain)
	{
		RendererWindowState window = MakeWindow();
		PersistedPinnedMeasurement pin = MakeBondPin("healed");
		pin.linkBroken = true;

		std::vector<StructuredError> warnings;
		ApplyPersistedSceneObjects(window, {pin}, warnings);
		ASSERT_EQ(window.pinnedMeasurements.size(), 1u);
		EXPECT_FALSE(window.pinnedMeasurements[0].linkBroken);
		EXPECT_TRUE(window.pinnedMeasurements[0].frozenAtomPositions.empty());
		EXPECT_TRUE(warnings.empty());
	}

	TEST(SceneObjectPersistenceTests, BrokenLinkFreezesAnchorWarnsAndSurvivesResave)
	{
		RendererWindowState window = MakeWindow();
		PersistedPinnedMeasurement pin = MakeBondPin("broken");
		pin.atomRefs[1] = {1, "O", glm::vec3(9.0f, 9.0f, 9.0f)};

		std::vector<StructuredError> warnings;
		ApplyPersistedSceneObjects(window, {pin}, warnings);
		ASSERT_EQ(window.pinnedMeasurements.size(), 1u);
		const auto &loaded = window.pinnedMeasurements[0];
		EXPECT_TRUE(loaded.linkBroken);
		EXPECT_EQ(loaded.frozenAtomPositions, (std::vector<glm::vec3>{glm::vec3(0.0f), glm::vec3(9.0f)}));
		EXPECT_EQ(loaded.frozenAtomElements, (std::vector<std::string>{"C", "O"}));
		ASSERT_EQ(warnings.size(), 1u);
		EXPECT_EQ(warnings[0].code, "scene_objects.link_broken");
		EXPECT_EQ(warnings[0].severity, Severity::Warning);

		const std::vector<PersistedSceneObject> resaved = ExtractPersistedSceneObjects(window);
		ASSERT_EQ(resaved.size(), 1u);
		const auto &out = std::get<PersistedPinnedMeasurement>(resaved[0]);
		EXPECT_TRUE(out.linkBroken);
		ASSERT_EQ(out.atomRefs.size(), 2u);
		EXPECT_EQ(out.atomRefs[1].element, "O");
		EXPECT_EQ(out.atomRefs[1].position, glm::vec3(9.0f));
	}

	TEST(SceneObjectPersistenceTests, ExtractThenApplyRoundTripsEveryKind)
	{
		RendererWindowState source = MakeWindow();
		RendererWindowState::PinnedMeasurement pin;
		pin.atomIndices = {0, 1, 2};
		pin.worldOffset = glm::vec3(0.0f, 0.5f, 0.0f);
		pin.style.scale = 2.0f;
		source.pinnedMeasurements.push_back(pin);
		RendererWindowState::FreeLabel label;
		label.text = "hi";
		label.worldPosition = glm::vec3(1.0f);
		source.freeLabels.push_back(label);
		PersistedSceneArrow arrow;
		arrow.kind = PersistedArrowKind::Line;
		arrow.endTip = "None";
		arrow.points[1] = glm::vec3(2.0f, 0.0f, 0.0f);
		arrow.startAnchorAtoms = {{0, "C", glm::vec3(0)}};
		arrow.endAnchorAtoms = {{1, "N", glm::vec3(1.5f, 0, 0)}};
		arrow.atomBuffer = 0.75f;
		arrow.style.alpha = 0.5f;
		const auto migrated = MigrateArrowToPath(arrow);
		ASSERT_TRUE(migrated);
		(void)SceneSystem::AppendScenePath(source, migrated.Value().path);
		EnsureScenePersistKeys(source);

		const std::vector<PersistedSceneObject> saved = ExtractPersistedSceneObjects(source);
		ASSERT_EQ(saved.size(), 3u);
		const auto &savedPin = std::get<PersistedPinnedMeasurement>(saved[0]);
		ASSERT_EQ(savedPin.atomRefs.size(), 3u);
		EXPECT_EQ(savedPin.atomRefs[2].element, "C");
		EXPECT_EQ(savedPin.atomRefs[2].position, glm::vec3(0.0f, 1.5f, 0.0f));

		RendererWindowState target = MakeWindow();
		std::vector<StructuredError> warnings;
		ApplyPersistedSceneObjects(target, saved, warnings);
		EXPECT_TRUE(warnings.empty());
		ASSERT_EQ(target.pinnedMeasurements.size(), 1u);
		EXPECT_EQ(target.pinnedMeasurements[0].atomIndices, pin.atomIndices);
		EXPECT_EQ(target.pinnedMeasurements[0].worldOffset, pin.worldOffset);
		EXPECT_FLOAT_EQ(target.pinnedMeasurements[0].style.scale, 2.0f);
		EXPECT_EQ(target.pinnedMeasurements[0].persistKey, source.pinnedMeasurements[0].persistKey);
		ASSERT_EQ(target.freeLabels.size(), 1u);
		EXPECT_EQ(target.freeLabels[0].text, "hi");
		EXPECT_EQ(target.freeLabels[0].worldPosition, glm::vec3(1.0f));
		ASSERT_NE(target.paths, nullptr);
		ASSERT_EQ(target.paths->Store().Size(), 1u);
		const ScenePath &path = *target.paths->Store().At(0);
		EXPECT_EQ(path.transform.position + path.nodes.back().position, arrow.points.back());
		EXPECT_EQ(std::get<PathBinding::CopyPosition>(path.nodes[0].binding.value).atomIndex, 0u);
		EXPECT_EQ(std::get<PathBinding::CopyPosition>(path.nodes[1].binding.value).atomIndex, 1u);
		EXPECT_FLOAT_EQ(std::get<PathBinding::CopyPosition>(path.nodes[0].binding.value).buffer, 0.75f);
		EXPECT_FLOAT_EQ(path.style.alpha, 0.5f);
	}

	TEST(SceneObjectPersistenceTests, HalfBoundPathKeepsTheFreeEndThroughProjectFileRoundTrip)
	{
		RendererWindowState source = MakeWindow();
		ScenePath path;
		path.nodes = {{PathElementId{1}, glm::vec3(-2.0f, 1.0f, 0.0f), {}},
			{PathElementId{2}, glm::vec3(1.25f, 0.0f, 0.0f),
				PathBinding{PathBinding::CopyPosition{1, glm::vec3(0.0f), 0.75f}}}};
		path.segments = {{PathElementId{3}, LineSegmentData{}}};
		path.nextElementId = 4;
		const auto id = SceneSystem::AppendScenePath(source, path);
		const auto before = ResolveNodePositions(*source.paths->Store().Find(id), SceneSystem::MakePathBindingContext(source));
		SceneObjectsFile file;
		file.structures.push_back({"structure", ExtractPersistedSceneObjects(source)});
		SceneObjectsFile parsed;
		std::vector<StructuredError> warnings;
		std::string error;
		ASSERT_TRUE(SceneObjectsIO::Parse(SceneObjectsIO::Serialize(file), parsed, warnings, error)) << error;
		ASSERT_TRUE(warnings.empty());
		RendererWindowState target = MakeWindow();
		ApplyPersistedSceneObjects(target, parsed.structures[0].objects, warnings);
		ASSERT_TRUE(warnings.empty());
		ASSERT_NE(target.paths, nullptr);
		ASSERT_EQ(target.paths->Store().Size(), 1u);
		const ScenePath &loaded = *target.paths->Store().At(0);
		EXPECT_TRUE(std::holds_alternative<PathBinding::Free>(loaded.nodes[0].binding.value));
		const auto &binding = std::get<PathBinding::CopyPosition>(loaded.nodes[1].binding.value);
		EXPECT_EQ(binding.atomIndex, 1u);
		EXPECT_FLOAT_EQ(binding.buffer, 0.75f);
		const auto after = ResolveNodePositions(loaded, SceneSystem::MakePathBindingContext(target));
		EXPECT_EQ(after.positions, before.positions);
	}

	TEST(SceneObjectPersistenceTests, OrbitalSceneDecorationsSurviveProjectFileRoundTrip)
	{
		RendererWindowState source = MakeWindow();
		RendererWindowState::SceneOrbital orbital;
		orbital.preset = OrbitalPreset::Sp3Sigma;
		orbital.centerA = glm::vec3(0.0f);
		orbital.centerB = glm::vec3(1.5f, 0.0f, 0.0f);
		orbital.stretch = glm::vec3(0.6f, 1.4f, 2.25f);
		orbital.phaseFlipped = true;
		source.sceneOrbitals.push_back(orbital);

		SceneObjectsFile file;
		file.structures.push_back({"structure", ExtractPersistedSceneObjects(source)});
		SceneObjectsFile parsed;
		std::vector<StructuredError> ioWarnings;
		std::string error;
		ASSERT_TRUE(SceneObjectsIO::Parse(SceneObjectsIO::Serialize(file), parsed, ioWarnings, error)) << error;
		ASSERT_TRUE(ioWarnings.empty());

		RendererWindowState target = MakeWindow();
		std::vector<StructuredError> applyWarnings;
		ApplyPersistedSceneObjects(target, parsed.structures.at(0).objects, applyWarnings);

		EXPECT_TRUE(applyWarnings.empty());
		ASSERT_EQ(target.sceneOrbitals.size(), 1u);
		EXPECT_EQ(target.sceneOrbitals[0].stretch, orbital.stretch);
		EXPECT_TRUE(target.sceneOrbitals[0].phaseFlipped);
	}

	TEST(SceneObjectPersistenceTests, ArrowEndpointReferencesRebindAfterAtomsAreReordered)
	{
		RendererWindowState source = MakeWindow();
		PersistedSceneArrow arrow;
		arrow.startAnchorAtoms = {{0, "C", source.structure.atoms[0].cartesianPosition}};
		arrow.endAnchorAtoms = {{1, "N", source.structure.atoms[1].cartesianPosition}};
		const std::vector<PersistedSceneObject> saved = {arrow};

		RendererWindowState target = MakeWindow();
		std::rotate(target.structure.atoms.begin(), target.structure.atoms.begin() + 1, target.structure.atoms.end());
		std::vector<StructuredError> warnings;
		ApplyPersistedSceneObjects(target, saved, warnings);

		EXPECT_TRUE(warnings.empty());
		ASSERT_NE(target.paths, nullptr);
		const ScenePath &path = *target.paths->Store().At(0);
		EXPECT_EQ(std::get<PathBinding::CopyPosition>(path.nodes[0].binding.value).atomIndex, 2u);
		EXPECT_EQ(std::get<PathBinding::CopyPosition>(path.nodes[1].binding.value).atomIndex, 0u);
	}

	TEST(SceneObjectPersistenceTests, AnchoredPlaneSurvivesExtractAndApply)
	{
		RendererWindowState source = MakeWindow();
		RendererWindowState::ScenePlane plane;
		plane.anchorAtoms = {0, 1, 2};
		plane.center = glm::vec3(0.5f, 0.5f, 0.0f);
		source.scenePlanes.push_back(plane);
		EnsureScenePersistKeys(source);

		const std::vector<PersistedSceneObject> saved = ExtractPersistedSceneObjects(source);
		ASSERT_EQ(saved.size(), 1u);
		const auto &savedPlane = std::get<PersistedScenePlane>(saved[0]);
		ASSERT_EQ(savedPlane.anchorAtoms.size(), 3u);
		EXPECT_EQ(savedPlane.anchorAtoms[0].element, "C");
		EXPECT_EQ(savedPlane.anchorAtoms[1].element, "N");
		EXPECT_EQ(savedPlane.anchorAtoms[2].position, glm::vec3(0.0f, 1.5f, 0.0f));

		RendererWindowState target = MakeWindow();
		std::vector<StructuredError> warnings;
		ApplyPersistedSceneObjects(target, saved, warnings);

		EXPECT_TRUE(warnings.empty());
		ASSERT_EQ(target.scenePlanes.size(), 1u);
		EXPECT_EQ(target.scenePlanes[0].anchorAtoms, (std::vector<std::size_t>{0, 1, 2}));
		EXPECT_EQ(target.scenePlanes[0].center, plane.center);
	}

	TEST(SceneObjectPersistenceTests, MergeDedupsByKeyMostRecentWinsAndKeepsDeletedElsewhere)
	{
		const std::vector<PersistedSceneObject> older = {MakeLabel("a", "a-old"), MakeLabel("b", "b-only-in-older"), MakeLabel("", "anon1")};
		const std::vector<PersistedSceneObject> newer = {MakeLabel("a", "a-new"), MakeLabel("c", "c-new"), MakeLabel("", "anon2")};

		const std::vector<PersistedSceneObject> merged = MergeWindowSceneObjects({older, newer});
		std::vector<std::string> texts;
		for (const PersistedSceneObject &object : merged)
			texts.push_back(std::get<PersistedFreeLabel>(object).text);
		EXPECT_EQ(texts, (std::vector<std::string>{"a-new", "b-only-in-older", "anon1", "c-new", "anon2"}));
	}
} // namespace DefectStudio::Tests
