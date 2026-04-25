#pragma once

#include <string>
#include <vector>

#include "App/ApplicationState.hpp"
#include "Core/Layer.hpp"
#include "Core/Utils/Memory.hpp"

namespace DefectStudio
{
	class ConfigManager;
	class Window;
	struct EditorFontOption;
	struct EditorUiState;

	class ImGuiLayer final : public Layer
	{
	public:
		ImGuiLayer();
		ImGuiLayer(WeakRef<Window> window, WeakRef<ConfigManager> configManager, ApplicationConfig config, bool resetLayout);
		static void ApplyAppearanceToCurrentContext(const AppearanceConfig &appearance);
		void ApplyUiConfig(const UIConfig &uiConfig);
		void BindRuntime(WeakRef<Window> window, WeakRef<ConfigManager> configManager, ApplicationConfig config, bool resetLayout);
		void BindConfigManager(WeakRef<ConfigManager> configManager);
		void BindUiState(WeakRef<EditorUiState> uiState);
		[[nodiscard]] bool IsInitialized() const;

		void BeginFrame();
		void RenderDrawData();

		void OnAttach() override;
		void OnDetach() override;
		void OnUpdate(float deltaTime) override;
		void OnImGuiRender() override;

	private:
		[[nodiscard]] Path resolveLayoutPath() const;
		void resetLayoutIfRequested(const Path &layoutPath);
		void applyUiConfigToContext() const;
		void saveLayout();
		void shutdownImGui();
		void syncFontsFromSources();
		void applySelectedFont();
		bool rebuildImGuiFont(const EditorFontOption &fontOption);
		void handleUiRequests();

	private:
		WeakRef<Window> m_Window;
		WeakRef<EditorUiState> m_UiState;
		WeakRef<ConfigManager> m_ConfigManager;
		ApplicationConfig m_Config;
		bool m_ResetLayoutOnAttach = false;
		bool m_Initialized = false;
	};
} // namespace DefectStudio
