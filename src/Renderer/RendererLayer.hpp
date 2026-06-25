#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "Core/Layer.hpp"
#include "Core/EventSystem/BusEventSystem/EventReceiver.hpp"
#include "Core/Utils/Path.hpp"
#include "Core/Utils/Memory.hpp"
#include "Domain/Crystal/ElementProperties.hpp"
#include "Renderer/RendererMeshData.hpp"
#include "Renderer/AtomStyleTable.hpp"
#include "Renderer/RendererStartupDefinitions.hpp"
#include "Renderer/RendererSettings.hpp"
#include "Renderer/RendererTypes.hpp"
#include "Renderer/RendererWindowState.hpp"

namespace DefectStudio
{
	class OpenGlRendererBackend;
	class RendererViewCamera;
	class RendererPanel;
	class EventBus;
	struct ApplicationConfig;

	namespace AppEvents::Config
	{
		struct Applied;
	}

	struct RendererStartupConfig
	{
		Path configDirectory;
		Path assetsDirectory;
		Path shaderDirectory;
		AtomStyleTable atomStyleTable;
		ElementPropertiesTable elementPropertiesTable;
		RendererStartupLayoutDefinition startupLayout;
		RendererPrimitiveMeshAssets primitiveMeshes;
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
		void ApplyConfig(const ApplicationConfig &config);
		void BindEventBus(Ref<EventBus> eventBus);

	private:
		void loadDefaultWindows();
		void bindConfigEvents();
		void onConfigApplied(const AppEvents::Config::Applied &event);
		void applyDefaultProjectionToWindows();
		const RendererToolbarIconTexture *getToolbarIcon(const std::string &iconFileName);
		void releaseToolbarIcons();
		Path resolveShaderDirectory() const;

	private:
		RendererStartupConfig m_StartupConfig;
		Ref<EventBus> m_EventBus;
		Unique<OpenGlRendererBackend> m_RendererBackend;
		std::vector<RendererWindowState> m_Windows;
		RendererGlobalRenderSettings m_GlobalRenderSettings;
		Unique<RendererPanel> m_Panel;
		std::unordered_map<std::string, RendererToolbarIconTexture> m_ToolbarIcons;
		std::string m_SelectedPeriodicElement = "C";
		bool m_ShowPeriodicTableWindow = true;
		float m_LastDeltaTime = 0.0f;
		bool m_Attached = false;

		friend class RendererPanel;
	};
} // namespace DefectStudio
