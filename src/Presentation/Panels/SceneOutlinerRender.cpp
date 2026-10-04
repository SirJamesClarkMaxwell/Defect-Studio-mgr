#include "Core/dspch.hpp"
#include "Presentation/Panels/SceneOutlinerPanel.hpp"

#include <cstdio>
#include <map>
#include <optional>
#include <imgui.h>
#include "Presentation/Panels/RendererTabChrome.hpp"
#include "Presentation/Panels/SceneObjectEditActions.hpp"
#include "Presentation/Panels/SceneOutlinerVisibilityColumns.hpp"
#include "Presentation/Panels/ViewportDefectFrame.hpp"
#include "Renderer/Scene/SceneComponents.hpp"
#include "Renderer/Scene/SceneSystem.hpp"

namespace DefectStudio
{
	namespace
	{
		// The window row stands for everything under it, in both columns.
		[[nodiscard]] SceneVisibilityColumnState WindowContentColumnState(const RendererWindowState &windowState)
		{
			SceneVisibilityColumnAccumulator flags;
			AddSceneVisibilityColumnFlags(windowState.structure.atoms, flags);
			AddSceneVisibilityColumnFlags(windowState.structure.bonds, flags);
			AddSceneVisibilityColumnFlags(windowState.pinnedMeasurements, flags);
			AddSceneVisibilityColumnFlags(windowState.freeLabels, flags);
			AddSceneVisibilityColumnFlags(windowState.sceneOrbitals, flags);
			AddSceneVisibilityColumnFlags(windowState.scenePlanes, flags);
			if (windowState.paths != nullptr)
				windowState.paths->Store().Visit([&flags](const ScenePath &path) { flags.Add(path.visible, path.renderable); });
			return SceneVisibilityStateFrom(flags);
		}

		void ApplyWindowContentColumnEdit(RendererWindowState &windowState, const SceneVisibilityColumnEdit &edit)
		{
			if (!edit.visibleChanged && !edit.renderableChanged)
				return;
			entt::registry &registry = windowState.sceneRegistry.Registry();
			for (const entt::entity entity : registry.view<VisibilityComponent>())
			{
				VisibilityComponent &visibility = registry.get<VisibilityComponent>(entity);
				if (edit.visibleChanged)
					visibility.visible = edit.visible;
				if (edit.renderableChanged)
					visibility.renderable = edit.renderable;
			}
			ApplySceneVisibilityColumnEdit(windowState.pinnedMeasurements, edit);
			ApplySceneVisibilityColumnEdit(windowState.freeLabels, edit);
			ApplySceneVisibilityColumnEdit(windowState.sceneOrbitals, edit);
			ApplySceneVisibilityColumnEdit(windowState.scenePlanes, edit);
			if (windowState.paths != nullptr)
				ApplySceneVisibilityColumnEdit(windowState.paths->Store(), edit);
			SceneSystem::PushSelectionAndVisibilityToWindowState(windowState.sceneRegistry, windowState);
		}

	}

	void SceneOutlinerPanel::Render()
	{
		if (!IsVisible())
		{
			// Hiding the panel while it held focus would otherwise leave nav switched on for good,
			// and the viewport's arrow-key camera shortcuts would stop working.
			if (m_HadKeyboardFocus)
				applyKeyboardNavFlag(false);
			return;
		}

		// Dear ImGui's own keyboard nav, switched on only while this panel holds focus: arrows walk
		// the rows and scroll them into view, Enter/Space activates the focused one. The app-wide flag
		// stays off on purpose (see ProjectTreePanel.hpp) because the viewport's camera shortcuts are
		// bare arrow keys - but the viewport is not focused while the outliner is, so the two never
		// contest a key.
		//
		// The flag is written at the END of the frame, never set-then-restored inside one: ImGui
		// latches ConfigFlags in NewFrame, so a value that does not outlive Render() is a value
		// NewFrame never sees. That was why the arrow keys did nothing at all.
		bool windowOpen = true;
		if (!ImGui::Begin(GetTitle().c_str(), &windowOpen))
		{
			ImGui::End();
			SetVisible(windowOpen);
			applyKeyboardNavFlag(false);
			return;
		}

		m_HadKeyboardFocus = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);
		pollCopyJob();

		std::vector<RendererWindowState> &windows = m_Layer.GetWindows();
		const std::string displayedWindowId = ResolveActiveRendererWindowId(m_Layer);
		if (m_DisplayedWindowId != displayedWindowId)
		{
			m_DisplayedWindowId = displayedWindowId;
			m_EditingWindowIndex = -1;
			m_SelectionAnchor.reset();
		}
		m_ActiveWindowIndex = -1;
		for (int i = 0; i < static_cast<int>(windows.size()); ++i)
			if (windows[static_cast<std::size_t>(i)].windowId == displayedWindowId)
				m_ActiveWindowIndex = i;
		if (m_EditingWindowIndex != m_ActiveWindowIndex)
			m_EditingWindowIndex = -1;
		if (m_HadKeyboardFocus && ImGui::IsKeyPressed(ImGuiKey_Escape, false))
			clearSelection();

		// Delete/Ctrl+C/Ctrl+D/Ctrl+V for whichever scene-object kind is selected, mirroring the
		// viewport's own shortcuts (ViewportLabelInteraction.cpp) exactly - same dispatcher, same
		// chords, just gated on this panel holding focus instead of the mouse hovering the 3D view.
		// Previously the row context menu was the only way to reach these from the outliner at all.
		if (m_HadKeyboardFocus && m_ActiveWindowIndex >= 0)
		{
			RendererWindowState &activeWindow = windows[static_cast<std::size_t>(m_ActiveWindowIndex)];
			const std::optional<SceneObjectEditKind> selectedKind = !activeWindow.selectedScenePaths.empty()
				? std::optional<SceneObjectEditKind>{SceneObjectEditKind::Path}
				: !activeWindow.selectedSceneOrbitals.empty()
				? std::optional<SceneObjectEditKind>{SceneObjectEditKind::Orbital}
				: !activeWindow.selectedScenePlanes.empty()
				? std::optional<SceneObjectEditKind>{SceneObjectEditKind::Plane}
				: !activeWindow.selectedFreeLabels.empty()
				? std::optional<SceneObjectEditKind>{SceneObjectEditKind::FreeLabel}
				: std::nullopt;
			const ImGuiIO &io = ImGui::GetIO();
			if (selectedKind.has_value() && ImGui::IsKeyPressed(ImGuiKey_Delete, false))
				ExecuteSceneObjectEditAction(activeWindow, *selectedKind, SceneObjectEditAction::Delete);
			if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C, false) && selectedKind.has_value())
				ExecuteSceneObjectEditAction(activeWindow, *selectedKind, SceneObjectEditAction::Copy);
			if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D, false) && selectedKind.has_value())
				ExecuteSceneObjectEditAction(activeWindow, *selectedKind, SceneObjectEditAction::Duplicate);
			if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V, false))
				ExecuteSceneObjectEditAction(
					activeWindow, selectedKind.value_or(LastCopiedSceneObjectEditKind()), SceneObjectEditAction::Paste);
		}

		if (m_ActiveWindowIndex < 0)
			ImGui::TextDisabled("No displayed renderer scene.");

		for (int i = 0; i < static_cast<int>(windows.size()); ++i)
		{
			if (i != m_ActiveWindowIndex)
				continue;
			RendererWindowState &windowState = windows[static_cast<std::size_t>(i)];
			ImGui::PushID(i);

			if (m_EditingWindowIndex == i)
			{
				if (m_JustStartedEditing)
				{
					ImGui::SetKeyboardFocusHere();
					m_JustStartedEditing = false;
				}
				ImGui::SetNextItemWidth(-1.0f);
				ImGui::SetNextItemAllowOverlap();
				const bool committed = ImGui::InputText(
					"##rename", m_EditingBuffer.data(), m_EditingBuffer.size(),
					ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
				const bool deactivated = ImGui::IsItemDeactivated();
				ApplyWindowContentColumnEdit(
					windowState, DrawSceneVisibilityColumns(WindowContentColumnState(windowState)));
				if (committed || deactivated)
				{
					if (m_EditingBuffer[0] != '\0')
						windowState.title = m_EditingBuffer.data();
					m_EditingWindowIndex = -1;
				}
			}
			else
			{
				ImGui::SetNextItemAllowOverlap();
				const bool open = ImGui::TreeNodeEx(
					"##win", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth, "%s",
					windowState.title.c_str());
				if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
					m_ActiveWindowIndex = i;
				if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
				{
					m_ActiveWindowIndex = i;
					m_EditingWindowIndex = i;
					std::snprintf(m_EditingBuffer.data(), m_EditingBuffer.size(), "%s", windowState.title.c_str());
					m_JustStartedEditing = true;
				}
				if (m_ActiveWindowIndex == i && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
					ImGui::IsKeyPressed(ImGuiKey_F2, false))
				{
					m_EditingWindowIndex = i;
					std::snprintf(m_EditingBuffer.data(), m_EditingBuffer.size(), "%s", windowState.title.c_str());
					m_JustStartedEditing = true;
				}

				if (ImGui::BeginPopupContextItem())
				{
					const bool isCopying = m_PendingCopyJob != nullptr;
					if (ImGui::BeginMenu("Copy view + visibility to", !isCopying))
					{
						bool anyOther = false;
						for (int otherIndex = 0; otherIndex < static_cast<int>(windows.size()); ++otherIndex)
						{
							if (otherIndex == i)
								continue;
							anyOther = true;
							const RendererWindowState &otherWindow = windows[static_cast<std::size_t>(otherIndex)];
							if (ImGui::MenuItem(otherWindow.title.c_str()))
								dispatchCopyViewAndVisibility(windowState, otherWindow.windowId);
						}
						if (!anyOther)
							ImGui::TextDisabled("No other open windows");
						ImGui::EndMenu();
					}
					ImGui::EndPopup();
				}
				ApplyWindowContentColumnEdit(
					windowState, DrawSceneVisibilityColumns(WindowContentColumnState(windowState)));

				if (open)
				{
					// Grouped by species rather than one flat atom-per-row list - a defect supercell's
					// atom count is unwieldy to scroll flat, but its handful of distinct elements
					// aren't. Recomputed every frame (cheap relative to everything else Render() already
					// walks per window) rather than cached, since atoms can be added/removed/retyped by
					// any command between frames with no dedicated invalidation hook to hang a cache off.
					std::map<std::string, std::vector<std::size_t>> speciesGroups;
					for (std::size_t atomIndex = 0; atomIndex < windowState.structure.atoms.size(); ++atomIndex)
						speciesGroups[windowState.structure.atoms[atomIndex].element].push_back(atomIndex);

					for (const auto &[species, atomIndices] : speciesGroups)
						drawSpeciesGroup(windowState, species, atomIndices);

					drawLabelsGroup(windowState);
					drawOrbitalsGroup(windowState);
					drawPlanesGroup(windowState);
					drawVacanciesGroup(windowState);
					DrawDefectFrameOutlinerRow(windowState);
					drawPathsGroup(windowState);

					ImGui::TreePop();
				}
			}

			ImGui::PopID();
		}

		if (m_PendingCopyJob != nullptr)
			ImGui::TextDisabled("Copying view + visibility...");
		if (!m_CopyError.empty())
			ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", m_CopyError.c_str());

		ImGui::End();
		SetVisible(windowOpen);
		applyKeyboardNavFlag(m_HadKeyboardFocus);
	}

	void SceneOutlinerPanel::applyKeyboardNavFlag(const bool enabled)
	{
		m_HadKeyboardFocus = enabled;
		ImGuiIO &io = ImGui::GetIO();
		if (enabled)
			io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
		else
			io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
	}
} // namespace DefectStudio
