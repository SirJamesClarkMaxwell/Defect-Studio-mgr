#pragma once

#include <string>
#include <vector>

#include "App/ApplicationState.hpp"
#include "Core/Layer.hpp"
#include "Core/EventSystem/BusEventSystem/EventReceiver.hpp"
#include "Core/Utils/Memory.hpp"

namespace DefectStudio
{
	class ConfigManager;
	class EventBus;
	class Window;
	struct EditorFontOption;
	struct EditorUiState;

	namespace EditorUiEvents
	{
		struct AppearanceApplyRequested;
		struct AppearancePreviewRequested;
		struct ConfigPreviewRequested;
		struct FontListRefreshRequested;
		struct FontReloadRequested;
		struct FontScaleChanged;
		struct LayoutLoadRequested;
		struct LayoutResetRequested;
		struct LayoutSaveRequested;
		struct ThemeLoadRequested;
		struct ThemeSaveRequested;
	}

	struct ImGuiLayerRuntime
	{
		WeakRef<Window> window;
		Ref<EventBus> eventBus;
		WeakRef<ConfigManager> configManager;
		ApplicationConfig config;
		bool resetLayout = false;
	};

	class ImGuiLayer final : public Layer, public EventReceiver
	{
	public:
		explicit ImGuiLayer(ImGuiLayerRuntime runtime);
		void ApplyUiConfig(const UIConfig &uiConfig);
		void BindUiState(WeakRef<EditorUiState> uiState);
		[[nodiscard]] bool IsInitialized() const;

		void BeginFrame();
		void EndFrame();
		void Render();

		void OnAttach() override;
		void OnDetach() override;
		void OnUpdate(float deltaTime) override;
		void OnImGuiRender() override;

	private:
		[[nodiscard]] Path resolveLayoutPath() const;
		void resetLayoutIfRequested(const Path &layoutPath);
		void applyUiConfigToContext() const;
		void bindEventBus(Ref<EventBus> eventBus);
		void bindConfigManager(WeakRef<ConfigManager> configManager);
		void saveLayout();
		void shutdownImGui();
		void syncFontsFromSources();
		void applySelectedFont();
		bool rebuildImGuiFont(const EditorFontOption &fontOption);
		[[nodiscard]] Path resolveRequestedLayoutPath(const Path &requestedPath) const;
		void saveLayoutToPath(const Path &layoutPath, const char *failurePrefix);
		void onUiConfigPreviewRequested(const EditorUiEvents::ConfigPreviewRequested &event);
		void onFontListRefreshRequested(const EditorUiEvents::FontListRefreshRequested &event);
		void onFontReloadRequested(const EditorUiEvents::FontReloadRequested &event);
		void onFontScaleChanged(const EditorUiEvents::FontScaleChanged &event);
		void onAppearancePreviewRequested(const EditorUiEvents::AppearancePreviewRequested &event);
		void onAppearanceApplyRequested(const EditorUiEvents::AppearanceApplyRequested &event);
		void onThemeSaveRequested(const EditorUiEvents::ThemeSaveRequested &event);
		void onThemeLoadRequested(const EditorUiEvents::ThemeLoadRequested &event);
		void onLayoutSaveRequested(const EditorUiEvents::LayoutSaveRequested &event);
		void onLayoutLoadRequested(const EditorUiEvents::LayoutLoadRequested &event);
		void onLayoutResetRequested(const EditorUiEvents::LayoutResetRequested &event);

	private:
		WeakRef<Window> m_Window;
		Ref<EventBus> m_EventBus;
		WeakRef<EditorUiState> m_UiState;
		WeakRef<ConfigManager> m_ConfigManager;
		ApplicationConfig m_Config;
		bool m_ResetLayoutOnAttach = false;
		bool m_Initialized = false;
	};
} // namespace DefectStudio
