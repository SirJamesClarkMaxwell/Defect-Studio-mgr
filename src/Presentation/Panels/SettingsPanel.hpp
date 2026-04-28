#pragma once

#include <array>
#include <string>

#include "App/Events/ApplicationConfigEvents.hpp"
#include "App/ApplicationState.hpp"
#include "Core/EventSystem/BusEventSystem/EventReceiver.hpp"
#include "Core/Utils/Memory.hpp"
#include "Presentation/EditorUiState.hpp"
#include "Presentation/Panels/IPanel.hpp"
#include "Presentation/Panels/SettingsProfileManager.hpp"

namespace DefectStudio
{
    class EventBus;
    class JobSystem;

    class SettingsPanel final : public IPanel, public EventReceiver
    {
    public:
        explicit SettingsPanel(Ref<EventBus> eventBus = {},
                          WeakRef<JobSystem> jobSystem = {},
                          WeakRef<EditorUiState> uiState = {},
                          std::string title = "SettingsPanel",
                          bool visibleByDefault = true);
        SettingsPanel(const SettingsPanel &other);

        void Render() override;
        [[nodiscard]] Ref<IPanel> Clone() const override;

        [[nodiscard]] bool IsUrgentWorkerReserved() const;
        [[nodiscard]] float GetFontScaleStep() const;
        [[nodiscard]] float GetFontScale() const;

        void SetFontScale(float fontScale);
        void SetFontScaleStep(float step);

    private:
        void ensureDraftInitialized();
        void syncDraftFromApplication();
        void syncDraftFromBuffers();
        [[nodiscard]] bool applyDraft(bool persist);
        [[nodiscard]] bool saveDraftAsDefaults();
        void applyRuntimePreview();
        void maybeApplyPreview();
        void previewAppearanceIfEnabled();
        void bindConfigEvents();
        void onConfigApplied(const AppEvents::Config::Applied &event);
        void onConfigApplyFailed(const AppEvents::Config::ApplyFailed &event);
        void onUserConfigSaved(const AppEvents::Config::UserSaved &event);
        void onUserConfigSaveFailed(const AppEvents::Config::UserSaveFailed &event);
        void onDefaultsSaved(const AppEvents::Config::DefaultsSaved &event);
        void onDefaultsSaveFailed(const AppEvents::Config::DefaultsSaveFailed &event);

        void renderActionBar();
        void renderSidebar();
        void renderSystemTab();
        void renderDisplayTab();
        void renderProfilesTab();
        void renderLayoutTab();
        void renderViewportTab();
        void renderFilePathsTab();

        void renderAppearanceColors();
        void renderAppearanceMetrics();
        void renderAppearanceStateRules();
        void renderAppearanceFiles();

    private:
        bool m_DraftInitialized = false;
        bool m_DraftDirty = false;
        enum class Tab : int
        {
            System = 0,
            Interface,
            Profiles,
            Layout,
            Viewport,
            Editing,
            Animation,
            Input,
            FilePaths,
            Count
        };

        Tab m_SelectedTab = Tab::System;
        ApplicationConfig m_DraftConfig;
        std::array<char, 256> m_WindowTitleBuffer = {};
        std::array<char, 320> m_LogFilePathBuffer = {};
        std::array<char, 320> m_FontPathBuffer = {};
        std::array<char, 320> m_ThemeSavePathBuffer = {};
        std::array<char, 320> m_ThemeLoadPathBuffer = {};
        std::array<char, 320> m_LayoutPathBuffer = {};
        std::string m_StatusMessage;
        Ref<EventBus> m_EventBus;
        WeakRef<JobSystem> m_JobSystem;
        WeakRef<EditorUiState> m_UiState;
        SettingsProfileManager m_ProfileManager;
    };
} // namespace DefectStudio
