#include "Core/dspch.hpp"

#include "Presentation/Panels/ViewportOrientationTriad.hpp"

#include <algorithm>
#include <array>
#include <cmath>

#include "Renderer/RendererViewCamera.hpp"
#include "Renderer/RendererWindowState.hpp"
#include "Renderer/Scene/SceneTransform.hpp"

namespace DefectStudio
{
	namespace
	{
		constexpr std::array<ImU32, 3> kAxisColors = {
			IM_COL32(230, 70, 70, 255), IM_COL32(90, 210, 90, 255), IM_COL32(90, 150, 240, 255)};

		void DrawAxis(
			ImDrawList &drawList, const glm::vec2 &origin, const glm::vec2 &projectedDirection,
			float axisLength, float scale, ImU32 color, const char *label)
		{
			const glm::vec2 endpoint = origin + projectedDirection * axisLength;
			drawList.AddLine(ImVec2(origin.x, origin.y), ImVec2(endpoint.x, endpoint.y), color, 2.5f * scale);

			const float screenLengthSquared = glm::dot(projectedDirection, projectedDirection);
			if (screenLengthSquared > 1.0e-6f)
			{
				const glm::vec2 direction = projectedDirection / std::sqrt(screenLengthSquared);
				const glm::vec2 perpendicular(-direction.y, direction.x);
				const glm::vec2 headBase = endpoint - direction * (6.0f * scale);
				drawList.AddTriangleFilled(
					ImVec2(endpoint.x, endpoint.y),
					ImVec2(
						headBase.x + perpendicular.x * 3.0f * scale,
						headBase.y + perpendicular.y * 3.0f * scale),
					ImVec2(
						headBase.x - perpendicular.x * 3.0f * scale,
						headBase.y - perpendicular.y * 3.0f * scale),
					color);
			}

			drawList.AddText(
				ImVec2(endpoint.x + 4.0f * scale, endpoint.y - ImGui::GetFontSize() * 0.5f), color, label);
		}
	} // namespace

	OrientationAxes ResolveNormalizedOrientationAxes(
		TransformOrientation orientation, const TransformBases &bases)
	{
		const glm::mat3 basis = ResolveBasis(orientation, bases);
		OrientationAxes axes = {glm::vec3(0.0f), glm::vec3(0.0f), glm::vec3(0.0f)};
		for (int axis = 0; axis < 3; ++axis)
		{
			const float lengthSquared = glm::dot(basis[axis], basis[axis]);
			if (lengthSquared > 1.0e-8f)
				axes[axis] = basis[axis] / std::sqrt(lengthSquared);
		}
		return axes;
	}

	OrientationScreenAxes ProjectOrientationAxes(const glm::mat4 &view, const OrientationAxes &axes)
	{
		OrientationScreenAxes projected = {glm::vec2(0.0f), glm::vec2(0.0f), glm::vec2(0.0f)};
		const glm::mat3 cameraRotation(view);
		for (int axis = 0; axis < 3; ++axis)
		{
			const glm::vec3 cameraDirection = cameraRotation * axes[axis];
			projected[axis] = glm::vec2(cameraDirection.x, -cameraDirection.y);
		}
		return projected;
	}

	bool RenderViewportOrientationTriad(
		RendererWindowState &windowState, const ImVec2 &imageOrigin, const ImVec2 &imageSize, bool hovered)
	{
		if (windowState.camera == nullptr || imageSize.x <= 0.0f || imageSize.y <= 0.0f)
			return false;

		const float scale = std::max(ImGui::GetIO().FontGlobalScale, 0.01f);
		const float axisLength = 35.0f * scale;
		const float padding = 6.0f * scale;
		const float captionHeight = ImGui::GetFontSize();
		const ImVec2 triadSize(axisLength * 2.0f + padding * 2.0f, axisLength * 2.0f + padding * 2.0f + captionHeight);
		const ImVec2 triadMin(
			imageOrigin.x + 4.0f * scale,
			imageOrigin.y + imageSize.y - triadSize.y - 4.0f * scale);
		const ImVec2 triadMax(triadMin.x + triadSize.x, triadMin.y + triadSize.y);
		const glm::vec2 axisOrigin(
			triadMin.x + padding + axisLength,
			triadMin.y + padding + axisLength);

		const TransformOrientation orientation = windowState.transformOrientation;
		TransformBases bases;
		bases.lattice = windowState.structure.lattice;
		bases.local = windowState.modalTransform.has_value()
			? windowState.modalTransform->bases.local
			: SceneTransformLocalBasis(CaptureSceneTransformSelection(windowState));
		const OrientationScreenAxes projected = ProjectOrientationAxes(
			windowState.camera->ViewMatrix(), ResolveNormalizedOrientationAxes(orientation, bases));

		ImDrawList &drawList = *ImGui::GetWindowDrawList();
		drawList.PushClipRect(imageOrigin, ImVec2(imageOrigin.x + imageSize.x, imageOrigin.y + imageSize.y), true);
		drawList.AddRectFilled(triadMin, triadMax, IM_COL32(18, 18, 22, 115), 4.0f * scale);
		const bool lattice = orientation == TransformOrientation::Lattice;
		const std::array<const char *, 3> labels = lattice
			? std::array<const char *, 3>{"a", "b", "c"}
			: std::array<const char *, 3>{"x", "y", "z"};
		for (int axis = 0; axis < 3; ++axis)
			DrawAxis(drawList, axisOrigin, projected[axis], axisLength, scale, kAxisColors[axis], labels[axis]);
		drawList.AddCircleFilled(ImVec2(axisOrigin.x, axisOrigin.y), 3.0f * scale, IM_COL32(235, 235, 235, 255));

		const char *caption = "Global";
		if (orientation == TransformOrientation::Local)
			caption = "Local";
		else if (orientation == TransformOrientation::Lattice)
			caption = "Lattice";
		const ImVec2 captionSize = ImGui::CalcTextSize(caption);
		drawList.AddText(
			ImVec2(
				triadMin.x + (triadSize.x - captionSize.x) * 0.5f,
				triadMax.y - captionHeight),
			IM_COL32(235, 235, 235, 255), caption);
		drawList.PopClipRect();

		const bool triadHovered = hovered && ImGui::IsMouseHoveringRect(triadMin, triadMax);
		if (triadHovered)
			ImGui::SetTooltip("Transform orientation: %s - click to switch", caption);
		if (!triadHovered || windowState.modalTransform.has_value() ||
			!ImGui::IsMouseClicked(ImGuiMouseButton_Left))
			return false;

		windowState.transformOrientation = lattice
			? TransformOrientation::Global
			: TransformOrientation::Lattice;
		return true;
	}
} // namespace DefectStudio
