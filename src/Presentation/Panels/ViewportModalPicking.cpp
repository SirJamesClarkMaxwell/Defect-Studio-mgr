#include "Core/dspch.hpp"

#include "Presentation/Panels/ViewportModalPicking.hpp"

#include <cmath>

#include "Presentation/Panels/ViewportPicking.hpp"
#include "Presentation/Panels/ViewportSelection.hpp"
#include "Renderer/RendererWindowState.hpp"
#include "Renderer/Scene/SelectionHitTest.hpp"

namespace DefectStudio
{
	std::optional<std::size_t> PickViewportModalAtomAlongRay(
		const RendererWindowState &window, const glm::vec3 &origin, const glm::vec3 &direction)
	{
		// The existing picker needs only atom geometry and showAtoms. Filter a local copy so even
		// an unmoved atom behind the selection can be picked without changing the visible preview.
		RendererWindowState picking;
		picking.showAtoms = window.showAtoms;
		picking.structure.atoms = window.structure.atoms;
		for (const AtomTransformStart &atom : window.modalTransformSelection.atoms)
			if (atom.index < picking.structure.atoms.size())
				picking.structure.atoms[atom.index].visible = false;
		return PickAtomAlongRay(picking, origin, direction);
	}

	void ApplyViewportModalAtomPick(RendererWindowState &window, std::size_t atomIndex, const glm::vec2 &mouse)
	{
		if (!window.modalTransform || !window.modalPickedAxis.active)
			return;
		for (const AtomTransformStart &atom : window.modalTransformSelection.atoms)
			if (atom.index == atomIndex)
				return;
		const bool pivot = window.modalPickedAxis.purpose == ModalPickPurpose::Pivot;
		ModalTransformSession &session = *window.modalTransform;
		window.modalPickedAxis.Pick(window.structure, atomIndex, session, mouse);
		if (pivot && !window.modalPickedAxis.active)
		{
			window.modalTransformPreviewSelection = CaptureSceneTransformSelection(window);
			const auto positions = SceneTransformAnchorPositions(window, *window.modalTransformPreviewSelection);
			session.anchor = ComputeTransformPivot(TransformPivotMode::Median, positions, std::nullopt);
			window.modalLastDelta = {};
		}
	}

	bool UpdateViewportModalPicking(RendererWindowState &window, const ModalTransformView &view, const glm::vec2 &mouse)
	{
		ModalTransformSession &session = *window.modalTransform;
		std::optional<ModalPickPurpose> purpose;
		if (session.op != ModalTransformOp::Scale && IsUnmodifiedModalAxisKeyPressed(ImGuiKey_V))
			purpose = ModalPickPurpose::AxisA;
		else if (session.op != ModalTransformOp::Translate && IsUnmodifiedModalAxisKeyPressed(ImGuiKey_P))
			purpose = ModalPickPurpose::Pivot;
		else if (session.op == ModalTransformOp::Translate && IsUnmodifiedModalAxisKeyPressed(ImGuiKey_D))
			purpose = ModalPickPurpose::DistanceReference;
		if (purpose)
		{
			window.modalPickedAxis = {true, std::nullopt, {}, *purpose};
			window.modalTransformStartedFromHandle = false;
		}
		if (!window.modalPickedAxis.active)
			return false;

		if (session.op == ModalTransformOp::Scale)
		{
			const auto projected = SelectionHitTest::ProjectToScreen(view.projection * view.view, view.viewportSize, session.pivot);
			if (projected)
			{
				const glm::vec2 pivot = view.viewportOrigin + *projected;
				const float previousRadius = glm::distance(session.lastMouse, pivot);
				if (previousRadius > 1.0e-6f)
					session.startMouse = pivot + (session.startMouse - pivot) * (glm::distance(mouse, pivot) / previousRadius);
			}
		}
		else
			session.startMouse += mouse - session.lastMouse;
		session.lastMouse = mouse;
		window.modalSnapTargetHit.reset();
		if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsMouseClicked(ImGuiMouseButton_Right))
			window.modalPickedAxis = {};
		else if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && mouse.x >= view.viewportOrigin.x &&
			mouse.y >= view.viewportOrigin.y && mouse.x <= view.viewportOrigin.x + view.viewportSize.x &&
			mouse.y <= view.viewportOrigin.y + view.viewportSize.y)
		{
			const auto ray = ComputeViewportPickRay(window, mouse - view.viewportOrigin, view.viewportSize);
			if (const auto atom = ray ? PickViewportModalAtomAlongRay(window, ray->origin, ray->direction) : std::nullopt)
				ApplyViewportModalAtomPick(window, *atom, mouse);
		}
		return true;
	}
} // namespace DefectStudio
