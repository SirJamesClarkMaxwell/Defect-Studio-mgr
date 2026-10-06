#include "Core/dspch.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <tuple>
#include <gtest/gtest.h>

#include "Presentation/Operators/SceneOperatorRegistry.hpp"
#include "Presentation/Panels/ScenePathCurvedArrow.hpp"
#include "Renderer/Path/CurvedArrowParameters.hpp"
#include "Renderer/RendererLayer.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		SceneOperator MakeStubOperator(std::string id, std::string label)
		{
			SceneOperator op;
			op.id = std::move(id);
			op.label = std::move(label);
			op.execute = [](RendererWindowState &, const SceneOperatorValues &) {
				return Result<std::vector<SceneObjectId>>{std::vector<SceneObjectId>{}};
			};
			return op;
		}

		const SceneOperatorParameter *FindParameter(const SceneOperator &op, const std::string &key)
		{
			const auto it = std::find_if(op.schema.begin(), op.schema.end(),
				[&key](const SceneOperatorParameter &parameter) { return parameter.key == key; });
			return it == op.schema.end() ? nullptr : &*it;
		}

		template <typename T>
		const T *FindValue(const SceneOperatorValues &values, const std::string &key)
		{
			const auto it = values.find(key);
			return it == values.end() ? nullptr : std::get_if<T>(&it->second);
		}
	}

	TEST(SceneOperatorRegistryTests, RegisterThenFindReturnsTheOperator)
	{
		SceneOperatorRegistry registry;
		ASSERT_TRUE(registry.Register(MakeStubOperator("test.alpha", "Alpha")));
		const auto *found = registry.Find("test.alpha");
		ASSERT_NE(found, nullptr);
		EXPECT_EQ(found->id, "test.alpha");
		EXPECT_EQ(found->label, "Alpha");
		EXPECT_EQ(registry.Find("test.missing"), nullptr);
	}

	TEST(SceneOperatorRegistryTests, RegisteringADuplicateIdIsRejected)
	{
		SceneOperatorRegistry registry;
		ASSERT_TRUE(registry.Register(MakeStubOperator("test.alpha", "First")));
		const auto second = registry.Register(MakeStubOperator("test.alpha", "Second"));
		ASSERT_FALSE(second);
		EXPECT_FALSE(second.Error().code.empty());
		const auto *found = registry.Find("test.alpha");
		ASSERT_NE(found, nullptr);
		EXPECT_EQ(found->label, "First");
	}

	TEST(SceneOperatorRegistryTests, ListIdsIsSortedAndComplete)
	{
		SceneOperatorRegistry registry;
		ASSERT_TRUE(registry.Register(MakeStubOperator("test.beta", "Beta")));
		ASSERT_TRUE(registry.Register(MakeStubOperator("test.alpha", "Alpha")));
		const std::vector<std::string> expected{"test.alpha", "test.beta"};
		EXPECT_EQ(registry.ListIds(), expected);
	}

	TEST(SceneOperatorRegistryTests, CurvedArrowOperatorExposesRadiusSweepAndRotation)
	{
		SceneOperatorRegistry registry;
		ASSERT_TRUE(RegisterCurvedArrowOperator(registry));
		const auto *op = registry.Find("scene.curved_arrow");
		ASSERT_NE(op, nullptr);
		EXPECT_FALSE(op->label.empty());
		ASSERT_NE(op->execute, nullptr);

		for (const char *key : {"axisMode", "radiusRule", "radiusFactor", "sweepDegrees",
				 "rotationDegrees", "decoration", "color", "strokeWidth"})
		{
			const auto *parameter = FindParameter(*op, key);
			ASSERT_NE(parameter, nullptr) << "missing schema entry: " << key;
			EXPECT_FALSE(parameter->label.empty()) << key;
		}

		const auto *axisMode = FindParameter(*op, "axisMode");
		ASSERT_NE(axisMode, nullptr);
		EXPECT_EQ(axisMode->kind, SceneOperatorParameter::Kind::Enum);
		EXPECT_EQ(axisMode->enumLabels.size(), 4u);

		const auto *radiusRule = FindParameter(*op, "radiusRule");
		ASSERT_NE(radiusRule, nullptr);
		EXPECT_EQ(radiusRule->kind, SceneOperatorParameter::Kind::Enum);
		EXPECT_EQ(radiusRule->enumLabels.size(), 2u);

		const auto *sweep = FindParameter(*op, "sweepDegrees");
		ASSERT_NE(sweep, nullptr);
		EXPECT_EQ(sweep->kind, SceneOperatorParameter::Kind::Float);
		// The panel's slider must not be able to ask for a shape the operator then clamps away.
		EXPECT_GE(sweep->minimum, 1.0f);
		EXPECT_LE(sweep->maximum, 350.0f);
		EXPECT_LT(sweep->minimum, sweep->maximum);

		const auto *color = FindParameter(*op, "color");
		ASSERT_NE(color, nullptr);
		EXPECT_EQ(color->kind, SceneOperatorParameter::Kind::Color);
	}

	TEST(SceneOperatorRegistryTests, CurvedArrowDefaultsMatchTheParameterStruct)
	{
		SceneOperatorRegistry registry;
		ASSERT_TRUE(RegisterCurvedArrowOperator(registry));
		const auto *op = registry.Find("scene.curved_arrow");
		ASSERT_NE(op, nullptr);

		const CurvedArrowParameters reference;
		const auto *axisMode = FindValue<int>(op->defaults, "axisMode");
		ASSERT_NE(axisMode, nullptr);
		EXPECT_EQ(*axisMode, static_cast<int>(reference.axisMode));
		const auto *radiusRule = FindValue<int>(op->defaults, "radiusRule");
		ASSERT_NE(radiusRule, nullptr);
		EXPECT_EQ(*radiusRule, static_cast<int>(reference.radiusRule));
		const auto *decoration = FindValue<int>(op->defaults, "decoration");
		ASSERT_NE(decoration, nullptr);
		EXPECT_EQ(*decoration, static_cast<int>(reference.decoration));

		const auto *radiusFactor = FindValue<float>(op->defaults, "radiusFactor");
		ASSERT_NE(radiusFactor, nullptr);
		EXPECT_FLOAT_EQ(*radiusFactor, reference.radiusFactor);
		const auto *sweepDegrees = FindValue<float>(op->defaults, "sweepDegrees");
		ASSERT_NE(sweepDegrees, nullptr);
		EXPECT_FLOAT_EQ(*sweepDegrees, reference.sweepDegrees);
		const auto *rotationDegrees = FindValue<float>(op->defaults, "rotationDegrees");
		ASSERT_NE(rotationDegrees, nullptr);
		EXPECT_FLOAT_EQ(*rotationDegrees, reference.rotationDegrees);
		const auto *strokeWidth = FindValue<float>(op->defaults, "strokeWidth");
		ASSERT_NE(strokeWidth, nullptr);
		EXPECT_FLOAT_EQ(*strokeWidth, reference.strokeWidth);

		const auto *color = FindValue<glm::vec3>(op->defaults, "color");
		ASSERT_NE(color, nullptr);
		EXPECT_FLOAT_EQ(color->r, reference.color.r);
		EXPECT_FLOAT_EQ(color->g, reference.color.g);
		EXPECT_FLOAT_EQ(color->b, reference.color.b);

		// Every schema key has a default, and nothing is defaulted that the panel cannot render.
		EXPECT_EQ(op->defaults.size(), op->schema.size());
		for (const auto &parameter : op->schema)
			EXPECT_NE(op->defaults.find(parameter.key), op->defaults.end()) << parameter.key;
	}

	TEST(SceneOperatorRegistryTests, CurvedArrowExecuteBuildsTheRingForTwoSelectedAtoms)
	{
		SceneOperatorRegistry registry;
		ASSERT_TRUE(RegisterCurvedArrowOperator(registry));
		const auto *op = registry.Find("scene.curved_arrow");
		ASSERT_NE(op, nullptr);

		RendererWindowState window;
		window.structure.atoms = {{"C", {-1, 0, 0}}, {"C", {1, 0, 0}}};
		window.selectedAtomIndices = {0, 1};

		const auto added = op->execute(window, op->defaults);
		ASSERT_TRUE(added);
		ASSERT_EQ(added->size(), 2u);
		const auto *path = window.paths->Store().Find(added->front());
		ASSERT_NE(path, nullptr);
		// The defaults route through CurvedArrowParameters, so the bond ring is what comes out.
		EXPECT_TRUE(std::holds_alternative<PathTransformBinding::BondFrame>(path->transformBinding.value));
	}

	TEST(SceneOperatorRegistryTests, CurvedArrowExecuteHonoursAChangedSweep)
	{
		SceneOperatorRegistry registry;
		ASSERT_TRUE(RegisterCurvedArrowOperator(registry));
		const auto *op = registry.Find("scene.curved_arrow");
		ASSERT_NE(op, nullptr);

		RendererWindowState window;
		window.structure.atoms = {{"C", {-1, 0, 0}}, {"C", {1, 0, 0}}};
		window.selectedAtomIndices = {0, 1};

		auto values = op->defaults;
		values["sweepDegrees"] = 90.0f;
		const auto added = op->execute(window, values);
		ASSERT_TRUE(added);
		ASSERT_EQ(added->size(), 2u);
		const auto *path = window.paths->Store().Find(added->front());
		ASSERT_NE(path, nullptr);
		ASSERT_TRUE(std::holds_alternative<CircularArcSegmentData>(path->segments.front().data));
		const auto sweep = std::get<CircularArcSegmentData>(path->segments.front().data).signedSweepRadians;
		// A value map the operator ignored would still read 150 degrees here.
		EXPECT_NEAR(std::abs(sweep), glm::radians(90.0f), 1.0e-4f);
	}

	TEST(SceneOperatorRegistryTests, CurvatureChangesCycleSweepAndBondModeShowsSweepInstead)
	{
		SceneOperatorRegistry registry;
		ASSERT_TRUE(RegisterCurvedArrowOperator(registry));
		ASSERT_NE(registry.Find("scene.curved_arrow"), nullptr);
		const auto &op = *registry.Find("scene.curved_arrow");
		ASSERT_NE(FindParameter(op, "curvature"), nullptr);
		const auto &parameter = *FindParameter(op, "curvature");
		EXPECT_EQ(parameter.kind, SceneOperatorParameter::Kind::Float);
		EXPECT_EQ(parameter.label, "Wygięcie łuku");
		EXPECT_FLOAT_EQ(parameter.minimum, 0.05f);
		EXPECT_FLOAT_EQ(parameter.maximum, 1.5f);
		EXPECT_FLOAT_EQ(std::get<float>(op.defaults.at("curvature")), CurvedArrowParameters{}.curvature);
		ASSERT_TRUE(op.isParameterRelevant);
		RendererWindowState window;
		window.structure.atoms = {{"C", {2, 0, 0}}, {"C", {-1, std::sqrt(3.0f), 0}}, {"C", {-1, -std::sqrt(3.0f), 0}}};
		window.selectedAtomIndices = {0, 1, 2};
		EXPECT_TRUE(op.isParameterRelevant("curvature", op.defaults, window));
		EXPECT_FALSE(op.isParameterRelevant("sweepDegrees", op.defaults, window));
		auto values = op.defaults;
		values["curvature"] = 1.0f;
		const auto added = op.execute(window, values);
		ASSERT_TRUE(added);
		ASSERT_EQ(added->size(), 3u);
		for (const auto id : *added)
		{
			const auto &path = *window.paths->Store().Find(id);
			EXPECT_NEAR(std::get<CircularArcSegmentData>(path.segments.front().data).signedSweepRadians,
				2.0f * std::numbers::pi_v<float> / 3.0f, 1.0e-5f);
		}
		window.selectedAtomIndices = {0, 1};
		EXPECT_TRUE(op.isParameterRelevant("sweepDegrees", values, window));
		EXPECT_FALSE(op.isParameterRelevant("curvature", values, window));
	}

	TEST(SceneOperatorRegistryTests, SelectionModeIgnoresStaleAtomsAndKeepsVacancyPairsNonBond)
	{
		SceneOperatorRegistry registry;
		ASSERT_TRUE(RegisterCurvedArrowOperator(registry));
		const auto &op = *registry.Find("scene.curved_arrow");
		RendererWindowState window;
		window.structure.atoms = {{"C", {-1, 0, 0}}, {"C", {1, 0, 0}}};
		window.selectedAtomIndices = {0, 1, 99};
		EXPECT_EQ(ResolveCurvedArrowSelectionMode(window, CurvedArrowAxisMode::Auto), CurvedArrowSelectionMode::Bond);
		EXPECT_TRUE(op.isParameterRelevant("axisMode", op.defaults, window));
		EXPECT_TRUE(op.isParameterRelevant("sweepDegrees", op.defaults, window));
		EXPECT_FALSE(op.isParameterRelevant("curvature", op.defaults, window));
		const auto added = op.execute(window, op.defaults);
		ASSERT_TRUE(added);
		EXPECT_TRUE(std::holds_alternative<PathTransformBinding::BondFrame>(
			window.paths->Store().Find(added->front())->transformBinding.value));
		window.structure.vacancies.push_back({{0, 1, 0}});
		window.selectedAtomIndices = {0};
		window.selectedVacancies = {0};
		EXPECT_EQ(ResolveCurvedArrowSelectionMode(window, CurvedArrowAxisMode::Auto), CurvedArrowSelectionMode::TwoEnds);
		EXPECT_TRUE(op.isParameterRelevant("curvature", op.defaults, window));
		EXPECT_FALSE(op.isParameterRelevant("sweepDegrees", op.defaults, window));
	}

	TEST(SceneOperatorRegistryTests, CurvedArrowExecuteFailsWithoutASelection)
	{
		SceneOperatorRegistry registry;
		ASSERT_TRUE(RegisterCurvedArrowOperator(registry));
		const auto *op = registry.Find("scene.curved_arrow");
		ASSERT_NE(op, nullptr);

		RendererWindowState window;
		const auto added = op->execute(window, op->defaults);
		// The panel re-runs on every slider move; a lost selection must be a clean rejection.
		EXPECT_FALSE(added);
	}
	TEST(SceneOperatorRegistryTests, NewArrowParametersHaveRequestedRangesAndModeRelevance)
	{
		SceneOperatorRegistry registry;
		ASSERT_TRUE(RegisterCurvedArrowOperator(registry));
		const auto &op = *registry.Find("scene.curved_arrow");
		for (const auto &[key, kind, minimum, maximum] :
			std::vector<std::tuple<std::string, SceneOperatorParameter::Kind, float, float>>{
				{"radiusScale", SceneOperatorParameter::Kind::Float, 0.8f, 2.5f},
				{"endGap", SceneOperatorParameter::Kind::Float, 0.0f, 3.0f},
				{"arrowCount", SceneOperatorParameter::Kind::Int, 1.0f, 6.0f}})
		{
			ASSERT_NE(FindParameter(op, key), nullptr);
			const auto &parameter = *FindParameter(op, key);
			EXPECT_EQ(parameter.kind, kind);
			EXPECT_FLOAT_EQ(parameter.minimum, minimum);
			EXPECT_FLOAT_EQ(parameter.maximum, maximum);
		}
		EXPECT_EQ(std::get<int>(op.defaults.at("arrowCount")), 2);
		EXPECT_FLOAT_EQ(std::get<float>(op.defaults.at("sweepDegrees")), 150.0f);
		EXPECT_FLOAT_EQ(std::get<float>(op.defaults.at("radiusScale")), 1.0f);
		EXPECT_NEAR(std::get<float>(op.defaults.at("endGap")), 0.15f, 1.0e-5f);
		RendererWindowState window;
		window.structure.atoms = {{"C", {-1, 0, 0}}, {"C", {1, 0, 0}}, {"C", {0, 1, 0}}};
		for (const auto &selection : {std::vector<std::size_t>{0, 1}, std::vector<std::size_t>{0, 1, 2}})
		{
			window.selectedAtomIndices = selection;
			const bool bond = selection.size() == 2;
			EXPECT_EQ(op.isParameterRelevant("arrowCount", op.defaults, window), bond);
			EXPECT_EQ(op.isParameterRelevant("radiusScale", op.defaults, window), !bond);
			EXPECT_EQ(op.isParameterRelevant("endGap", op.defaults, window), !bond);
		}
	}
}

namespace DefectStudio::Tests
{
	TEST(SceneOperatorRegistryTests, TiltSchemaAndExactVisibleSetsAgreeWithEachMode)
	{
		SceneOperatorRegistry registry;
		ASSERT_TRUE(RegisterCurvedArrowOperator(registry));
		const auto &op = *registry.Find("scene.curved_arrow");
		ASSERT_NE(FindParameter(op, "tiltDegrees"), nullptr);
		const auto &tilt = *FindParameter(op, "tiltDegrees");
		EXPECT_EQ(tilt.label, "Nachylenie");
		EXPECT_EQ(tilt.kind, SceneOperatorParameter::Kind::Float);
		EXPECT_FLOAT_EQ(tilt.minimum, -180);
		EXPECT_FLOAT_EQ(tilt.maximum, 180);
		EXPECT_FLOAT_EQ(std::get<float>(op.defaults.at("tiltDegrees")), 0);
		RendererWindowState window;
		window.structure.atoms = {{"C", {2, 0, 0}}, {"C", {-1, std::sqrt(3.0f), 0}}, {"C", {-1, -std::sqrt(3.0f), 0}}};
		for (int mode = 0; mode < 3; ++mode)
		{
			window.selectedAtomIndices = mode == 0 ? std::vector<std::size_t>{0, 1, 2} : std::vector<std::size_t>{0, 1};
			auto values = op.defaults;
			values["axisMode"] = static_cast<int>(mode == 2 ? CurvedArrowAxisMode::DefectZ : CurvedArrowAxisMode::Auto);
			std::vector<std::string> visible;
			for (const auto &parameter : op.schema)
				if (op.isParameterRelevant(parameter.key, values, window)) visible.push_back(parameter.key);
			const std::vector<std::vector<std::string>> expected{
				{"radiusScale", "endGap", "curvature", "tiltDegrees", "decoration", "color", "strokeWidth"},
				{"axisMode", "radiusRule", "radiusFactor", "arrowCount", "sweepDegrees", "rotationDegrees", "decoration", "color", "strokeWidth"},
				{"axisMode", "arrowCount", "radiusScale", "endGap", "curvature", "tiltDegrees", "decoration", "color", "strokeWidth"}};
			EXPECT_EQ(visible, expected[mode]);
			ASSERT_TRUE(op.parameterMaximum);
			EXPECT_FLOAT_EQ(op.parameterMaximum(*FindParameter(op, "arrowCount"), values, window), mode == 2 ? 2.0f : 6.0f);
		}
		window.structure.vacancies.push_back({{0, 1, 0}});
		window.selectedAtomIndices = {0};
		window.selectedVacancies = {0};
		EXPECT_TRUE(op.isParameterRelevant("arrowCount", op.defaults, window));
		EXPECT_TRUE(op.isParameterRelevant("tiltDegrees", op.defaults, window));
		EXPECT_FLOAT_EQ(op.parameterMaximum(*FindParameter(op, "arrowCount"), op.defaults, window), 2);
	}
}
