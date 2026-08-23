#include "Core/dspch.hpp"

#include "Presentation/Panels/SceneOutlinerPanel.hpp"

#include <cstdio>

#include <imgui.h>

#include "Renderer/Scene/SceneComponents.hpp"
#include "Renderer/Scene/SceneSystem.hpp"

namespace DefectStudio
{
	namespace
	{
		[[nodiscard]] bool AnyContentHidden(const RendererWindowState &windowState)
		{
			for (const RendererAtomData &atom : windowState.structure.atoms)
				if (!atom.visible)
					return true;
			for (const RendererBondData &bond : windowState.structure.bonds)
				if (!bond.visible)
					return true;
			return false;
		}

		// Same mechanism as H (hide selected)/Alt+H (show all) - see ViewModifier.cpp - just applied
		// to every entity in the window instead of only the selected ones.
		void SetWindowContentVisible(RendererWindowState &windowState, bool visible)
		{
			entt::registry &registry = windowState.sceneRegistry.Registry();
			for (const entt::entity entity : registry.view<VisibilityComponent>())
				registry.get<VisibilityComponent>(entity).visible = visible;
			SceneSystem::PushSelectionAndVisibilityToWindowState(windowState.sceneRegistry, windowState);
		}
	} // namespace

	SceneOutlinerPanel::SceneOutlinerPanel(RendererLayer &layer, std::string title, bool visibleByDefault)
		: IPanel(std::move(title), visibleByDefault), m_Layer(layer)
	{
	}

	Ref<IPanel> SceneOutlinerPanel::Clone() const
	{
		return CreateRef<SceneOutlinerPanel>(*this);
	}

	void SceneOutlinerPanel::Render()
	{
		if (!IsVisible())
			return;

		bool windowOpen = true;
		if (!ImGui::Begin(GetTitle().c_str(), &windowOpen))
		{
			ImGui::End();
			SetVisible(windowOpen);
			return;
		}

		std::vector<RendererWindowState> &windows = m_Layer.GetWindows();
		if (m_EditingWindowIndex >= static_cast<int>(windows.size()))
			m_EditingWindowIndex = -1;
		if (m_ActiveWindowIndex >= static_cast<int>(windows.size()))
			m_ActiveWindowIndex = -1;

		if (windows.empty())
			ImGui::TextDisabled("No open structures.");

		for (int i = 0; i < static_cast<int>(windows.size()); ++i)
		{
			RendererWindowState &windowState = windows[static_cast<std::size_t>(i)];
			ImGui::PushID(i);

			bool visible = !AnyContentHidden(windowState);
			if (ImGui::Checkbox("##visible", &visible))
				SetWindowContentVisible(windowState, visible);
			ImGui::SameLine();

			if (m_EditingWindowIndex == i)
			{
				if (m_JustStartedEditing)
				{
					ImGui::SetKeyboardFocusHere();
					m_JustStartedEditing = false;
				}
				ImGui::SetNextItemWidth(-1.0f);
				const bool committed = ImGui::InputText(
					"##rename", m_EditingBuffer.data(), m_EditingBuffer.size(),
					ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
				if (committed || ImGui::IsItemDeactivated())
				{
					if (m_EditingBuffer[0] != '\0')
						windowState.title = m_EditingBuffer.data();
					m_EditingWindowIndex = -1;
				}
			}
			else
			{
				if (ImGui::Selectable(windowState.title.c_str(), m_ActiveWindowIndex == i))
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
			}

			ImGui::PopID();
		}

		ImGui::End();
		SetVisible(windowOpen);
	}
} // namespace DefectStudio
