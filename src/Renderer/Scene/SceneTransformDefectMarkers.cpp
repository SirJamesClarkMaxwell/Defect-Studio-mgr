#include "Core/dspch.hpp"

#include "Renderer/Scene/SceneTransformDefectMarkers.hpp"

#include <algorithm>

#include <glm/gtc/quaternion.hpp>

#include "Renderer/RendererWindowState.hpp"

namespace DefectStudio
{
	void CaptureSceneTransformDefectMarkers(const RendererWindowState &window, SceneTransformSelectionSnapshot &snapshot)
	{
		const auto &frame = window.structure.defectFrame;
		const bool frameSelected = window.defectFrameSelected && frame && window.showDefectFrame;
		std::vector<std::size_t> indices = window.selectedVacancies;
		if (frameSelected)
			for (const std::size_t child : window.defectFrameChildren.vacancies)
				if (std::find(indices.begin(), indices.end(), child) == indices.end())
					indices.push_back(child);
		for (const std::size_t index : indices)
			if (index < window.structure.vacancies.size())
				snapshot.vacancies.push_back({index, window.structure.vacancies[index].cartesianPosition});
		if (frameSelected)
			snapshot.defectFrame = DefectFrameTransformStart{frame->origin, frame->x, frame->y, frame->z};
	}

	void ApplySceneTransformDefectMarkers(RendererWindowState &window, const SceneTransformSelectionSnapshot &snapshot,
		const SceneTransformDelta &delta, TransformPivotMode pivotMode, const glm::vec3 &selectionPivot)
	{
		auto pivotFor = [&](const glm::vec3 &start) {
			return pivotMode == TransformPivotMode::IndividualOrigins ? start : selectionPivot;
		};
		for (const VacancyTransformStart &start : snapshot.vacancies)
			if (start.index < window.structure.vacancies.size())
				window.structure.vacancies[start.index].cartesianPosition =
					ApplyTransformDelta(delta.spatial, start.position, pivotFor(start.position));
		if (!snapshot.defectFrame || !window.structure.defectFrame)
			return;
		const DefectFrameTransformStart &start = *snapshot.defectFrame;
		DefectFrame &frame = *window.structure.defectFrame;
		frame.origin = ApplyTransformDelta(delta.spatial, start.origin, pivotFor(start.origin));
		// Only the rotation part turns the axes; a scale or shear would stop them being orthonormal.
		frame.x = glm::normalize(delta.spatial.rotation * start.x);
		frame.z = glm::normalize(delta.spatial.rotation * start.z);
		frame.y = glm::cross(frame.z, frame.x);
	}

	void RestoreSceneTransformDefectMarkers(RendererWindowState &window, const SceneTransformSelectionSnapshot &snapshot)
	{
		for (const VacancyTransformStart &start : snapshot.vacancies)
			if (start.index < window.structure.vacancies.size())
				window.structure.vacancies[start.index].cartesianPosition = start.position;
		if (snapshot.defectFrame && window.structure.defectFrame)
			*window.structure.defectFrame = DefectFrame{
				snapshot.defectFrame->origin, snapshot.defectFrame->x, snapshot.defectFrame->y, snapshot.defectFrame->z};
	}
} // namespace DefectStudio
