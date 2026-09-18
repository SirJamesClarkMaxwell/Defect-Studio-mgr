#include "Core/dspch.hpp"

#include "Presentation/Panels/SceneArrowEditorWidget.hpp"

#include <algorithm>
#include <functional>
#include <utility>

#include <imgui.h>

#include "Core/Commands/Command.hpp"
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

	namespace
	{
		class ReverseSelectedSceneArrowsCommand final : public ICommand
		{
		public:
			explicit ReverseSelectedSceneArrowsCommand(RendererLayer &rendererLayer)
				: m_RendererLayer(rendererLayer)
			{
			}

			Result<void> Execute(CommandContext &) override
			{
				std::vector<RendererWindowState> &windows = m_RendererLayer.get().GetWindows();
				const std::string &focusedWindowId = m_RendererLayer.get().GetFocusedViewportWindowId();
				auto window = focusedWindowId.empty() && windows.size() == 1
					? windows.begin()
					: std::find_if(
						windows.begin(), windows.end(),
						[&focusedWindowId](const RendererWindowState &candidate) {
							return candidate.windowId == focusedWindowId;
						});
				if (window == windows.end() || window->selectedSceneArrows.empty())
					return {};

				std::vector<std::size_t> selectedIndices;
				selectedIndices.reserve(window->selectedSceneArrows.size());
				for (const SceneObjectId id : window->selectedSceneArrows)
				{
					const std::size_t index = FindObjectIndex(window->sceneArrows, id);
					if (index < window->sceneArrows.size())
						selectedIndices.push_back(index);
				}
				if (selectedIndices.empty())
					return {};

				PushPinnedMeasurementUndoSnapshot(*window);
				for (const std::size_t index : selectedIndices)
					ReverseSceneArrow(window->sceneArrows[index]);
				return {};
			}

			[[nodiscard]] std::string Description() const override
			{
				return "Reverse selected scene arrows";
			}

		private:
			std::reference_wrapper<RendererLayer> m_RendererLayer;
		};
	} // namespace

	Unique<ICommand> CreateReverseSelectedSceneArrowsCommand(RendererLayer &rendererLayer)
	{
		return CreateUnique<ReverseSelectedSceneArrowsCommand>(rendererLayer);
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
	static void OffsetSceneArrowCopy(RendererWindowState::SceneArrow &arrow)
	{
		for (glm::vec3 &point : arrow.points)
			point += kArrowDuplicateOffset;
		if (arrow.controlPoint)
			*arrow.controlPoint += kArrowDuplicateOffset;
		// An anchored copy cannot keep a visible offset: the live refresh would immediately put it
		// back on the source atoms. The offset therefore becomes a new free placement.
		arrow.startAnchorAtom.reset();
		arrow.endAnchorAtom.reset();
	}

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
			OffsetSceneArrowCopy(copy);
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
			OffsetSceneArrowCopy(copy);
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
		// Session-wide default for newly anchored arrows only. Existing arrows read their own
		// SceneArrow::atomBuffer, so changing this cannot rewrite an object already in the scene.
		static float buffer = 1.15f;
		return buffer;
	}

	void MatchSceneArrowPositionToAtoms(
		RendererWindowState::SceneArrow &arrow,
		const RendererAtomData &startAtom,
		const RendererAtomData &endAtom,
		const float radiusBuffer)
	{
		arrow.start() = startAtom.cartesianPosition;
		arrow.end() = endAtom.cartesianPosition;
		SceneSystem::ApplySceneArrowAtomBuffer(arrow, startAtom.radius, endAtom.radius, radiusBuffer);
	}

	static void DrawSceneArrowAtomBufferTooltip()
	{
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
			ImGui::SetTooltip(
				"Odleglosc konca od srodka atomu, w promieniach kuli. "
				"0 = srodek, 1.0 = powierzchnia, 1.15 = odstep 15%% promienia.");
	}

	void DrawSceneArrowAtomBufferControl()
	{
		ImGui::SetNextItemWidth(120.0f);
		ImGui::DragFloat("Bufor##SceneArrowAtomBuffer", &GetSceneArrowAtomBuffer(), 0.02f, 0.0f, 3.0f, "%.2f r");
		DrawSceneArrowAtomBufferTooltip();
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
		std::reverse(arrow.points.begin(), arrow.points.end());
		std::swap(arrow.startTip, arrow.endTip);
		std::swap(arrow.startAnchorAtom, arrow.endAnchorAtom);
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

		RendererWindowState::SceneArrow &arrow = windowState.sceneArrows[arrowIndex];
		const SceneArrowAtomMatchDescription description = DescribeSceneArrowAtomMatch(validAtomIndices.size());
		const auto drawAnchor = [&](const char *label, std::optional<std::size_t> &anchor) {
			ImGui::PushID(label);
			if (!anchor.has_value())
				ImGui::TextDisabled("%s: wolny", label);
			else if (*anchor < windowState.structure.atoms.size())
				ImGui::Text("%s: %s #%zu", label, windowState.structure.atoms[*anchor].element.c_str(), *anchor);
			else
				ImGui::Text("%s: brak atomu #%zu", label, *anchor);
			if (anchor.has_value())
			{
				ImGui::SameLine();
				if (ImGui::SmallButton("Odczep"))
				{
					PushPinnedMeasurementUndoSnapshot(windowState);
					anchor.reset();
				}
			}
			ImGui::PopID();
		};
		drawAnchor("Start", arrow.startAnchorAtom);
		drawAnchor("End", arrow.endAnchorAtom);

		ImGui::BeginDisabled(!description.canMatchPosition);
		const bool attach = ImGui::Button("Zakotwicz na zaznaczonych atomach");
		ImGui::EndDisabled();
		if (!description.canMatchPosition &&
			ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort))
			ImGui::SetTooltip("%s", description.positionTooltip.c_str());
		if (attach)
		{
			PushPinnedMeasurementUndoSnapshot(windowState);
			arrow.startAnchorAtom = validAtomIndices[0];
			arrow.endAnchorAtom = validAtomIndices[1];
			MatchSceneArrowPositionToAtoms(
				arrow,
				windowState.structure.atoms[validAtomIndices[0]],
				windowState.structure.atoms[validAtomIndices[1]],
				arrow.atomBuffer);
		}

		const bool freeArrow = !arrow.startAnchorAtom.has_value() && !arrow.endAnchorAtom.has_value();
		ImGui::BeginDisabled(freeArrow);
		float editedBuffer = arrow.atomBuffer;
		ImGui::SetNextItemWidth(120.0f);
		const bool bufferChanged = ImGui::DragFloat(
			"Bufor##SceneArrowLiveAtomBuffer", &editedBuffer, 0.02f, 0.0f, 3.0f, "%.2f r");
		if (ImGui::IsItemActivated())
			PushPinnedMeasurementUndoSnapshot(windowState);
		if (bufferChanged)
		{
			arrow.atomBuffer = editedBuffer;
			SceneSystem::RefreshAnchoredSceneArrows(windowState);
		}
		DrawSceneArrowAtomBufferTooltip();
		ImGui::EndDisabled();

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
