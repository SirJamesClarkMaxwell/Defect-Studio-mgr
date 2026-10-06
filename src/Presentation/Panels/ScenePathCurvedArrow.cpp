#include "Core/dspch.hpp"

#include "Presentation/Panels/ScenePathCurvedArrow.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>

#include "Presentation/Panels/ScenePathDevMenu.hpp"
#include "Presentation/Panels/ScenePathOperations.hpp"
#include "Renderer/Path/PathTopology.hpp"
#include "Renderer/RendererLayer.hpp"
#include "Renderer/Scene/ScenePlaneGeometry.hpp"

namespace DefectStudio
{
	namespace
	{
		struct End
		{
			glm::vec3 position;
			PathBinding binding;
			float angle = 0.0f;
		};

		StructuredError ArrowError(const char *code, const char *message)
		{
			return {ErrorCategory::Validation, Severity::Error, message, message,
				"Select compatible atoms and try again.", "Presentation/ScenePathCurvedArrow",
				code, DisplayPolicy::Silent};
		}

		bool IsFinite(const glm::vec3 &position)
		{
			return std::isfinite(position.x) && std::isfinite(position.y) && std::isfinite(position.z);
		}

		// Preserve the two-end fallback: nearest neighbours, with a selected vacancy taking priority.
		glm::vec3 TwoEndAxis(const RendererWindowState &window, const std::vector<End> &ends, glm::vec3 &origin)
		{
			const auto a = ends[0].position, b = ends[1].position, midpoint = (a + b) * 0.5f;
			std::vector<glm::vec3> neighbours;
			for (const auto &atom : window.structure.atoms)
				if (IsFinite(atom.cartesianPosition) && glm::distance(atom.cartesianPosition, a) > 1.0e-5f &&
					glm::distance(atom.cartesianPosition, b) > 1.0e-5f)
					neighbours.push_back(atom.cartesianPosition);
			std::stable_sort(neighbours.begin(), neighbours.end(), [midpoint](const auto &x, const auto &y) {
				return glm::dot(x - midpoint, x - midpoint) < glm::dot(y - midpoint, y - midpoint);
			});
			const auto count = std::min(neighbours.size(), std::size_t{3});
			glm::vec3 centroid = count == 0 ? midpoint : glm::vec3(0.0f);
			for (std::size_t i = 0; i < count; ++i)
				centroid += neighbours[i] / static_cast<float>(count);
			glm::vec3 axis = glm::cross(b - a, centroid - midpoint);
			if (window.selectedAtomIndices.size() == 2 && window.selectedVacancies.size() == 1 &&
				window.selectedVacancies.front() < window.structure.vacancies.size())
			{
				origin = window.structure.vacancies[window.selectedVacancies.front()].cartesianPosition;
				axis = glm::cross(a - origin, b - origin);
			}
			if (glm::length(axis) < 1.0e-5f)
				axis = glm::cross(b - a, std::abs((b - a).z) < 0.9f * glm::length(b - a) ?
					glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0));
			return axis;
		}

		glm::quat BondFrameRotation(const glm::vec3 &axis)
		{
			return glm::rotation(glm::vec3(0.0f, 0.0f, 1.0f), axis);
		}
	}

	CurvedArrowSelectionMode ResolveCurvedArrowSelectionMode(
		const RendererWindowState &window, CurvedArrowAxisMode axisMode)
	{
		const auto atomCount = std::count_if(window.selectedAtomIndices.begin(), window.selectedAtomIndices.end(),
			[&window](std::size_t index) { return index < window.structure.atoms.size(); });
		if (atomCount >= 3)
			return CurvedArrowSelectionMode::Cycle;
		if (axisMode == CurvedArrowAxisMode::Bond || (axisMode == CurvedArrowAxisMode::Auto && atomCount == 2))
			return CurvedArrowSelectionMode::Bond;
		return CurvedArrowSelectionMode::TwoEnds;
	}

	Result<std::vector<SceneObjectId>> AddCurvedArrowThroughSelectedAtoms(
		RendererWindowState &window, const CurvedArrowParameters &parameters, SceneOperationUndo undo)
	{
		std::vector<End> ends;
		const float defaultGap = std::max(0.0f, GetScenePathAtomBuffer() - 1.0f);
		const float gap = parameters.endGap && std::isfinite(*parameters.endGap) ?
			std::clamp(*parameters.endGap, 0.0f, 3.0f) : defaultGap;
		const float buffer = parameters.endGap ? 1.0f + gap : GetScenePathAtomBuffer();
		const float radiusScale = std::isfinite(parameters.radiusScale) ?
			std::clamp(parameters.radiusScale, 0.8f, 2.5f) : 1.0f;
		for (const auto index : window.selectedAtomIndices)
			if (index < window.structure.atoms.size())
				ends.push_back({window.structure.atoms[index].cartesianPosition,
					PathBinding{PathBinding::CopyPosition{index, {}, buffer}}});
		const auto mode = ResolveCurvedArrowSelectionMode(window, parameters.axisMode);
		const bool cycle = mode == CurvedArrowSelectionMode::Cycle;
		// Keep atom/vacancy and vacancy/vacancy pairs. For >=2 atoms vacancies only define the axis.
		if (ends.size() < 2)
			for (const auto index : window.selectedVacancies)
				if (index < window.structure.vacancies.size())
					ends.push_back({window.structure.vacancies[index].cartesianPosition,
						PathBinding{PathBinding::CopyVacancy{index, {}, buffer}}});
		if (ends.size() < 2 || (!cycle && ends.size() != 2))
			return ArrowError("path.two_atoms_required", "Select at least two atoms, or exactly two atom/vacancy ends.");
		for (const auto &end : ends)
			if (!IsFinite(end.position))
				return ArrowError("path.arc_nonfinite", "The arrow ends must have finite positions.");

		const bool bondMode = mode == CurvedArrowSelectionMode::Bond;
		const float curvature = std::isfinite(parameters.curvature) ?
			std::clamp(parameters.curvature, 0.05f, 1.5f) : CurvedArrowParameters{}.curvature;
		glm::vec3 origin(0.0f), axis(0.0f);
		for (const auto &end : ends)
			origin += end.position / static_cast<float>(ends.size());
		if (bondMode)
		{
			const auto *first = std::get_if<PathBinding::CopyPosition>(&ends[0].binding.value);
			const auto *second = std::get_if<PathBinding::CopyPosition>(&ends[1].binding.value);
			if (first == nullptr || second == nullptr)
				return ArrowError("curved_arrow.bond_atoms_required", "A bond-axis arrow needs two selected atoms.");
			origin = (ends[0].position + ends[1].position) * 0.5f;
			axis = ends[1].position - ends[0].position;
			if (glm::length(axis) < 1.0e-5f)
				return ArrowError("curved_arrow.degenerate_bond", "The selected atoms must not occupy the same position.");
		}
		else if (const auto &frame = window.structure.defectFrame)
		{
			origin = frame->origin;
			axis = frame->z;
		}
		else if (cycle)
		{
			std::vector<glm::vec3> points;
			for (const auto &end : ends) points.push_back(end.position);
			const auto fit = FitScenePlane(points, glm::vec3(0, 0, -1));
			if (!fit)
				return ArrowError("path.arc_axis_zero", "The selected atoms must define a rotation plane.");
			origin = fit->center;
			axis = fit->normal;
			// A best-fit plane has no sign. Pick its dominant component positive, independent of selection order.
			int dominant = 0;
			for (int i = 1; i < 3; ++i)
				if (std::abs(axis[i]) > std::abs(axis[dominant])) dominant = i;
			if (axis[dominant] < 0) axis = -axis;
			if (window.selectedVacancies.size() == 1 && window.selectedVacancies.front() < window.structure.vacancies.size())
				origin = window.structure.vacancies[window.selectedVacancies.front()].cartesianPosition;
		}
		else
			axis = TwoEndAxis(window, ends, origin);
		const float axisLength = glm::length(axis);
		if (!IsFinite(origin) || !IsFinite(axis) || !std::isfinite(axisLength) || axisLength < 1.0e-5f)
			return ArrowError("path.arc_axis_zero", "A curved arrow needs a valid rotation axis.");
		axis /= axisLength;
		const auto radial = [origin, axis](const glm::vec3 &position) {
			const auto delta = position - origin;
			return delta - axis * glm::dot(delta, axis);
		};
		std::vector<ScenePath> paths;
		if (bondMode)
		{
			const auto *first = std::get_if<PathBinding::CopyPosition>(&ends[0].binding.value);
			const auto *second = std::get_if<PathBinding::CopyPosition>(&ends[1].binding.value);
			const float length = axisLength;
			const std::size_t atomA = first->atomIndex, atomB = second->atomIndex;
			const float atomRadius = std::max(window.structure.atoms[atomA].radius,
				window.structure.atoms[atomB].radius);
			float radius = parameters.radiusRule == CurvedArrowRadiusRule::BondFraction ?
				length * parameters.radiusFactor : atomRadius * parameters.radiusFactor;
			if (!std::isfinite(radius) || radius < 1.0e-4f)
				radius = 0.35f * length;
			if (!std::isfinite(radius) || radius <= 1.0e-5f)
				return ArrowError("curved_arrow.degenerate_bond", "The selected atoms must define a visible bond.");
			const int arrowCount = std::clamp(parameters.arrowCount, 1, 6);
			const float spacingDegrees = 360.0f / static_cast<float>(arrowCount);
			const float requestedSweep = std::isfinite(parameters.sweepDegrees) ?
				parameters.sweepDegrees : CurvedArrowParameters{}.sweepDegrees;
			const float sweepDegrees = std::clamp(requestedSweep, 1.0f, spacingDegrees - 5.0f);
			const float sweep = glm::radians(sweepDegrees);
			const float rotation = glm::radians(std::isfinite(parameters.rotationDegrees) ? parameters.rotationDegrees : 0.0f);
			const glm::vec3 u{1, 0, 0};
			const glm::vec3 v{0, 1, 0};
			ends[0].position = radius * u;
			ends[1].position = radius * (std::cos(sweep) * u + std::sin(sweep) * v);
			ends[0].binding = PathBinding{PathBinding::Free{}};
			ends[1].binding = PathBinding{PathBinding::Free{}};
			origin = (window.structure.atoms[atomA].cartesianPosition +
				window.structure.atoms[atomB].cartesianPosition) * 0.5f;
			for (int i = 0; i < arrowCount; ++i)
			{
				ScenePath path = MakeDevScenePath(ScenePathDevPreset::Line, glm::vec3(0));
				path.name = "Zakrzywiona strzałka (C_2)";
				path.nodes[0].position = ends[0].position;
				path.nodes[1].position = ends[1].position;
				path.segments[0].data = CircularArcSegmentData{glm::vec3(0, 0, 1), sweep};
				path.transform.position = origin;
				path.transform.rotation = BondFrameRotation(axis);
				path.transformBinding.value = PathTransformBinding::BondFrame{atomA, atomB, rotation + glm::radians(spacingDegrees * i)};
				path.style.shadeSmooth = true;
				path.style.width = parameters.strokeWidth;
				path.style.color = parameters.color;
				path.style.endDecoration.kind = parameters.decoration;
				path.style.endDecoration.lengthScale = 3.0f;
				path.style.endDecoration.widthScale = 1.0f;
				if (!ValidatePath(path).empty())
					return ArrowError("path.edit_invalid_result", "The generated bond-axis arrow is invalid.");
				paths.push_back(std::move(path));
			}
		}
		else for (const auto &end : ends)
		{
			const float radius = glm::length(radial(end.position));
			if (!std::isfinite(radius) || radius <= 1.0e-5f)
				return ArrowError("path.arc_radius_nonpositive", "The arrow ends must be away from the rotation axis.");
		}
		if (!bondMode && cycle)
		{
			const auto u = glm::normalize(radial(ends.front().position)), v = glm::cross(axis, u);
			for (auto &end : ends)
			{
				const auto r = radial(end.position);
				end.angle = std::atan2(glm::dot(r, v), glm::dot(r, u));
			}
			std::stable_sort(ends.begin(), ends.end(), [](const End &a, const End &b) { return a.angle < b.angle; });
		}

		// Prepare and validate the whole batch before changing the scene or its undo history.
		float pairAngle = 0.0f;
		if (!bondMode) for (std::size_t i = 0, count = cycle ? ends.size() : std::clamp(parameters.arrowCount, 1, 2); i < count; ++i)
		{
			const auto &a = ends[i], &b = ends[(i + 1) % ends.size()];
			const auto u = glm::normalize(radial(a.position)), v = glm::normalize(radial(b.position));
			float angle = std::atan2(glm::dot(axis, glm::cross(u, v)), glm::dot(u, v));
			if (std::abs(angle) < 1.0e-4f)
				return ArrowError("path.arc_sweep_out_of_range", "The arrow ends must have different angles around the axis.");
			if (cycle && angle < 0) angle += 2.0f * std::numbers::pi_v<float>;
			if (!cycle)
			{
				if (i == 0) pairAngle = angle;
				angle = pairAngle; // Reversing the chord with the same sweep puts the return arc on the other side.
			}
			ScenePath path = MakeDevScenePath(ScenePathDevPreset::Line, glm::vec3(0));
			path.name = "Zakrzywiona strzałka (C_n)";
			path.transform.position = glm::vec3(0);
			path.nodes[0].position = a.position;
			path.nodes[1].position = b.position;
			path.nodes[0].binding = a.binding;
			path.nodes[1].binding = b.binding;
			// Keep the clearance-bound ends fixed. Scaling sagitta (h = chord/2 * tan(sweep/4))
			// moves the arc outward continuously, without the old sphere-exit/zero-trim switch.
			const float sweep = radiusScale == 1.0f ? angle * curvature :
				4.0f * std::atan(radiusScale * std::tan(angle * curvature * 0.25f));
			// A pair shares one orientation for its chord line, so its bulges stay opposite at every tilt.
			const glm::vec3 chord = glm::normalize(!cycle && i == 1 ? a.position - b.position : b.position - a.position);
			const glm::vec3 normal = glm::normalize(axis - chord * glm::dot(axis, chord));
			const float tilt = glm::radians(std::isfinite(parameters.tiltDegrees) ?
				std::clamp(parameters.tiltDegrees, -180.0f, 180.0f) : 0.0f);
			path.segments[0].data = CircularArcSegmentData{glm::angleAxis(tilt, chord) * normal, sweep};
			path.style.shadeSmooth = true;
			path.style.width = parameters.strokeWidth;
			path.style.color = parameters.color;
			path.style.endDecoration.kind = parameters.decoration;
			path.style.endDecoration.lengthScale = 3.0f;
			path.style.endDecoration.widthScale = 1.0f;
			MovePathOriginToCentre(path);
			const auto diagnostics = ValidatePath(path);
			if (!diagnostics.empty())
				return ArrowError("path.edit_invalid_result", diagnostics.front().message.c_str());
			paths.push_back(std::move(path));
		}
		auto before = CaptureSceneObjectsSnapshot(window);
		std::vector<SceneObjectId> ids;
		const auto context = MakeSilentPathEditContext(window);
		for (auto &path : paths)
		{
			const auto added = AddScenePath(context, std::move(path));
			if (!added)
			{
				RestoreSceneObjectsSnapshot(window, std::move(before));
				return added.Error();
			}
			ids.push_back(added.Value());
		}
		if (undo == SceneOperationUndo::Push)
			PushSceneObjectsUndoSnapshot(window, std::move(before));
		SelectAddedScenePaths(window, ids);
		return ids;
	}
}
