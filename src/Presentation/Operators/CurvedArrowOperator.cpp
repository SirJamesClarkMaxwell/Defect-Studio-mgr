#include "Core/dspch.hpp"

#include "Presentation/Operators/SceneOperatorRegistry.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

#include "Presentation/Panels/ScenePathCurvedArrow.hpp"
#include "Presentation/Panels/ScenePathOperations.hpp"
#include "Renderer/Path/CurvedArrowParameters.hpp"

namespace DefectStudio
{
	namespace
	{
		template <typename T>
		T ReadValue(const SceneOperatorValues &values, const char *key, T fallback)
		{
			const auto it = values.find(key);
			if (it == values.end())
				return fallback;
			if (const auto *value = std::get_if<T>(&it->second))
				return *value;
			return fallback;
		}

		template <typename Enum>
		Enum ReadEnum(const SceneOperatorValues &values, const char *key, Enum fallback, int count)
		{
			const auto it = values.find(key);
			if (it == values.end())
				return fallback;
			const auto *value = std::get_if<int>(&it->second);
			if (value == nullptr || *value < 0 || *value >= count)
				return fallback;
			return static_cast<Enum>(*value);
		}

		float ReadFiniteFloat(const SceneOperatorValues &values, const char *key, float fallback)
		{
			const float value = ReadValue(values, key, fallback);
			return std::isfinite(value) ? value : fallback;
		}

		glm::vec3 ReadFiniteColor(const SceneOperatorValues &values, const char *key, glm::vec3 fallback)
		{
			const glm::vec3 value = ReadValue(values, key, fallback);
			return std::isfinite(value.r) && std::isfinite(value.g) && std::isfinite(value.b) ? value : fallback;
		}

		CurvedArrowParameters ToCurvedArrowParameters(const SceneOperatorValues &values)
		{
			CurvedArrowParameters parameters;
			parameters.axisMode = ReadEnum(values, "axisMode", parameters.axisMode, 4);
			parameters.radiusRule = ReadEnum(values, "radiusRule", parameters.radiusRule, 2);
			parameters.radiusFactor = ReadFiniteFloat(values, "radiusFactor", parameters.radiusFactor);
			parameters.arrowCount = ReadValue(values, "arrowCount", parameters.arrowCount);
			parameters.sweepDegrees = ReadFiniteFloat(values, "sweepDegrees", parameters.sweepDegrees);
			parameters.rotationDegrees = ReadFiniteFloat(values, "rotationDegrees", parameters.rotationDegrees);
			parameters.radiusScale = ReadFiniteFloat(values, "radiusScale", parameters.radiusScale);
			parameters.endGap = ReadFiniteFloat(values, "endGap", std::max(0.0f, GetScenePathAtomBuffer() - 1.0f));
			parameters.curvature = ReadFiniteFloat(values, "curvature", parameters.curvature);
			parameters.tiltDegrees = ReadFiniteFloat(values, "tiltDegrees", parameters.tiltDegrees);
			parameters.decoration = ReadEnum(values, "decoration", parameters.decoration, 9);
			parameters.color = ReadFiniteColor(values, "color", parameters.color);
			parameters.strokeWidth = ReadFiniteFloat(values, "strokeWidth", parameters.strokeWidth);
			return parameters;
		}

		SceneOperatorParameter FloatParameter(
			std::string key, std::string label, float minimum, float maximum)
		{
			return {std::move(key), std::move(label), SceneOperatorParameter::Kind::Float, minimum, maximum, {}};
		}

		SceneOperatorParameter EnumParameter(
			std::string key, std::string label, std::vector<std::string> labels)
		{
			SceneOperatorParameter parameter;
			parameter.key = std::move(key);
			parameter.label = std::move(label);
			parameter.kind = SceneOperatorParameter::Kind::Enum;
			parameter.enumLabels = std::move(labels);
			return parameter;
		}
	}

	Result<void> RegisterCurvedArrowOperator(SceneOperatorRegistry &registry)
	{
		const CurvedArrowParameters defaults;
		SceneOperator op;
		op.id = "scene.curved_arrow";
		op.label = "Zakrzywiona strzałka C_n";
		op.schema = {
			EnumParameter("axisMode", "Oś", {"Automatyczna", "Wiązanie", "Oś Z defektu", "Prostopadła do wiązania"}),
			EnumParameter("radiusRule", "Reguła promienia", {"Względem atomu", "Ułamek wiązania"}),
			FloatParameter("radiusFactor", "Współczynnik promienia", 0.1f, 5.0f),
			{.key = "arrowCount", .label = "Liczba strzałek", .kind = SceneOperatorParameter::Kind::Int,
				.minimum = 1.0f, .maximum = 6.0f},
			FloatParameter("sweepDegrees", "Rozpiętość", 1.0f, 350.0f),
			FloatParameter("rotationDegrees", "Obrót", -360.0f, 360.0f),
			FloatParameter("radiusScale", "Promień okręgu", 0.8f, 2.5f),
			FloatParameter("endGap", "Odstęp od atomów", 0.0f, 3.0f),
			FloatParameter("curvature", "Wygięcie łuku", 0.05f, 1.5f),
			FloatParameter("tiltDegrees", "Nachylenie", -180.0f, 180.0f),
			EnumParameter("decoration", "Dekoracja", {
				"Brak", "Strzałka", "Wklęsła", "Latex", "Belka", "Okrąg", "Kwadrat", "Romb", "Klin"}),
			{.key = "color", .label = "Kolor", .kind = SceneOperatorParameter::Kind::Color},
			FloatParameter("strokeWidth", "Grubość kreski", 0.001f, 1.0f)};
		op.defaults = {
			{"axisMode", static_cast<int>(defaults.axisMode)},
			{"radiusRule", static_cast<int>(defaults.radiusRule)},
			{"radiusFactor", defaults.radiusFactor},
			{"arrowCount", defaults.arrowCount},
			{"sweepDegrees", defaults.sweepDegrees},
			{"rotationDegrees", defaults.rotationDegrees},
			{"radiusScale", defaults.radiusScale},
			{"endGap", std::max(0.0f, GetScenePathAtomBuffer() - 1.0f)},
			{"curvature", defaults.curvature},
			{"tiltDegrees", defaults.tiltDegrees},
			{"decoration", static_cast<int>(defaults.decoration)},
			{"color", defaults.color},
			{"strokeWidth", defaults.strokeWidth}};
		op.isParameterRelevant = [](const std::string &key, const SceneOperatorValues &values,
			const RendererWindowState &window) {
			const auto mode = ResolveCurvedArrowSelectionMode(window,
				ReadEnum(values, "axisMode", CurvedArrowAxisMode::Auto, 4));
			if (key == "decoration" || key == "color" || key == "strokeWidth")
				return true;
			if (key == "axisMode")
				return mode != CurvedArrowSelectionMode::Cycle;
			if (key == "arrowCount")
				return mode != CurvedArrowSelectionMode::Cycle;
			if (key == "curvature" || key == "radiusScale" || key == "endGap" || key == "tiltDegrees")
				return mode != CurvedArrowSelectionMode::Bond;
			return mode == CurvedArrowSelectionMode::Bond;
		};
		op.parameterMaximum = [](const SceneOperatorParameter &parameter, const SceneOperatorValues &values,
			const RendererWindowState &window) {
			return parameter.key == "arrowCount" && ResolveCurvedArrowSelectionMode(window,
				ReadEnum(values, "axisMode", CurvedArrowAxisMode::Auto, 4)) == CurvedArrowSelectionMode::TwoEnds
				? 2.0f : parameter.maximum;
		};
		op.execute = [](RendererWindowState &window, const SceneOperatorValues &values) {
			// The runner owns the undo entry: an operator re-runs many times behind a single one.
			return AddCurvedArrowThroughSelectedAtoms(
				window, ToCurvedArrowParameters(values), SceneOperationUndo::Suppress);
		};

		return registry.Register(std::move(op));
	}
}
