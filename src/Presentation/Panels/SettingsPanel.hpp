#pragma once

#include <array>
#include <optional>
#include <string>
#include <vector>

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
    class KeymapResolver;
    class CommandRegistry;

    namespace EditorUiEvents
    {
        struct LayoutListLoaded;
        struct LayoutListFailed;
    }

    class SettingsPanel final : public IPanel, public EventReceiver
    {
    public:
        explicit SettingsPanel(Ref<EventBus> eventBus = {},
                          WeakRef<JobSystem> jobSystem = {},
                          WeakRef<EditorUiState> uiState = {},
                          WeakRef<KeymapResolver> keymapResolver = {},
                          WeakRef<CommandRegistry> commandRegistry = {},
                          ApplicationConfig initialConfig = {},
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
        void syncDraftFromCurrentConfig();
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
        void onLayoutListLoaded(const EditorUiEvents::LayoutListLoaded &event);
        void onLayoutListFailed(const EditorUiEvents::LayoutListFailed &event);

        void renderActionBar();
        void renderSidebar();
        void renderSidebarVisibilityToggle();
        void renderSystemTab();
        void renderDisplayTab();
        void renderProfilesTab();
        void renderLayoutTab();
        void renderViewportTab();
        void renderRendererTab();
        void renderFilePathsTab();
        void renderInputTab();
        void renderKeyBindingsTab();

        void renderAppearanceColors();
        void renderAppearanceMetrics();
        void renderAppearanceStateRules();
        void renderAppearanceFiles();
        void refreshDraftBuffers();
        void captureHistoryAfterRender(const ApplicationConfig &frameStartConfig);
        void pushUndoSnapshot(const ApplicationConfig &snapshot);
        void clearRedoHistory();
        void performUndo();
        void performRedo();
        [[nodiscard]] bool configsEquivalent(const ApplicationConfig &left, const ApplicationConfig &right) const;
        [[nodiscard]] std::string configFingerprint(const ApplicationConfig &config) const;

    private:
        bool m_DraftInitialized = false;
        bool m_DraftDirty = false;
        bool m_LayoutListRequested = false;
        bool m_ShowSidebar = true;
        bool m_IsApplyingHistory = false;
        bool m_BlockHistoryCaptureForFrame = false;
        enum class Tab : int
        {
            System = 0,
            Interface,
            Profiles,
            Layout,
            Viewport,
            Renderer,
            Editing,
            Animation,
            Input,
            KeyBindings,
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
        std::array<char, 128> m_KeyBindingSearchBuffer = {};
        std::vector<ApplicationConfig> m_UndoHistory;
        std::vector<ApplicationConfig> m_RedoHistory;
        std::optional<ApplicationConfig> m_PendingUndoSnapshot;
        std::string m_StatusMessage;
        Ref<EventBus> m_EventBus;
        WeakRef<JobSystem> m_JobSystem;
        WeakRef<EditorUiState> m_UiState;
        WeakRef<KeymapResolver> m_KeymapResolver;
        WeakRef<CommandRegistry> m_CommandRegistry;
        SettingsProfileManager m_ProfileManager;
    };
} // namespace DefectStudio
