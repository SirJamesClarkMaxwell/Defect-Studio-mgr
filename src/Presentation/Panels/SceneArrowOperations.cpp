#include "Core/dspch.hpp"

#include "Presentation/Panels/SceneArrowEditorWidget.hpp"

#include <algorithm>
#include <functional>
#include <utility>

#include <imgui.h>

#include "Renderer/RendererLayer.hpp"
#include "Renderer/Scene/SceneSystem.hpp"

namespace DefectStudio
{
	template <typename T>
	[[nodiscard]] std::size_t FindObjectIndex(const std::vector<T> &objects, const SceneObjectId id)
	{
		const auto found = std::find_if(objects.begin(), objects.end(), [id](const T &object) { return object.id == id; });
		return found == objects.end() ? objects.size() : static_cast<std::size_t>(std::distance(objects.begin(), found));
	}

	void EraseSceneArrows(RendererWindowState &windowState, std::vector<SceneObjectId> ids)
	{
		std::vector<std::size_t> indices;
		for (const SceneObjectId id : ids)
			indices.push_back(FindObjectIndex(windowState.sceneArrows, id));
		std::sort(indices.begin(), indices.end(), std::greater<>());
		indices.erase(std::unique(indices.begin(), indices.end()), indices.end());

		bool erasedAny = false;
		for (const std::size_t index : indices)
		{
			if (index >= windowState.sceneArrows.size())
				continue;
			windowState.sceneArrows.erase(windowState.sceneArrows.begin() + static_cast<std::ptrdiff_t>(index));
			erasedAny = true;
		}

		windowState.selectedSceneArrows.clear();
		windowState.sceneArrowDragging = false;
		if (erasedAny)
		{
			windowState.sceneArrowQuickEditActive = false;
			SceneSystem::SyncLabelEntities(windowState.sceneRegistry, windowState);
		}
	}

	std::vector<RendererWindowState::SceneArrow> &GetSceneArrowClipboard()
	{
		static std::vector<RendererWindowState::SceneArrow> clipboard;
		return clipboard;
	}

	void CopySceneArrowsToClipboard(const RendererWindowState &windowState)
	{
		std::vector<RendererWindowState::SceneArrow> &clipboard = GetSceneArrowClipboard();
		clipboard.clear();
		for (const SceneObjectId id : windowState.selectedSceneArrows)
		{
			const std::size_t index = FindObjectIndex(windowState.sceneArrows, id);
			if (index < windowState.sceneArrows.size())
				clipboard.push_back(windowState.sceneArrows[index]);
		}
	}

	// Flat world-space offset so the copy doesn't land exactly on top of the original - same constant
	// (and same non-cleverness) as RendererAtomEditCommands.cpp's kDuplicateOffset/kPasteOffset.
	static constexpr glm::vec3 kArrowDuplicateOffset(0.5f, 0.0f, 0.0f);

	void DuplicateSelectedSceneArrows(RendererWindowState &windowState)
	{
		if (windowState.selectedSceneArrows.empty())
			return;
		PushPinnedMeasurementUndoSnapshot(windowState);

		std::vector<SceneObjectId> newIds;
		newIds.reserve(windowState.selectedSceneArrows.size());
		for (const SceneObjectId id : windowState.selectedSceneArrows)
		{
			const std::size_t index = FindObjectIndex(windowState.sceneArrows, id);
			if (index >= windowState.sceneArrows.size())
				continue;
			RendererWindowState::SceneArrow copy = windowState.sceneArrows[index];
			copy.id = windowState.sceneRegistry.AllocateObjectId();
			copy.persistKey.clear();
			newIds.push_back(copy.id);
			copy.start += kArrowDuplicateOffset;
			copy.end += kArrowDuplicateOffset;
			windowState.sceneArrows.push_back(std::move(copy));
		}
		windowState.selectedSceneArrows = std::move(newIds);
	}

	void PasteSceneArrowsFromClipboard(RendererWindowState &windowState)
	{
		const std::vector<RendererWindowState::SceneArrow> &clipboard = GetSceneArrowClipboard();
		if (clipboard.empty())
			return;
		PushPinnedMeasurementUndoSnapshot(windowState);

		std::vector<SceneObjectId> newIds;
		newIds.reserve(clipboard.size());
		for (const RendererWindowState::SceneArrow &arrow : clipboard)
		{
			RendererWindowState::SceneArrow copy = arrow;
			copy.id = windowState.sceneRegistry.AllocateObjectId();
			copy.persistKey.clear();
			newIds.push_back(copy.id);
			copy.start += kArrowDuplicateOffset;
			copy.end += kArrowDuplicateOffset;
			windowState.sceneArrows.push_back(std::move(copy));
		}
		windowState.selectedSceneArrows = std::move(newIds);
	}

	SceneArrowAtomMatchDescription DescribeSceneArrowAtomMatch(const std::size_t validSelectedAtomCount)
	{
		SceneArrowAtomMatchDescription description;
		description.canMatchPosition = validSelectedAtomCount == 2;
		description.canMatchColor = validSelectedAtomCount == 1 || validSelectedAtomCount == 2;
		if (!description.canMatchPosition)
			description.positionTooltip = "Zaznacz dokladnie dwa atomy, aby dopasowac pozycje.";
		if (!description.canMatchColor)
			description.colorTooltip = "Zaznacz jeden lub dwa atomy, aby dopasowac kolor.";
		return description;
	}

	float &GetSceneArrowAtomBuffer()
	{
		// Session-wide, deliberately not per-arrow: it is the setting for the *act* of snapping an
		// arrow to two atoms, not a property of the arrow that results - once drawn, the endpoints
		// are ordinary coordinates the user can drag. 1.0 is tangent to the drawn sphere; the 1.15
		// default leaves a visible gap equal to 15% of that atom's radius.
		static float buffer = 1.15f;
		return buffer;
	}

	void MatchSceneArrowPositionToAtoms(
		RendererWindowState::SceneArrow &arrow,
		const RendererAtomData &startAtom,
		const RendererAtomData &endAtom,
		const float radiusBuffer)
	{
		arrow.start = startAtom.cartesianPosition;
		arrow.end = endAtom.cartesianPosition;
		if (radiusBuffer <= 0.0f)
			return;

		const glm::vec3 delta = arrow.end - arrow.start;
		const float distance = glm::length(delta);
		const float requested = radiusBuffer * (startAtom.radius + endAtom.radius);
		if (distance <= 1e-4f || requested <= 1e-4f)
			return;

		// Overlapping spheres - a short bond, or a big buffer - would otherwise trim past the far
		// end and draw the arrow backwards. Shrink both gaps together so the arrow keeps a tenth
		// of its length instead of inverting.
		const float scale = std::min(1.0f, 0.9f * distance / requested);
		const glm::vec3 direction = delta / distance;
		arrow.start += direction * (radiusBuffer * startAtom.radius * scale);
		arrow.end -= direction * (radiusBuffer * endAtom.radius * scale);
	}

	void DrawSceneArrowAtomBufferControl()
	{
		ImGui::SetNextItemWidth(120.0f);
		ImGui::DragFloat("Bufor##SceneArrowAtomBuffer", &GetSceneArrowAtomBuffer(), 0.02f, 0.0f, 3.0f, "%.2f r");
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
			ImGui::SetTooltip(
				"Odleglosc konca od srodka atomu, w promieniach kuli. "
				"0 = srodek, 1.0 = powierzchnia, 1.15 = odstep 15%% promienia.");
	}

	void MatchSceneArrowColorToAtom(RendererWindowState::SceneArrow &arrow, const RendererAtomData &atom)
	{
		arrow.style.useGradient = false;
		arrow.style.color = atom.color;
	}

	void MatchSceneArrowColorToAtoms(
		RendererWindowState::SceneArrow &arrow, const RendererAtomData &startAtom, const RendererAtomData &endAtom)
	{
		arrow.style.useGradient = true;
		arrow.style.gradient.start = startAtom.color;
		arrow.style.gradient.finish = endAtom.color;
	}

	void ReverseSceneArrow(RendererWindowState::SceneArrow &arrow)
	{
		std::swap(arrow.start, arrow.end);
	}

	void DrawSceneArrowAtomMatchActions(RendererWindowState &windowState, const std::size_t arrowIndex)
	{
		if (arrowIndex >= windowState.sceneArrows.size())
			return;

		std::vector<std::size_t> validAtomIndices;
		validAtomIndices.reserve(windowState.selectedAtomIndices.size());
		for (const std::size_t atomIndex : windowState.selectedAtomIndices)
			if (atomIndex < windowState.structure.atoms.size())
				validAtomIndices.push_back(atomIndex);

		const SceneArrowAtomMatchDescription description = DescribeSceneArrowAtomMatch(validAtomIndices.size());
		DrawSceneArrowAtomBufferControl();
		ImGui::SameLine();
		ImGui::BeginDisabled(!description.canMatchPosition);
		const bool matchPosition = ImGui::Button("Match position##SceneArrowMatchPosition");
		ImGui::EndDisabled();
		if (!description.canMatchPosition &&
			ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort))
		{
			ImGui::SetTooltip("%s", description.positionTooltip.c_str());
		}
		RendererWindowState::SceneArrow &arrow = windowState.sceneArrows[arrowIndex];
		if (matchPosition)
		{
			PushPinnedMeasurementUndoSnapshot(windowState);
			MatchSceneArrowPositionToAtoms(
				arrow,
				windowState.structure.atoms[validAtomIndices[0]],
				windowState.structure.atoms[validAtomIndices[1]],
				GetSceneArrowAtomBuffer());
		}

		ImGui::SameLine();
		ImGui::BeginDisabled(!description.canMatchColor);
		const bool matchColor = ImGui::Button("Match colour##SceneArrowMatchColor");
		ImGui::EndDisabled();
		if (!description.canMatchColor &&
			ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort))
		{
			ImGui::SetTooltip("%s", description.colorTooltip.c_str());
		}
		if (!matchColor)
			return;

		PushPinnedMeasurementUndoSnapshot(windowState);
		const RendererAtomData &firstAtom = windowState.structure.atoms[validAtomIndices[0]];
		if (validAtomIndices.size() == 1)
			MatchSceneArrowColorToAtom(arrow, firstAtom);
		else
			MatchSceneArrowColorToAtoms(arrow, firstAtom, windowState.structure.atoms[validAtomIndices[1]]);
	}

	std::optional<RendererWindowState::ArrowStyle> &GetArrowGeometryClipboard()
	{
		static std::optional<RendererWindowState::ArrowStyle> clipboard;
		return clipboard;
	}

	std::optional<RendererWindowState::ArrowStyle> &GetArrowStyleClipboard()
	{
		static std::optional<RendererWindowState::ArrowStyle> clipboard;
		return clipboard;
	}

	void CopyArrowGeometry(const RendererWindowState::ArrowStyle &style)
	{
		GetArrowGeometryClipboard() = style;
	}

	void CopyArrowStyle(const RendererWindowState::ArrowStyle &style)
	{
		GetArrowStyleClipboard() = style;
	}

	bool PasteArrowGeometry(RendererWindowState &windowState, const std::vector<SceneObjectId> &targets)
	{
		const std::optional<RendererWindowState::ArrowStyle> &clipboard = GetArrowGeometryClipboard();
		if (!clipboard.has_value() || targets.empty())
			return false;
		for (const SceneObjectId id : targets)
		{
			const std::size_t index = FindObjectIndex(windowState.sceneArrows, id);
			if (index >= windowState.sceneArrows.size())
				continue;
			RendererWindowState::ArrowStyle &style = windowState.sceneArrows[index].style;
			style.shaftWidth = clipboard->shaftWidth;
			style.dashed = clipboard->dashed;
			style.dashLength = clipboard->dashLength;
			style.gapLength = clipboard->gapLength;
			style.headWidth = clipboard->headWidth;
			style.headLength = clipboard->headLength;
			style.outlineWidth = clipboard->outlineWidth;
		}
		return true;
	}

	bool PasteArrowStyle(RendererWindowState &windowState, const std::vector<SceneObjectId> &targets)
	{
		const std::optional<RendererWindowState::ArrowStyle> &clipboard = GetArrowStyleClipboard();
		if (!clipboard.has_value() || targets.empty())
			return false;
		for (const SceneObjectId id : targets)
		{
			const std::size_t index = FindObjectIndex(windowState.sceneArrows, id);
			if (index >= windowState.sceneArrows.size())
				continue;
			RendererWindowState::ArrowStyle &style = windowState.sceneArrows[index].style;
			style.color = clipboard->color;
			style.alpha = clipboard->alpha;
			style.outlineColor = clipboard->outlineColor;
		}
		return true;
	}

} // namespace DefectStudio
