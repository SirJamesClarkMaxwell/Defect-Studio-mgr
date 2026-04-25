#include "Core/dspch.hpp"

#include <algorithm>
#include <cstdio>

#include <imgui.h>

#include "Presentation/Panels/AppearanceEditor.hpp"

namespace DefectStudio
{
	namespace
	{
		void copyToBuffer(std::array<char, 320> &buffer, const std::string &value)
		{
			std::snprintf(buffer.data(), buffer.size(), "%s", value.c_str());
		}

		void sliderRule(const char *label, float &value, float min, float max)
		{
			ImGui::SliderFloat(label, &value, min, max, "%.3f");
		}
	}

	AppearanceEditor::AppearanceEditor(WeakRef<EditorUiState> uiState, std::string title, bool visibleByDefault)
		: IPanel(std::move(title), visibleByDefault),
		  m_UiState(std::move(uiState))
	{
	}

	Ref<IPanel> AppearanceEditor::Clone() const
	{
		return CreateRef<AppearanceEditor>(*this);
	}

	void AppearanceEditor::Render()
	{
		auto uiState = m_UiState.lock();
		if (uiState == nullptr || !IsVisible())
			return;

		initializeBuffers(*uiState);

		bool visible = IsVisible();
		ImGui::SetNextWindowSize(ImVec2(760.0f, 680.0f), ImGuiCond_FirstUseEver);
		if (!ImGui::Begin(GetTitle().c_str(), &visible))
		{
			SetVisible(visible);
			ImGui::End();
			return;
		}
		SetVisible(visible);

		ImGui::TextWrapped("Dark orange theme controls. The panel only edits runtime state; ImGuiLayer applies it and ConfigManager handles files.");
		if (ImGui::Button("Apply style"))
			uiState->appearanceApplyRequested = true;
		ImGui::SameLine();
		if (ImGui::Button("Reset to dark orange"))
		{
			uiState->appearance = AppearanceConfig{};
			uiState->appearanceApplyRequested = true;
		}

		if (!uiState->appearanceStatusMessage.empty())
			ImGui::TextWrapped("%s", uiState->appearanceStatusMessage.c_str());

		ImGui::Separator();
		renderColorSection(uiState->appearance);
		renderMetricsSection(uiState->appearance);
		renderStateRulesSection(uiState->appearance);
		renderFileSection(*uiState);

		ImGui::End();
	}

	void AppearanceEditor::initializeBuffers(const EditorUiState &uiState)
	{
		if (m_BuffersInitialized)
			return;

		const std::string defaultThemePath = !uiState.themeSavePath.empty()
			? uiState.themeSavePath
			: (uiState.paths.themesDirectory / Path("dark-orange.yaml")).String();
		const std::string defaultLayoutPath = !uiState.layoutPath.empty()
			? uiState.layoutPath
			: (uiState.paths.layoutsDirectory / Path("imgui.ini")).String();

		copyToBuffer(m_ThemeSavePath, defaultThemePath);
		copyToBuffer(m_ThemeLoadPath, !uiState.themeLoadPath.empty() ? uiState.themeLoadPath : defaultThemePath);
		copyToBuffer(m_LayoutPath, defaultLayoutPath);
		m_BuffersInitialized = true;
	}

	void AppearanceEditor::syncBuffersToState(EditorUiState &uiState)
	{
		uiState.themeSavePath = m_ThemeSavePath.data();
		uiState.themeLoadPath = m_ThemeLoadPath.data();
		uiState.layoutPath = m_LayoutPath.data();
	}

	void AppearanceEditor::renderColorSection(AppearanceConfig &appearance)
	{
		if (!ImGui::CollapsingHeader("Colors", ImGuiTreeNodeFlags_DefaultOpen))
			return;

		ImGui::ColorEdit4("Background", appearance.backgroundColor.data());
		ImGui::ColorEdit4("Surface", appearance.surfaceColor.data());
		ImGui::ColorEdit4("Raised surface", appearance.raisedSurfaceColor.data());
		ImGui::ColorEdit4("Border", appearance.borderColor.data());
		ImGui::ColorEdit4("Collapsible", appearance.collapsibleColor.data());
		ImGui::ColorEdit4("Text", appearance.textColor.data());
		ImGui::ColorEdit4("Muted text", appearance.mutedTextColor.data());
		ImGui::ColorEdit4("Accent controls", appearance.accentColor.data());
		ImGui::ColorEdit4("Clear color", appearance.clearColor.data());
	}

	void AppearanceEditor::renderMetricsSection(AppearanceConfig &appearance)
	{
		if (!ImGui::CollapsingHeader("Rounding and spacing", ImGuiTreeNodeFlags_DefaultOpen))
			return;

		ImGui::SliderFloat("Window rounding", &appearance.windowRounding, 0.0f, 24.0f, "%.1f");
		ImGui::SliderFloat("Frame rounding", &appearance.frameRounding, 0.0f, 18.0f, "%.1f");
		ImGui::SliderFloat("Grab rounding", &appearance.grabRounding, 0.0f, 18.0f, "%.1f");
		ImGui::SliderFloat("Popup rounding", &appearance.popupRounding, 0.0f, 24.0f, "%.1f");
		ImGui::SliderFloat("Scrollbar rounding", &appearance.scrollbarRounding, 0.0f, 24.0f, "%.1f");
		ImGui::SliderFloat("Tab rounding", &appearance.tabRounding, 0.0f, 18.0f, "%.1f");
		ImGui::SliderFloat("Window padding X", &appearance.windowPaddingX, 0.0f, 32.0f, "%.1f");
		ImGui::SliderFloat("Window padding Y", &appearance.windowPaddingY, 0.0f, 32.0f, "%.1f");
		ImGui::SliderFloat("Frame padding X", &appearance.framePaddingX, 0.0f, 24.0f, "%.1f");
		ImGui::SliderFloat("Frame padding Y", &appearance.framePaddingY, 0.0f, 24.0f, "%.1f");
		ImGui::SliderFloat("Item spacing X", &appearance.itemSpacingX, 0.0f, 24.0f, "%.1f");
		ImGui::SliderFloat("Item spacing Y", &appearance.itemSpacingY, 0.0f, 24.0f, "%.1f");
	}

	void AppearanceEditor::renderStateRulesSection(AppearanceConfig &appearance)
	{
		if (!ImGui::CollapsingHeader("State rules", ImGuiTreeNodeFlags_DefaultOpen))
			return;

		AppearanceStateRules &rules = appearance.stateRules;
		sliderRule("Neutral hover lighten", rules.neutralHoverLighten, 0.0f, 0.35f);
		sliderRule("Neutral active lighten", rules.neutralActiveLighten, 0.0f, 0.35f);
		sliderRule("Accent hover lighten", rules.accentHoverLighten, 0.0f, 0.45f);
		sliderRule("Accent hover saturation", rules.accentHoverSaturation, -0.25f, 0.60f);
		sliderRule("Accent active darken", rules.accentActiveDarken, 0.0f, 0.45f);
		sliderRule("Accent active saturation", rules.accentActiveSaturation, -0.25f, 0.60f);
		sliderRule("Selected alpha", rules.selectedAlpha, 0.0f, 1.0f);
		sliderRule("Selected hover alpha", rules.selectedHoverAlpha, 0.0f, 1.0f);
		sliderRule("Selected active alpha", rules.selectedActiveAlpha, 0.0f, 1.0f);
		sliderRule("Disabled alpha", rules.disabledAlpha, 0.05f, 1.0f);
		sliderRule("Disabled background mix", rules.disabledBackgroundMix, 0.0f, 1.0f);
	}

	void AppearanceEditor::renderFileSection(EditorUiState &uiState)
	{
		if (!ImGui::CollapsingHeader("Theme and layout files", ImGuiTreeNodeFlags_DefaultOpen))
			return;

		ImGui::InputText("Theme save path", m_ThemeSavePath.data(), m_ThemeSavePath.size());
		if (ImGui::Button("Save theme YAML"))
		{
			syncBuffersToState(uiState);
			uiState.themeSaveRequested = true;
		}

		ImGui::InputText("Theme load path", m_ThemeLoadPath.data(), m_ThemeLoadPath.size());
		if (ImGui::Button("Load theme YAML"))
		{
			syncBuffersToState(uiState);
			uiState.themeLoadRequested = true;
		}

		ImGui::Separator();
		ImGui::InputText("Layout .ini path", m_LayoutPath.data(), m_LayoutPath.size());
		if (ImGui::Button("Save ImGui layout"))
		{
			syncBuffersToState(uiState);
			uiState.layoutSaveRequested = true;
		}
		ImGui::SameLine();
		if (ImGui::Button("Load ImGui layout"))
		{
			syncBuffersToState(uiState);
			uiState.layoutLoadRequested = true;
		}
		ImGui::SameLine();
		if (ImGui::Button("Reset runtime layout"))
		{
			syncBuffersToState(uiState);
			uiState.layoutResetRequested = true;
		}

		if (!uiState.layoutStatusMessage.empty())
			ImGui::TextWrapped("%s", uiState.layoutStatusMessage.c_str());
	}
} // namespace DefectStudio
