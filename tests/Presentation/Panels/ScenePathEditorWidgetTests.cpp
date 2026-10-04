#include "Core/dspch.hpp"

#include <cmath>
#include <limits>

#include <gtest/gtest.h>

#include <glm/gtc/quaternion.hpp>

#include "Presentation/Panels/ScenePathEditorWidget.hpp"
#include "Renderer/Path/PathStrokeMesher.hpp"
#include "Renderer/Scene/SceneSystem.hpp"

namespace DefectStudio::Tests
{
	TEST(ScenePathEditorWidgetTests, MeshOverlayDefaultsOff)
	{
		EXPECT_FALSE(RendererWindowState{}.showPathMeshOverlay);
	}

	namespace
	{
		ScenePath MakePath(RendererWindowState &window, const char *name, float width)
		{
			ScenePath path;
			path.id = window.sceneRegistry.AllocateObjectId();
			path.name = name;
			path.style.width = width;
			PathNode first;
			first.id = AllocateElementId(path);
			first.position = glm::vec3(0.0f);
			PathNode second;
			second.id = AllocateElementId(path);
			second.position = glm::vec3(1.0f, 0.0f, 0.0f);
			path.nodes = {first, second};
			PathSegment segment;
			segment.id = AllocateElementId(path);
			segment.data = LineSegmentData{};
			path.segments = {segment};
			return path;
		}

		void AddPaths(RendererWindowState &window)
		{
			PathSystem &paths = SceneSystem::EnsurePathSystem(window);
			paths.Store().Insert(MakePath(window, "First", 0.05f));
			paths.Store().Insert(MakePath(window, "Second", 0.15f));
		}

		void ExpectDashEqual(const PathDashStyle &expected, const PathDashStyle &actual)
		{
			EXPECT_EQ(actual.enabled, expected.enabled);
			EXPECT_FLOAT_EQ(actual.dashLength, expected.dashLength);
			EXPECT_FLOAT_EQ(actual.gapLength, expected.gapLength);
			EXPECT_FLOAT_EQ(actual.phase, expected.phase);
		}

		void ExpectGradientEqual(const PathGradient &expected, const PathGradient &actual)
		{
			EXPECT_EQ(actual.enabled, expected.enabled);
			ASSERT_EQ(actual.stops.size(), expected.stops.size());
			for (std::size_t index = 0; index < expected.stops.size(); ++index)
			{
				EXPECT_FLOAT_EQ(actual.stops[index].position, expected.stops[index].position);
				EXPECT_EQ(actual.stops[index].color, expected.stops[index].color);
				EXPECT_FLOAT_EQ(actual.stops[index].alpha, expected.stops[index].alpha);
			}
		}

		void ExpectEquivalentRotation(const glm::quat &actual, const glm::quat &expected)
		{
			for (const glm::vec3 basis : {glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f)})
				EXPECT_NEAR(glm::distance(actual * basis, expected * basis), 0.0f, 1.0e-5f);
		}
	}

	TEST(ScenePathEditorWidgetTests, BevelPartsApplyAndResolveMixedSelections)
	{
		RendererWindowState window;
		AddPaths(window);
		const std::vector<SceneObjectId> ids{SceneObjectId{1}, SceneObjectId{2}};
		auto edit = ResolveScenePathStyleEdit(window, ids).values;
		edit.ribbonBevelParts = PathBevelParts::Shaft;
		EXPECT_EQ(ApplyScenePathStyleEdit(window, ids, edit, false), 2u);
		EXPECT_EQ(ResolveScenePathStyleEdit(window, ids).values.ribbonBevelParts, PathBevelParts::Shaft);
		window.paths->Store().MutateStyle(ids.back(), [](ScenePath &path) {
			path.style.ribbonBevelParts = PathBevelParts::Decorations;
		});
		EXPECT_TRUE(ResolveScenePathStyleEdit(window, ids).mixedRibbonBevelParts);
	}

	TEST(ScenePathEditorWidgetTests, EmptySelectionUsesDefaults)
	{
		RendererWindowState window;
		AddPaths(window);
		const ScenePathStyleEditState state = ResolveScenePathStyleEdit(window, {});
		EXPECT_EQ(state.resolved, 0u);
		EXPECT_FALSE(state.mixedWidth);
	}

	TEST(ScenePathEditorWidgetTests, ResolvesFirstPathAndMarksOnlyDifferingFieldsMixed)
	{
		RendererWindowState window;
		AddPaths(window);
		const std::vector<SceneObjectId> ids = window.paths->Store().Ids();
		ASSERT_GE(ids.size(), 2u);
		const ScenePathStyleEditState one = ResolveScenePathStyleEdit(window, {ids[0]});
		EXPECT_EQ(one.resolved, 1u);
		EXPECT_FLOAT_EQ(one.values.width, 0.05f);
		EXPECT_FALSE(one.mixedWidth);

		const ScenePathStyleEditState two = ResolveScenePathStyleEdit(window, ids);
		EXPECT_EQ(two.resolved, 2u);
		EXPECT_TRUE(two.mixedWidth);
		EXPECT_FALSE(two.mixedAlpha);
	}

	TEST(ScenePathEditorWidgetTests, ResolvesFirstTransformAndMarksOnlyDifferingComponentsMixed)
	{
		RendererWindowState window;
		AddPaths(window);
		const std::vector<SceneObjectId> ids = window.paths->Store().Ids();
		ASSERT_GE(ids.size(), 2u);
		const glm::quat firstRotation = glm::normalize(glm::angleAxis(
			glm::radians(37.0f), glm::normalize(glm::vec3(1.0f, -2.0f, 3.0f))));
		const glm::quat otherRotation = glm::normalize(glm::angleAxis(
			glm::radians(61.0f), glm::normalize(glm::vec3(-2.0f, 1.0f, 4.0f))));
		window.paths->Store().MutateGeometry(ids[0], [&](ScenePath &path) {
			path.transform.position = glm::vec3(3.0f, -4.0f, 2.0f);
			path.transform.rotation = firstRotation;
			path.transform.scale = glm::vec3(1.5f, 0.75f, 2.0f);
		});
		window.paths->Store().MutateGeometry(ids[1], [&](ScenePath &path) {
			path.transform.position = glm::vec3(-3.0f, 4.0f, -2.0f);
			path.transform.rotation = otherRotation;
			path.transform.scale = glm::vec3(1.5f, 0.75f, 2.0f);
		});

		const ScenePathTransformEditState state = ResolveScenePathTransformEdit(window, ids);
		ASSERT_EQ(state.resolved, 2u);
		EXPECT_EQ(state.values.position, glm::vec3(3.0f, -4.0f, 2.0f));
		EXPECT_EQ(state.values.scale, glm::vec3(1.5f, 0.75f, 2.0f));
		const glm::vec3 expectedEuler = glm::degrees(glm::eulerAngles(firstRotation));
		EXPECT_NEAR(glm::distance(state.values.rotationDegrees, expectedEuler), 0.0f, 1.0e-5f);
		EXPECT_TRUE(state.mixedPosition);
		EXPECT_TRUE(state.mixedRotation);
		EXPECT_FALSE(state.mixedScale);
	}

	TEST(ScenePathEditorWidgetTests, AppliesTransformToEverySelectedPathAndReportsChangedCount)
	{
		RendererWindowState window;
		AddPaths(window);
		const std::vector<SceneObjectId> ids = window.paths->Store().Ids();
		ASSERT_GE(ids.size(), 2u);
		ScenePathTransformEdit edit;
		edit.position = glm::vec3(7.0f, -1.0f, 4.0f);
		edit.rotationDegrees = glm::vec3(23.0f, -41.0f, 67.0f);
		edit.scale = glm::vec3(2.0f, 0.5f, 1.25f);

		EXPECT_EQ(ApplyScenePathTransformEdit(window, {ids[0], ids[1], SceneObjectId{999}}, edit), 2u);
		const glm::quat expectedRotation = glm::normalize(glm::quat(glm::radians(edit.rotationDegrees)));
		for (const SceneObjectId id : {ids[0], ids[1]})
		{
			const ScenePath *path = window.paths->Store().Find(id);
			ASSERT_NE(path, nullptr);
			EXPECT_EQ(path->transform.position, edit.position);
			EXPECT_EQ(path->transform.scale, edit.scale);
			ExpectEquivalentRotation(path->transform.rotation, expectedRotation);
		}
	}

	TEST(ScenePathEditorWidgetTests, EulerTransformResolveRoundTripPreservesRotationAction)
	{
		RendererWindowState window;
		AddPaths(window);
		const std::vector<SceneObjectId> ids = window.paths->Store().Ids();
		ASSERT_FALSE(ids.empty());
		ScenePathTransformEdit edit;
		edit.rotationDegrees = glm::vec3(31.0f, -52.0f, 74.0f);
		ASSERT_EQ(ApplyScenePathTransformEdit(window, {ids.front()}, edit), 1u);
		const ScenePath *applied = window.paths->Store().Find(ids.front());
		ASSERT_NE(applied, nullptr);
		const ScenePathTransformEditState resolved = ResolveScenePathTransformEdit(window, {ids.front()});
		ASSERT_EQ(resolved.resolved, 1u);
		const glm::quat reconstructed = glm::normalize(glm::quat(glm::radians(resolved.values.rotationDegrees)));
		ExpectEquivalentRotation(reconstructed, applied->transform.rotation);
	}

	TEST(ScenePathEditorWidgetTests, ResolvesDashAndGradientFromFirstResolvedPath)
	{
		RendererWindowState window;
		AddPaths(window);
		const std::vector<SceneObjectId> ids = window.paths->Store().Ids();
		ASSERT_GE(ids.size(), 2u);
		const SceneObjectId firstSelected = ids[1];
		const SceneObjectId otherSelected = ids[0];
		const PathDashStyle dash{true, 0.37f, 0.19f, -0.04f};
		const PathGradient gradient{true, {
			{0.2f, glm::vec3(0.1f, 0.2f, 0.3f), 0.4f},
			{0.8f, glm::vec3(0.7f, 0.6f, 0.5f), 0.9f}}};
		window.paths->Store().MutateStyle(firstSelected, [&](ScenePath &path) {
			path.style.dash = dash;
			path.style.gradient = gradient;
		});
		window.paths->Store().MutateStyle(otherSelected, [](ScenePath &path) {
			path.style.dash = {false, 0.01f, 0.02f, 0.03f};
			path.style.gradient = {false, {}};
		});

		const ScenePathStyleEditState state = ResolveScenePathStyleEdit(window, {firstSelected, otherSelected});
		ExpectDashEqual(dash, state.values.dash);
		ExpectGradientEqual(gradient, state.values.gradient);
	}

	TEST(ScenePathEditorWidgetTests, MarksDashAndGradientFieldsMixedIndependently)
	{
		RendererWindowState window;
		AddPaths(window);
		const std::vector<SceneObjectId> ids = window.paths->Store().Ids();
		ASSERT_GE(ids.size(), 2u);
		const SceneObjectId first = ids[0];
		const SceneObjectId second = ids[1];
		const PathDashStyle dash{true, 0.3f, 0.15f, 0.02f};
		const PathGradient gradient{true, {
			{0.1f, glm::vec3(0.1f, 0.2f, 0.3f), 0.4f},
			{0.9f, glm::vec3(0.7f, 0.6f, 0.5f), 0.8f}}};
		const auto setDash = [&](const PathDashStyle &value) {
			window.paths->Store().MutateStyle(first, [&](ScenePath &path) { path.style.dash = value; });
			window.paths->Store().MutateStyle(second, [&](ScenePath &path) { path.style.dash = value; });
		};
		const auto setGradient = [&](const PathGradient &value) {
			window.paths->Store().MutateStyle(first, [&](ScenePath &path) { path.style.gradient = value; });
			window.paths->Store().MutateStyle(second, [&](ScenePath &path) { path.style.gradient = value; });
		};

		setDash(dash);
		setGradient(gradient);
		ScenePathStyleEditState state = ResolveScenePathStyleEdit(window, ids);
		EXPECT_FALSE(state.mixedDash);
		EXPECT_FALSE(state.mixedGradient);

		for (const auto &change : {
			PathDashStyle{false, dash.dashLength, dash.gapLength, dash.phase},
			PathDashStyle{dash.enabled, dash.dashLength + 0.1f, dash.gapLength, dash.phase},
			PathDashStyle{dash.enabled, dash.dashLength, dash.gapLength + 0.1f, dash.phase},
			PathDashStyle{dash.enabled, dash.dashLength, dash.gapLength, dash.phase + 0.1f}})
		{
			setDash(dash);
			window.paths->Store().MutateStyle(second, [&](ScenePath &path) { path.style.dash = change; });
			state = ResolveScenePathStyleEdit(window, ids);
			EXPECT_TRUE(state.mixedDash);
			EXPECT_FALSE(state.mixedGradient);
		}

		const auto expectGradientMixed = [&](const auto &change) {
			setDash(dash);
			setGradient(gradient);
			window.paths->Store().MutateStyle(second, [&](ScenePath &path) {
				change(path.style.gradient);
			});
			const ScenePathStyleEditState changed = ResolveScenePathStyleEdit(window, ids);
			EXPECT_FALSE(changed.mixedDash);
			EXPECT_TRUE(changed.mixedGradient);
		};
		expectGradientMixed([](PathGradient &value) {
			value.stops.push_back({1.0f, glm::vec3(0.2f, 0.3f, 0.4f), 0.5f});
		});
		expectGradientMixed([](PathGradient &value) {
			ASSERT_GE(value.stops.size(), 2u);
			value.stops[1].position = 0.8f;
		});
		expectGradientMixed([](PathGradient &value) {
			ASSERT_GE(value.stops.size(), 2u);
			value.stops[0].color.x = 0.9f;
		});
		expectGradientMixed([](PathGradient &value) {
			ASSERT_GE(value.stops.size(), 2u);
			value.stops[1].alpha = 0.2f;
		});
	}

	TEST(ScenePathEditorWidgetTests, ResolvesAndAppliesMixedRibbonNormals)
	{
		RendererWindowState window;
		AddPaths(window);
		const std::vector<SceneObjectId> ids = window.paths->Store().Ids();
		ASSERT_GE(ids.size(), 2u);
		window.paths->Store().MutateStyle(ids[0], [](ScenePath &path) {
			path.style.profile = StrokeProfile::Flat;
			path.style.ribbonNormal = glm::vec3(0.0f, 1.0f, 0.0f);
		});
		window.paths->Store().MutateStyle(ids[1], [](ScenePath &path) {
			path.style.profile = StrokeProfile::Flat;
			path.style.ribbonNormal = glm::vec3(1.0f, 0.0f, 0.0f);
		});
		const ScenePathStyleEditState state = ResolveScenePathStyleEdit(window, ids);
		EXPECT_TRUE(state.mixedRibbonNormal);
		ScenePathStyleEdit edit = state.values;
		edit.ribbonNormal = glm::vec3(0.0f, 0.0f, 1.0f);
		EXPECT_EQ(ApplyScenePathStyleEdit(window, ids, edit), 2u);
		for (const SceneObjectId id : ids)
		{
			const ScenePath *path = window.paths->Store().Find(id);
			ASSERT_NE(path, nullptr);
			EXPECT_EQ(path->style.ribbonNormal, edit.ribbonNormal);
		}
	}

	TEST(ScenePathEditorWidgetTests, ResolvesAndAppliesMixedShadeSmooth)
	{
		RendererWindowState window;
		AddPaths(window);
		const std::vector<SceneObjectId> ids = window.paths->Store().Ids();
		ASSERT_GE(ids.size(), 2u);
		window.paths->Store().MutateStyle(ids[0], [](ScenePath &path) { path.style.shadeSmooth = false; });
		window.paths->Store().MutateStyle(ids[1], [](ScenePath &path) { path.style.shadeSmooth = true; });

		const ScenePathStyleEditState state = ResolveScenePathStyleEdit(window, ids);
		EXPECT_TRUE(state.mixedShadeSmooth);
		ScenePathStyleEdit edit = state.values;
		edit.shadeSmooth = true;
		EXPECT_EQ(ApplyScenePathStyleEdit(window, ids, edit), 2u);
		for (const SceneObjectId id : ids)
		{
			const ScenePath *path = window.paths->Store().Find(id);
			ASSERT_NE(path, nullptr);
			EXPECT_TRUE(path->style.shadeSmooth);
		}
	}

	TEST(ScenePathEditorWidgetTests, ApplyWritesEveryLiveSelectionAndOneUnknownIsIgnored)
	{
		RendererWindowState window;
		AddPaths(window);
		const std::vector<SceneObjectId> ids = window.paths->Store().Ids();
		ASSERT_GE(ids.size(), 2u);
		ScenePathStyleEdit edit;
		edit.width = 0.3f;
		edit.alpha = 0.4f;
		EXPECT_EQ(ApplyScenePathStyleEdit(window, {ids[0], ids[1], SceneObjectId{999}}, edit), 2u);
		const ScenePath *first = window.paths->Store().Find(ids[0]);
		const ScenePath *second = window.paths->Store().Find(ids[1]);
		ASSERT_NE(first, nullptr);
		ASSERT_NE(second, nullptr);
		EXPECT_FLOAT_EQ(first->style.width, 0.3f);
		EXPECT_FLOAT_EQ(second->style.alpha, 0.4f);
	}

	TEST(ScenePathEditorWidgetTests, ApplyWritesAllStyleComboValuesToEverySelectedPath)
	{
		RendererWindowState window;
		AddPaths(window);
		const std::vector<SceneObjectId> ids = window.paths->Store().Ids();
		ASSERT_GE(ids.size(), 2u);
		ScenePathStyleEdit edit;
		edit.profile = StrokeProfile::CameraFacing;
		edit.startDecoration = {PathDecorationKind::Bar, 2.0f, 0.5f, false};
		edit.endDecoration = {PathDecorationKind::Diamond, 1.5f, 2.5f, true};
		edit.depthMode = PathDepthMode::AlwaysOnTop;

		EXPECT_EQ(ApplyScenePathStyleEdit(window, ids, edit), 2u);
		for (const SceneObjectId id : ids)
		{
			const ScenePath *path = window.paths->Store().Find(id);
			ASSERT_NE(path, nullptr);
			EXPECT_EQ(path->style.profile, StrokeProfile::CameraFacing);
			EXPECT_EQ(path->style.startDecoration.kind, PathDecorationKind::Bar);
			EXPECT_FLOAT_EQ(path->style.startDecoration.lengthScale, 2.0f);
			EXPECT_FLOAT_EQ(path->style.startDecoration.widthScale, 0.5f);
			EXPECT_FALSE(path->style.startDecoration.filled);
			EXPECT_EQ(path->style.endDecoration.kind, PathDecorationKind::Diamond);
			EXPECT_FLOAT_EQ(path->style.endDecoration.lengthScale, 1.5f);
			EXPECT_FLOAT_EQ(path->style.endDecoration.widthScale, 2.5f);
			EXPECT_TRUE(path->style.endDecoration.filled);
			EXPECT_EQ(path->style.depthMode, PathDepthMode::AlwaysOnTop);
		}
	}

	TEST(ScenePathEditorWidgetTests, ResolvesLineStylePresetsAndCustomPatterns)
	{
		PathDashStyle solid;
		solid.enabled = false;
		EXPECT_EQ(ResolveScenePathLineStyle(solid), ScenePathLineStyle::Solid);

		for (const ScenePathLineStyle style : {ScenePathLineStyle::Dashed, ScenePathLineStyle::Dotted})
		{
			PathDashStyle applied;
			ApplyScenePathLineStyle(applied, style, 0.2f);
			EXPECT_EQ(ResolveScenePathLineStyle(applied), style);
		}

		const PathDashStyle custom{true, 0.3f, 0.4f, 0.1f};
		EXPECT_EQ(ResolveScenePathLineStyle(custom), ScenePathLineStyle::Custom);
	}

	TEST(ScenePathEditorWidgetTests, LineStylePresetsRoundTripAtSeveralStrokeWidths)
	{
		for (const ScenePathLineStyle style : {
			ScenePathLineStyle::Solid, ScenePathLineStyle::Dashed, ScenePathLineStyle::Dotted})
		{
			for (const float strokeWidth : {0.02f, 0.1f, 0.8f})
			{
				PathDashStyle dash;
				ApplyScenePathLineStyle(dash, style, strokeWidth);
				EXPECT_EQ(ResolveScenePathLineStyle(dash), style);
			}
		}
	}

	TEST(ScenePathEditorWidgetTests, LineStylePresetLengthsScaleWithStrokeWidth)
	{
		PathDashStyle thin;
		PathDashStyle thick;
		ApplyScenePathLineStyle(thin, ScenePathLineStyle::Dotted, 0.2f);
		ApplyScenePathLineStyle(thick, ScenePathLineStyle::Dotted, 0.4f);
		ASSERT_NE(thin.dashLength, 0.0f);
		ASSERT_NE(thin.gapLength, 0.0f);
		EXPECT_FLOAT_EQ(thick.dashLength / thin.dashLength, 2.0f);
		EXPECT_FLOAT_EQ(thick.gapLength / thin.gapLength, 2.0f);
	}

	TEST(ScenePathEditorWidgetTests, CustomLineStyleLeavesDashUntouched)
	{
		PathDashStyle dash{true, 0.31f, 0.17f, -0.09f};
		const PathDashStyle before = dash;
		ApplyScenePathLineStyle(dash, ScenePathLineStyle::Custom, 0.4f);
		ExpectDashEqual(before, dash);
	}

	TEST(ScenePathEditorWidgetTests, ApplyingGradientStopsKeepsPositionsFiniteBoundedAndOrdered)
	{
		RendererWindowState window;
		AddPaths(window);
		const std::vector<SceneObjectId> ids = window.paths->Store().Ids();
		ASSERT_FALSE(ids.empty());
		ScenePathStyleEdit edit;
		edit.gradient.enabled = true;
		edit.gradient.stops = {
			{std::numeric_limits<float>::quiet_NaN(), glm::vec3(1.0f, 0.0f, 0.0f), 1.0f},
			{1.2f, glm::vec3(0.0f, 1.0f, 0.0f), 0.8f},
			{-0.2f, glm::vec3(0.0f, 0.0f, 1.0f), 0.6f},
			{0.6f, glm::vec3(1.0f), 0.4f}};
		ASSERT_EQ(ApplyScenePathStyleEdit(window, {ids.front()}, edit), 1u);
		const ScenePath *path = window.paths->Store().Find(ids.front());
		ASSERT_NE(path, nullptr);
		ASSERT_EQ(path->style.gradient.stops.size(), edit.gradient.stops.size());
		float previous = 0.0f;
		for (const PathGradientStop &stop : path->style.gradient.stops)
		{
			EXPECT_TRUE(std::isfinite(stop.position));
			EXPECT_GE(stop.position, 0.0f);
			EXPECT_LE(stop.position, 1.0f);
			EXPECT_GE(stop.position, previous);
			previous = stop.position;
		}
	}

	TEST(ScenePathEditorWidgetTests, EmptyGradientEditDisablesTheGradient)
	{
		RendererWindowState window;
		AddPaths(window);
		const std::vector<SceneObjectId> ids = window.paths->Store().Ids();
		ASSERT_FALSE(ids.empty());
		ScenePathStyleEdit edit;
		edit.gradient = {true, {{0.0f, glm::vec3(1.0f, 0.0f, 0.0f), 1.0f}}};
		ASSERT_EQ(ApplyScenePathStyleEdit(window, {ids.front()}, edit), 1u);
		edit.gradient.stops.clear();
		edit.gradient.enabled = true;
		ASSERT_EQ(ApplyScenePathStyleEdit(window, {ids.front()}, edit), 1u);
		const ScenePath *path = window.paths->Store().Find(ids.front());
		ASSERT_NE(path, nullptr);
		EXPECT_FALSE(path->style.gradient.enabled);
		EXPECT_TRUE(path->style.gradient.stops.empty());
	}

	TEST(ScenePathEditorWidgetTests, DisablingGradientPreservesStopsAndUsesFlatColor)
	{
		RendererWindowState window;
		AddPaths(window);
		const std::vector<SceneObjectId> ids = window.paths->Store().Ids();
		ASSERT_FALSE(ids.empty());
		const glm::vec3 color{0.2f, 0.4f, 0.6f};
		const float alpha = 0.7f;
		const PathGradient gradient{true, {
			{0.0f, glm::vec3(1.0f, 0.0f, 0.0f), 0.2f},
			{1.0f, glm::vec3(0.0f, 0.0f, 1.0f), 0.9f}}};
		ScenePathStyleEdit edit;
		edit.color = color;
		edit.alpha = alpha;
		edit.gradient = gradient;
		ASSERT_EQ(ApplyScenePathStyleEdit(window, {ids.front()}, edit), 1u);
		edit.gradient.enabled = false;
		ASSERT_EQ(ApplyScenePathStyleEdit(window, {ids.front()}, edit), 1u);

		const ScenePath *path = window.paths->Store().Find(ids.front());
		ASSERT_NE(path, nullptr);
		PathGradient expectedGradient = gradient;
		expectedGradient.enabled = false;
		ExpectGradientEqual(expectedGradient, path->style.gradient);
		EXPECT_EQ(SampleStrokeColor(path->style, 0.5), glm::vec4(color, alpha));
	}

	TEST(ScenePathEditorWidgetTests, ResolvesWholeEndpointDecorationsAndMarksAllFieldDifferencesMixed)
	{
		RendererWindowState window;
		AddPaths(window);
		const std::vector<SceneObjectId> ids = window.paths->Store().Ids();
		ASSERT_GE(ids.size(), 2u);
		const PathEndpointDecoration start{PathDecorationKind::Latex, 2.0f, 0.75f, false};
		const PathEndpointDecoration end{PathDecorationKind::Kite, 1.25f, 1.5f, true};
		for (const SceneObjectId id : ids)
			window.paths->Store().MutateStyle(id, [&](ScenePath &path) {
				path.style.startDecoration = start;
				path.style.endDecoration = end;
			});

		const ScenePathStyleEditState resolved = ResolveScenePathStyleEdit(window, ids);
		EXPECT_EQ(resolved.values.startDecoration.kind, start.kind);
		EXPECT_FLOAT_EQ(resolved.values.startDecoration.lengthScale, start.lengthScale);
		EXPECT_FLOAT_EQ(resolved.values.startDecoration.widthScale, start.widthScale);
		EXPECT_EQ(resolved.values.startDecoration.filled, start.filled);
		EXPECT_EQ(resolved.values.endDecoration.kind, end.kind);
		EXPECT_FLOAT_EQ(resolved.values.endDecoration.lengthScale, end.lengthScale);
		EXPECT_FLOAT_EQ(resolved.values.endDecoration.widthScale, end.widthScale);
		EXPECT_EQ(resolved.values.endDecoration.filled, end.filled);
		EXPECT_FALSE(resolved.mixedStartDecoration);
		EXPECT_FALSE(resolved.mixedEndDecoration);

		const auto expectStartMixed = [&](const auto &change) {
			for (const SceneObjectId id : ids)
				window.paths->Store().MutateStyle(id, [&](ScenePath &path) { path.style.startDecoration = start; });
			window.paths->Store().MutateStyle(ids[1], [&](ScenePath &path) { change(path.style.startDecoration); });
			const ScenePathStyleEditState state = ResolveScenePathStyleEdit(window, ids);
			EXPECT_TRUE(state.mixedStartDecoration);
		};
		expectStartMixed([](PathEndpointDecoration &decoration) { decoration.kind = PathDecorationKind::Arrow; });
		expectStartMixed([](PathEndpointDecoration &decoration) { decoration.lengthScale = 3.0f; });
		expectStartMixed([](PathEndpointDecoration &decoration) { decoration.widthScale = 1.25f; });
		expectStartMixed([](PathEndpointDecoration &decoration) { decoration.filled = true; });

		const auto expectEndMixed = [&](const auto &change) {
			for (const SceneObjectId id : ids)
				window.paths->Store().MutateStyle(id, [&](ScenePath &path) { path.style.endDecoration = end; });
			window.paths->Store().MutateStyle(ids[1], [&](ScenePath &path) { change(path.style.endDecoration); });
			const ScenePathStyleEditState state = ResolveScenePathStyleEdit(window, ids);
			EXPECT_TRUE(state.mixedEndDecoration);
		};
		expectEndMixed([](PathEndpointDecoration &decoration) { decoration.kind = PathDecorationKind::Arrow; });
		expectEndMixed([](PathEndpointDecoration &decoration) { decoration.lengthScale = 2.0f; });
		expectEndMixed([](PathEndpointDecoration &decoration) { decoration.widthScale = 0.5f; });
		expectEndMixed([](PathEndpointDecoration &decoration) { decoration.filled = false; });
	}

	TEST(ScenePathEditorWidgetTests, DisplayNameUsesNameOrStoreIndex)
	{
		RendererWindowState window;
		AddPaths(window);
		ASSERT_GE(window.paths->Store().Size(), 2u);
		const ScenePath *named = window.paths->Store().At(0);
		ASSERT_NE(named, nullptr);
		EXPECT_EQ(ScenePathDisplayName(*named, 0), "First");
		const ScenePath *second = window.paths->Store().At(1);
		ASSERT_NE(second, nullptr);
		ScenePath unnamed = *second;
		unnamed.name.clear();
		EXPECT_EQ(ScenePathDisplayName(unnamed, 4), "Path #4");
	}

	TEST(ScenePathEditorWidgetTests, RenameUnknownFailsAndLiveRenameChangesName)
	{
		RendererWindowState window;
		AddPaths(window);
		const std::vector<SceneObjectId> ids = window.paths->Store().Ids();
		ASSERT_FALSE(ids.empty());
		const SceneObjectId id = ids.front();
		EXPECT_TRUE(RenameScenePath(window, id, "Renamed"));
		const ScenePath *renamed = window.paths->Store().Find(id);
		ASSERT_NE(renamed, nullptr);
		EXPECT_EQ(renamed->name, "Renamed");
		EXPECT_FALSE(RenameScenePath(window, SceneObjectId{999}, "Nope"));
	}
}
