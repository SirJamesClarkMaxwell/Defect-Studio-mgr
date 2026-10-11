#include "Core/dspch.hpp"

#include <gtest/gtest.h>
#include <limits>

#include "Presentation/Operators/SceneOperatorRegistry.hpp"
#include "Presentation/Panels/OperatorRedoPanel.hpp"
#include "Presentation/Panels/SceneHideVolumeEditor.hpp"
#include "Renderer/RendererLayer.hpp"
#include "Renderer/Scene/SceneSystem.hpp"

namespace DefectStudio::Tests
{
	class HideVolumeOperatorTests : public testing::Test
	{
	protected:
		void SetUp() override
		{
			renderer.BindUndoStack(undo);
			ASSERT_TRUE(RegisterHideVolumeOperator(registry));
			RendererWindowState window;
			window.windowId = "hide-volume-redo";
			window.structure.lattice = glm::mat3(20);
			window.structure.atoms = {{"C", {0, 0, 0}}, {"C", {3, 0, 0}}, {"C", {9, 0, 0}}};
			renderer.AddWindow(std::move(window));
			SceneSystem::SyncSceneWithStructure(Window().sceneRegistry, Window().structure);
			SceneSystem::PushSelectionAndVisibilityToWindowState(Window().sceneRegistry, Window());
		}
		void TearDown() override { renderer.OnDetach(); }
		RendererWindowState &Window() { return renderer.GetWindows().front(); }
		const SceneOperator &Operator() { return *registry.Find("scene.hide_volume"); }
		Ref<UndoStack> undo = CreateRef<UndoStack>();
		RendererLayer renderer{[] { RendererStartupConfig config; config.loadDefaultScene = false; return config; }()};
		SceneOperatorRegistry registry;
		OperatorRedoPanel panel;
	};

	TEST_F(HideVolumeOperatorTests, ShapeControlsAreRelevantAndDefaultsComeFromTheEditor)
	{
		for (const auto kind : {HideVolumeKind::Sphere, HideVolumeKind::Box, HideVolumeKind::Cylinder})
		{
			const auto defaults = MakeHideVolumeOperatorValues(Window(), kind, 1u);
			const auto reference = MakeSceneHideVolume(Window(), kind, 1u);
			EXPECT_FLOAT_EQ(std::get<float>(defaults.at("radius")), reference.halfExtents.x);
			EXPECT_EQ(std::get<bool>(defaults.at("invert")), reference.invert);
			EXPECT_EQ(Operator().isParameterRelevant("radius", defaults, Window()), kind != HideVolumeKind::Box);
			EXPECT_EQ(Operator().isParameterRelevant("halfHeight", defaults, Window()), kind == HideVolumeKind::Cylinder);
			for (const char *key : {"halfX", "halfY", "halfZ"})
				EXPECT_EQ(Operator().isParameterRelevant(key, defaults, Window()), kind == HideVolumeKind::Box);
		}
	}

	TEST_F(HideVolumeOperatorTests, ReapplyReplacesTheVolumeKeepsItsAtomAndUsesOneUndoEntry)
	{
		const auto initial = MakeHideVolumeOperatorValues(Window(), HideVolumeKind::Sphere, 1u, glm::vec3(100));
		ASSERT_TRUE(panel.RunAndOpen(Operator(), Window(), initial));
		EXPECT_TRUE(panel.IsOpen());
		EXPECT_EQ(undo->GetUndoDepth(), 1u);
		ASSERT_EQ(Window().sceneHideVolumes.size(), 1u);
		EXPECT_EQ(Window().sceneHideVolumes.front().color, glm::vec3(0, 1, 1));
		EXPECT_FLOAT_EQ(Window().sceneHideVolumes.front().alpha, 0.75f);
		EXPECT_EQ(Window().sceneHideVolumes.front().anchorAtoms, std::vector<std::size_t>{1});
		EXPECT_FALSE(Window().structure.atoms[2].visible);
		auto values = panel.Values();
		values["radius"] = 8.0f;
		ASSERT_TRUE(panel.Reapply(Window(), values));
		ASSERT_EQ(Window().sceneHideVolumes.size(), 1u);
		EXPECT_TRUE(Window().structure.atoms[2].visible);
		EXPECT_EQ(Window().sceneHideVolumes.front().anchorAtoms, std::vector<std::size_t>{1});
		values["kind"] = static_cast<int>(HideVolumeKind::Box);
		values["halfX"] = 2.0f;
		ASSERT_TRUE(panel.Reapply(Window(), values));
		ASSERT_EQ(Window().sceneHideVolumes.size(), 1u);
		EXPECT_EQ(Window().sceneHideVolumes.front().kind, HideVolumeKind::Box);
		EXPECT_FLOAT_EQ(Window().sceneHideVolumes.front().halfExtents.x, 2.0f);
		EXPECT_EQ(undo->GetUndoDepth(), 1u);
		ASSERT_TRUE(undo->Undo());
		EXPECT_TRUE(Window().sceneHideVolumes.empty());
		EXPECT_TRUE(Window().structure.atoms[2].visible);
		ASSERT_TRUE(undo->Redo());
		ASSERT_EQ(Window().sceneHideVolumes.size(), 1u);
		EXPECT_EQ(Window().sceneHideVolumes.front().kind, HideVolumeKind::Box);
	}

	TEST_F(HideVolumeOperatorTests, ReapplyKeepsAnUnanchoredPointAndConvertsTheCentreToFractionalSpace)
	{
		const glm::vec3 point(4, 6, 8);
		ASSERT_TRUE(panel.RunAndOpen(Operator(), Window(),
			MakeHideVolumeOperatorValues(Window(), HideVolumeKind::Cylinder, std::nullopt, point)));
		auto values = panel.Values();
		values["halfHeight"] = 2.0f;
		values["frame"] = static_cast<int>(HideVolumeFrame::Fractional);
		ASSERT_TRUE(panel.Reapply(Window(), values));
		ASSERT_EQ(Window().sceneHideVolumes.size(), 1u);
		const auto &volume = Window().sceneHideVolumes.front();
		EXPECT_TRUE(volume.anchorAtoms.empty());
		EXPECT_EQ(volume.center, glm::inverse(Window().structure.lattice) * point);
		EXPECT_FLOAT_EQ(volume.halfExtents.z, 2.0f);
		EXPECT_LT(glm::distance(HideVolumeWorldCenter(volume, Window().structure), point), 1e-5f);
	}

	TEST_F(HideVolumeOperatorTests, InvalidParametersPreserveTheLastGoodVolume)
	{
		ASSERT_TRUE(panel.RunAndOpen(Operator(), Window(), MakeHideVolumeOperatorValues(Window(), HideVolumeKind::Sphere, 0u)));
		auto values = panel.Values();
		values["radius"] = std::numeric_limits<float>::quiet_NaN();
		EXPECT_FALSE(panel.Reapply(Window(), values));
		EXPECT_FALSE(panel.IsOpen());
		ASSERT_EQ(Window().sceneHideVolumes.size(), 1u);
		EXPECT_FLOAT_EQ(Window().sceneHideVolumes.front().halfExtents.x, kDefaultHideVolumeRadius);
		EXPECT_EQ(undo->GetUndoDepth(), 1u);
	}

	TEST_F(HideVolumeOperatorTests, ActiveToggleIsUndoableAndRefreshesVisibilityInBothDirections)
	{
		AddSceneHideVolume(Window(), HideVolumeKind::Sphere, 0u);
		const auto id = Window().sceneHideVolumes.front().id;
		ASSERT_FALSE(Window().structure.atoms[2].visible);
		SetSceneHideVolumeActive(Window(), id, false);
		EXPECT_EQ(undo->GetUndoDepth(), 2u);
		EXPECT_TRUE(Window().structure.atoms[2].visible);
		ASSERT_TRUE(undo->Undo());
		EXPECT_TRUE(Window().sceneHideVolumes.front().active);
		EXPECT_FALSE(Window().structure.atoms[2].visible);
		ASSERT_TRUE(undo->Redo());
		EXPECT_FALSE(Window().sceneHideVolumes.front().active);
		EXPECT_TRUE(Window().structure.atoms[2].visible);
	}

	TEST_F(HideVolumeOperatorTests, InvalidFractionalCellPushesNothingAndDoesNotCreateAVolume)
	{
		auto values = MakeHideVolumeOperatorValues(Window(), HideVolumeKind::Box);
		values["frame"] = static_cast<int>(HideVolumeFrame::Fractional);
		Window().structure.lattice = glm::mat3(0);
		EXPECT_FALSE(panel.RunAndOpen(Operator(), Window(), values));
		EXPECT_FALSE(panel.IsOpen());
		EXPECT_TRUE(Window().sceneHideVolumes.empty());
		EXPECT_EQ(undo->GetUndoDepth(), 0u);
	}
}
