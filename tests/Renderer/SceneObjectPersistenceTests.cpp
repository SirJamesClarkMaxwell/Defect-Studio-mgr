#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>

#include "Renderer/Scene/SceneObjectPersistence.hpp"

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

	TEST(SceneObjectPersistenceTests, PersistKeysAreFilledOnceAndAreHex128)
	{
		RendererWindowState window = MakeWindow();
		window.freeLabels.push_back({});
		window.sceneArrows.push_back({});
		window.sceneArrows.push_back({});
		window.sceneArrows.back().persistKey = "keepme";
		EnsureScenePersistKeys(window);

		const std::string key = window.freeLabels[0].persistKey;
		ASSERT_EQ(key.size(), 32u);
		EXPECT_TRUE(std::all_of(key.begin(), key.end(), [](unsigned char c) { return std::isdigit(c) || (c >= 'a' && c <= 'f'); }));
		EXPECT_NE(window.sceneArrows[0].persistKey, key);
		EXPECT_EQ(window.sceneArrows[1].persistKey, "keepme");
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
		window.pinnedMeasurementUndoHistory.push_back({});

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
		EXPECT_TRUE(window.pinnedMeasurementUndoHistory.empty());
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
		RendererWindowState::SceneArrow arrow;
		arrow.kind = RendererWindowState::ArrowKind::Line;
		arrow.fixedPlane = RendererWindowState::WorldPlane::XZ;
		arrow.end = glm::vec3(2.0f, 0.0f, 0.0f);
		arrow.style.alpha = 0.5f;
		source.sceneArrows.push_back(arrow);
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
		ASSERT_EQ(target.sceneArrows.size(), 1u);
		EXPECT_EQ(target.sceneArrows[0].kind, RendererWindowState::ArrowKind::Line);
		EXPECT_EQ(target.sceneArrows[0].fixedPlane, RendererWindowState::WorldPlane::XZ);
		EXPECT_EQ(target.sceneArrows[0].end, arrow.end);
		EXPECT_FLOAT_EQ(target.sceneArrows[0].style.alpha, 0.5f);
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
