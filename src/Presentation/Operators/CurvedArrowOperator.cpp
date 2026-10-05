#include "Core/dspch.hpp"

#include "Presentation/Operators/SceneOperatorRegistry.hpp"

#include <cmath>
#include <utility>

#include "Presentation/Panels/ScenePathCurvedArrow.hpp"
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
			parameters.axisMode = ReadEnum(values, "axisMode", parameters.axisMode, 3);
			parameters.radiusRule = ReadEnum(values, "radiusRule", parameters.radiusRule, 2);
			parameters.radiusFactor = ReadFiniteFloat(values, "radiusFactor", parameters.radiusFactor);
			parameters.sweepDegrees = ReadFiniteFloat(values, "sweepDegrees", parameters.sweepDegrees);
			parameters.rotationDegrees = ReadFiniteFloat(values, "rotationDegrees", parameters.rotationDegrees);
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
			EnumParameter("axisMode", "Oś", {"Automatyczna", "Wiązanie", "Oś Z defektu"}),
			EnumParameter("radiusRule", "Reguła promienia", {"Względem atomu", "Ułamek wiązania"}),
			FloatParameter("radiusFactor", "Współczynnik promienia", 0.1f, 5.0f),
			FloatParameter("sweepDegrees", "Rozpiętość", 1.0f, 350.0f),
			FloatParameter("rotationDegrees", "Obrót", -360.0f, 360.0f),
			EnumParameter("decoration", "Dekoracja", {
				"Brak", "Strzałka", "Wklęsła", "Latex", "Belka", "Okrąg", "Kwadrat", "Romb", "Klin"}),
			{.key = "color", .label = "Kolor", .kind = SceneOperatorParameter::Kind::Color},
			FloatParameter("strokeWidth", "Grubość kreski", 0.001f, 1.0f)};
		op.defaults = {
			{"axisMode", static_cast<int>(defaults.axisMode)},
			{"radiusRule", static_cast<int>(defaults.radiusRule)},
			{"radiusFactor", defaults.radiusFactor},
			{"sweepDegrees", defaults.sweepDegrees},
			{"rotationDegrees", defaults.rotationDegrees},
			{"decoration", static_cast<int>(defaults.decoration)},
			{"color", defaults.color},
			{"strokeWidth", defaults.strokeWidth}};
		op.execute = [](RendererWindowState &window, const SceneOperatorValues &values) {
			// The runner owns the undo entry: an operator re-runs many times behind a single one.
			return AddCurvedArrowThroughSelectedAtoms(
				window, ToCurvedArrowParameters(values), SceneOperationUndo::Suppress);
		};

		return registry.Register(std::move(op));
	}
}
