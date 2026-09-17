#include "Core/dspch.hpp"

#include "Presentation/Panels/SceneArrowEditorWidget.hpp"

#include <algorithm>
#include <functional>

#include "Renderer/RendererLayer.hpp"

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
			windowState.sceneArrowQuickEditActive = false;
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

