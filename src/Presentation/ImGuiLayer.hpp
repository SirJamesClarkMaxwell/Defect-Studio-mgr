#pragma once

#include <deque>
#include <string>
#include <vector>

#include "Core/Layer.hpp"
#include "Core/EventSystem/BusEventSystem/EventReceiver.hpp"
#include "Core/Notifications/Notification.hpp"
#include "Core/Utils/Path.hpp"
#include "Core/Utils/Memory.hpp"
#include "Presentation/UiConfig.hpp"

struct GLFWwindow;

namespace DefectStudio
{
	class EventBus;
	struct EditorFontOption;
	struct EditorUiState;
	struct NotificationEvent;

	namespace EditorUiEvents
	{
		struct RuntimeConfigApplied;
		struct AppearanceApplyRequested;
		struct AppearancePreviewRequested;
		struct ConfigPreviewRequested;
		struct FontListRefreshRequested;
		struct FontReloadRequested;
		struct FontScaleChanged;
		struct PersistRequested;
		struct LayoutLoadRequested;
		struct LayoutLoaded;
		struct LayoutLoadFailed;
		struct LayoutResetRequested;
		struct LayoutSaved;
		struct LayoutSaveFailed;
		struct LayoutSaveRequested;
		struct ThemeLoadRequested;
		struct ThemeLoaded;
		struct ThemeLoadFailed;
		struct ThemeSaved;
		struct ThemeSaveFailed;
	}

	struct ImGuiLayerRuntime
	{
		GLFWwindow *nativeWindow = nullptr;
		Ref<EventBus> eventBus;
		UIConfig ui;
		AppearanceConfig appearance;
		Path layoutPath;
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
		void onThemeSaved(const EditorUiEvents::ThemeSaved &event);
		void onThemeSaveFailed(const EditorUiEvents::ThemeSaveFailed &event);
		void onThemeLoaded(const EditorUiEvents::ThemeLoaded &event);
		void onThemeLoadFailed(const EditorUiEvents::ThemeLoadFailed &event);
		void onLayoutSaveRequested(const EditorUiEvents::LayoutSaveRequested &event);
		void onLayoutSaved(const EditorUiEvents::LayoutSaved &event);
		void onLayoutSaveFailed(const EditorUiEvents::LayoutSaveFailed &event);
		void onLayoutLoaded(const EditorUiEvents::LayoutLoaded &event);
		void onLayoutLoadFailed(const EditorUiEvents::LayoutLoadFailed &event);
		void onLayoutResetRequested(const EditorUiEvents::LayoutResetRequested &event);
		void onRuntimeConfigApplied(const EditorUiEvents::RuntimeConfigApplied &event);
		void onNotificationEvent(const NotificationEvent &event);

	private:
		GLFWwindow *m_NativeWindow = nullptr;
		Ref<EventBus> m_EventBus;
		WeakRef<EditorUiState> m_UiState;
		UIConfig m_UiConfig;
		AppearanceConfig m_AppearanceConfig;
		Path m_LayoutPath;
		std::deque<Notification> m_PendingToasts;
		bool m_ResetLayoutOnAttach = false;
		bool m_Initialized = false;
	};
} // namespace DefectStudio
