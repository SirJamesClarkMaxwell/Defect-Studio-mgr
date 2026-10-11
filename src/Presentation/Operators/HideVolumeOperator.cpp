#include "Core/dspch.hpp"

#include "Presentation/Operators/SceneOperatorRegistry.hpp"
#include "Presentation/Panels/SceneHideVolumeEditor.hpp"

#include <cmath>
#include <utility>

namespace DefectStudio
{
	namespace
	{
		template <typename T>
		T ReadValue(const SceneOperatorValues &values, const std::string &key, T fallback)
		{
			const auto found = values.find(key);
			return found != values.end() && std::holds_alternative<T>(found->second)
				? std::get<T>(found->second) : fallback;
		}

		template <typename T>
		std::optional<T> ReadTarget(const SceneOperatorValues &values, const std::string &key)
		{
			const auto found = values.find(key);
			if (found == values.end() || !std::holds_alternative<T>(found->second)) return std::nullopt;
			return std::get<T>(found->second);
		}

		StructuredError InvalidVolume()
		{
			return {ErrorCategory::Validation, Severity::Error, "Nieprawidłowe parametry bryły ukrywającej.",
				"Hide volume requires a valid kind/frame, finite centre and positive dimensions.",
				"Sprawdź wymiary bryły i komórkę struktury.", "Presentation/HideVolumeOperator",
				"hide_volume.invalid", DisplayPolicy::Silent};
		}
	}

	SceneOperatorValues MakeHideVolumeOperatorValues(const RendererWindowState &window, HideVolumeKind kind,
		std::optional<std::size_t> targetAtom, std::optional<glm::vec3> targetPosition)
	{
		const auto volume = MakeSceneHideVolume(window, kind, targetAtom, targetPosition);
		SceneOperatorValues values{
			{"kind", static_cast<int>(volume.kind)}, {"invert", volume.invert},
			{"frame", static_cast<int>(volume.frame)}, {"radius", volume.halfExtents.x},
			{"halfHeight", volume.halfExtents.z}, {"halfX", volume.halfExtents.x},
			{"halfY", volume.halfExtents.y}, {"halfZ", volume.halfExtents.z}};
		if (targetAtom) values["targetAtom"] = *targetAtom;
		if (targetPosition) values["position"] = *targetPosition;
		return values;
	}

	Result<void> RegisterHideVolumeOperator(SceneOperatorRegistry &registry)
	{
		SceneOperator op;
		op.id = "scene.hide_volume";
		op.label = "Bryła ukrywająca";
		op.schema = {
			{.key = "kind", .label = "Kształt", .kind = SceneOperatorParameter::Kind::Enum,
				.enumLabels = {"Kula", "Prostopadłościan", "Walec"}},
			{.key = "invert", .label = "Ukryj na zewnątrz", .kind = SceneOperatorParameter::Kind::Bool},
			{.key = "radius", .label = "Promień", .minimum = 0.01f, .maximum = 100.0f},
			{.key = "halfHeight", .label = "Połowa wysokości", .minimum = 0.01f, .maximum = 100.0f},
			{.key = "halfX", .label = "Połowa boku X", .minimum = 0.01f, .maximum = 100.0f},
			{.key = "halfY", .label = "Połowa boku Y", .minimum = 0.01f, .maximum = 100.0f},
			{.key = "halfZ", .label = "Połowa boku Z", .minimum = 0.01f, .maximum = 100.0f},
			{.key = "frame", .label = "Układ", .kind = SceneOperatorParameter::Kind::Enum,
				.enumLabels = {"Zakotwiczona", "Ułamkowa"}}};
		op.defaults = MakeHideVolumeOperatorValues(RendererWindowState{}, HideVolumeKind::Sphere);
		op.isParameterRelevant = [](const std::string &key, const SceneOperatorValues &values, const RendererWindowState &) {
			const int kind = ReadValue(values, "kind", 0);
			if (key == "radius") return kind != static_cast<int>(HideVolumeKind::Box);
			if (key == "halfHeight") return kind == static_cast<int>(HideVolumeKind::Cylinder);
			if (key == "halfX" || key == "halfY" || key == "halfZ") return kind == static_cast<int>(HideVolumeKind::Box);
			return true;
		};
		op.execute = [](RendererWindowState &window, const SceneOperatorValues &values) -> Result<std::vector<SceneObjectId>> {
			const int kind = ReadValue(values, "kind", 0);
			const int frame = ReadValue(values, "frame", 0);
			if (kind < 0 || kind > 2 || frame < 0 || frame > 1) return InvalidVolume();
			auto volume = MakeSceneHideVolume(window, static_cast<HideVolumeKind>(kind),
				ReadTarget<std::size_t>(values, "targetAtom"), ReadTarget<glm::vec3>(values, "position"));
			volume.invert = ReadValue(values, "invert", volume.invert);
			if (volume.kind == HideVolumeKind::Box)
				volume.halfExtents = glm::vec3(ReadValue(values, "halfX", volume.halfExtents.x),
					ReadValue(values, "halfY", volume.halfExtents.y), ReadValue(values, "halfZ", volume.halfExtents.z));
			else
			{
				volume.halfExtents.x = ReadValue(values, "radius", volume.halfExtents.x);
				if (volume.kind == HideVolumeKind::Cylinder)
					volume.halfExtents.z = ReadValue(values, "halfHeight", volume.halfExtents.z);
			}
			volume.frame = static_cast<HideVolumeFrame>(frame);
			if (volume.frame == HideVolumeFrame::Fractional)
			{
				const float determinant = glm::determinant(window.structure.lattice);
				if (!std::isfinite(determinant) || std::abs(determinant) <= 1e-6f) return InvalidVolume();
				volume.center = glm::inverse(window.structure.lattice) * volume.center;
				volume.anchorAtoms.clear();
			}
			for (int axis = 0; axis < 3; ++axis)
				if (!std::isfinite(volume.center[axis]) || !std::isfinite(volume.halfExtents[axis]) || volume.halfExtents[axis] <= 0)
					return InvalidVolume();
			const auto id = AddSceneHideVolume(window, std::move(volume), SceneOperationUndo::Suppress);
			return std::vector<SceneObjectId>{id};
		};
		return registry.Register(std::move(op));
	}
}
