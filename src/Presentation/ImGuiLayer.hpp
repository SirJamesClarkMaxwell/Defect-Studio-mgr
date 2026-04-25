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
	struct UiAppearanceApplyRequestedEvent;
	struct UiAppearancePreviewRequestedEvent;
	struct UiConfigPreviewRequestedEvent;
	struct UiFontListRefreshRequestedEvent;
	struct UiFontReloadRequestedEvent;
	struct UiFontScaleChangedEvent;
	struct UiLayoutLoadRequestedEvent;
	struct UiLayoutResetRequestedEvent;
	struct UiLayoutSaveRequestedEvent;
	struct UiThemeLoadRequestedEvent;
	struct UiThemeSaveRequestedEvent;

	struct ImGuiLayerRuntime
	{
		WeakRef<Window> window;
		WeakRef<EventBus> eventBus;
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
		void bindEventBus(WeakRef<EventBus> eventBus);
		void bindConfigManager(WeakRef<ConfigManager> configManager);
		void saveLayout();
		void shutdownImGui();
		void syncFontsFromSources();
		void applySelectedFont();
		bool rebuildImGuiFont(const EditorFontOption &fontOption);
		[[nodiscard]] Path resolveRequestedLayoutPath(const Path &requestedPath) const;
		void saveLayoutToPath(const Path &layoutPath, const char *failurePrefix);
		void onUiConfigPreviewRequested(const UiConfigPreviewRequestedEvent &event);
		void onFontListRefreshRequested(const UiFontListRefreshRequestedEvent &event);
		void onFontReloadRequested(const UiFontReloadRequestedEvent &event);
		void onFontScaleChanged(const UiFontScaleChangedEvent &event);
		void onAppearancePreviewRequested(const UiAppearancePreviewRequestedEvent &event);
		void onAppearanceApplyRequested(const UiAppearanceApplyRequestedEvent &event);
		void onThemeSaveRequested(const UiThemeSaveRequestedEvent &event);
		void onThemeLoadRequested(const UiThemeLoadRequestedEvent &event);
		void onLayoutSaveRequested(const UiLayoutSaveRequestedEvent &event);
		void onLayoutLoadRequested(const UiLayoutLoadRequestedEvent &event);
		void onLayoutResetRequested(const UiLayoutResetRequestedEvent &event);

	private:
		WeakRef<Window> m_Window;
		WeakRef<EventBus> m_EventBus;
		WeakRef<EditorUiState> m_UiState;
		WeakRef<ConfigManager> m_ConfigManager;
		ApplicationConfig m_Config;
		bool m_ResetLayoutOnAttach = false;
		bool m_Initialized = false;
	};
} // namespace DefectStudio
