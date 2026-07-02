#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "Core/Layer.hpp"
#include "Core/EventSystem/BusEventSystem/EventReceiver.hpp"
#include "Core/Utils/Path.hpp"
#include "Core/Utils/Memory.hpp"
#include "Events/RendererEvents.hpp"
#include "Renderer/RendererConfig.hpp"
#include "Renderer/RendererMeshData.hpp"
#include "Renderer/RendererSettings.hpp"
#include "Renderer/RendererTypes.hpp"
#include "Renderer/RendererWindowState.hpp"

namespace DefectStudio
{
	class OpenGlRendererBackend;
	class RendererViewCamera;
	class EventBus;

	namespace RendererEvents::Config
	{
		struct Applied;
	}

	struct RendererStartupConfig
	{
		Path assetsDirectory;
		Path shaderDirectory;
		std::vector<RendererWindowState> startupWindows;
		RendererPrimitiveMeshAssets primitiveMeshes;
		std::vector<std::string> periodicTableSymbols;
		std::vector<std::string> lanthanideSymbols;
		std::vector<std::string> actinideSymbols;
		bool loadDefaultScene = true;
	};

	class RendererLayer final : public Layer, public EventReceiver
	{
	public:
		explicit RendererLayer(RendererStartupConfig startupConfig);
		~RendererLayer() override;

		void OnAttach() override;
		void OnDetach() override;
		void OnUpdate(float deltaTime) override;
		void OnImGuiRender() override;
		void ApplyConfig(const RendererConfig &config);
		void BindEventBus(Ref<EventBus> eventBus);
		[[nodiscard]] Ref<EventBus> GetEventBus() const;
		void BeginViewInteraction(const std::string &windowId, std::string sourceAction);
		void CommitViewInteraction(const std::string &windowId);
		void CancelViewInteraction(const std::string &windowId);
		void StartCameraTransition(
			const std::string &windowId,
			const glm::vec3 &target,
			float distance,
			float yaw,
			float pitch,
			float roll,
			const char *sourceAction = nullptr);
		void UpdateCameraTransitions(float deltaTime);
		void UndoViewChange(const std::string &windowId);
		void RedoViewChange(const std::string &windowId);
		void SetViewportSize(const std::string &windowId, glm::vec2 size);
		[[nodiscard]] std::vector<RendererWindowState> &GetWindows();
		[[nodiscard]] const std::vector<RendererWindowState> &GetWindows() const;
		[[nodiscard]] RendererGlobalRenderSettings &GetGlobalSettings();
		[[nodiscard]] const RendererGlobalRenderSettings &GetGlobalSettings() const;
		[[nodiscard]] bool IsAttached() const noexcept;
		[[nodiscard]] float GetLastDeltaTime() const noexcept;
		[[nodiscard]] const RendererToolbarIconTexture *GetToolbarIcon(const std::string &fileName) const;
		[[nodiscard]] const std::vector<std::string> &GetPeriodicTableSymbols() const;
		[[nodiscard]] const std::vector<std::string> &GetLanthanideSymbols() const;
		[[nodiscard]] const std::vector<std::string> &GetActinideSymbols() const;
		[[nodiscard]] unsigned int RenderToFbo(
			const std::string &windowKey,
			const RendererStructureData &structure,
			const RendererWindowState &windowState,
			const RendererGlobalRenderSettings &settings);
		void CollectProfilingData();
		bool &GetShowPeriodicTableWindow();
		std::string &GetSelectedPeriodicElement();

	private:
		void loadDefaultWindows();
		void bindConfigEvents();
		void onConfigApplied(const RendererEvents::Config::Applied &event);
		void onOrbitDelta(const RendererEvents::Viewport::OrbitDelta &event);
		void onPanDelta(const RendererEvents::Viewport::PanDelta &event);
		void onZoomDelta(const RendererEvents::Viewport::ZoomDelta &event);
		void onViewportFocusChanged(const RendererEvents::Viewport::FocusChanged &event);
		void onAlignToAxisRequested(const RendererEvents::Viewport::AlignToAxisRequested &event);
		void onOrbitDirectionRequested(const RendererEvents::Viewport::OrbitDirectionRequested &event);
		void onRollDirectionRequested(const RendererEvents::Viewport::RollDirectionRequested &event);
		void onZoomDirectionRequested(const RendererEvents::Viewport::ZoomDirectionRequested &event);
		void onOrbitStepRequested(const RendererEvents::Viewport::OrbitStepRequested &event);
		void onRollStepRequested(const RendererEvents::Viewport::RollStepRequested &event);
		void onZoomStepRequested(const RendererEvents::Viewport::ZoomStepRequested &event);
		void onFocusSelectedAtomRequested(const RendererEvents::Viewport::FocusSelectedAtomRequested &event);
		void onUndoViewRequested(const RendererEvents::Viewport::UndoViewRequested &event);
		void onRedoViewRequested(const RendererEvents::Viewport::RedoViewRequested &event);
		void onViewTransitionRequested(const RendererEvents::Viewport::ViewTransitionRequested &event);
		void onProjectionToggleRequested(const RendererEvents::Viewport::ProjectionToggleRequested &event);
		void onAtomSelectionRequested(const RendererEvents::Viewport::AtomSelectionRequested &event);
		[[nodiscard]] RendererWindowState *findWindowById(const std::string &windowId);
		[[nodiscard]] RendererWindowState *findViewportCommandWindow(const std::string &windowId);
		[[nodiscard]] RendererViewSnapshot captureViewSnapshot(const RendererWindowState &windowState) const;
		void restoreViewSnapshot(
			RendererWindowState &windowState,
			const RendererViewSnapshot &snapshot,
			const char *sourceAction);
		void pushViewChange(
			RendererWindowState &windowState,
			const RendererViewSnapshot &before,
			const RendererViewSnapshot &after,
			const char *sourceAction);
		void applyDefaultProjectionToWindows();
		const RendererToolbarIconTexture *getToolbarIcon(const std::string &iconFileName) const;
		void releaseToolbarIcons();
		Path resolveShaderDirectory() const;

	private:
		RendererStartupConfig m_StartupConfig;
		Ref<EventBus> m_EventBus;
		Unique<OpenGlRendererBackend> m_RendererBackend;
		std::vector<RendererWindowState> m_Windows;
		std::vector<std::string> m_PeriodicTableSymbols;
		std::vector<std::string> m_LanthanideSymbols;
		std::vector<std::string> m_ActinideSymbols;
		RendererGlobalRenderSettings m_GlobalRenderSettings;
		mutable std::unordered_map<std::string, RendererToolbarIconTexture> m_ToolbarIcons;
		std::string m_SelectedPeriodicElement = "C";
		std::string m_FocusedViewportWindowId;
		bool m_ShowPeriodicTableWindow = true;
		float m_LastDeltaTime = 0.0f;
		bool m_Attached = false;
	};
} // namespace DefectStudio
