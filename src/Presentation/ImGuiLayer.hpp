#pragma once

#include <string>
#include <vector>

#include "App/ApplicationState.hpp"
#include "Core/Layer.hpp"
#include "Core/Utils/Memory.hpp"

namespace DefectStudio
{
	class ConfigManager;
	struct EditorFontOption;
	struct EditorUiState;

	class ImGuiLayer final : public Layer
	{
	public:
		ImGuiLayer();
		static void ApplyAppearanceToCurrentContext(const AppearanceConfig &appearance);
		void BindConfigManager(WeakRef<ConfigManager> configManager);
		void BindUiState(WeakRef<EditorUiState> uiState);

		void OnAttach() override;
		void OnDetach() override;
		void OnUpdate(float deltaTime) override;
		void OnImGuiRender() override;

	private:
		void syncFontsFromSources();
		void applySelectedFont();
		bool rebuildImGuiFont(const EditorFontOption &fontOption);
		void handleUiRequests();

	private:
		WeakRef<EditorUiState> m_UiState;
		WeakRef<ConfigManager> m_ConfigManager;
	};
} // namespace DefectStudio
