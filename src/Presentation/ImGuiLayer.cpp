#include "Core/dspch.hpp"

#include <algorithm>
#include <array>
#include <functional>

#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <imgui.h>

#include "Presentation/ImGuiLayer.hpp"

#include "App/Managers/ConfigManager.hpp"
#include "App/Events/ApplicationConfigEvents.hpp"
#include "App/Window.hpp"
#include "App/Serialization/YamlCodecFacade.hpp"
#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/Platform/PlatformFontDiscovery.hpp"
#include "Core/Utils/Logger.hpp"
#include "Core/Utils/Path.hpp"
#include "Presentation/EditorUiEvents.hpp"
#include "Presentation/EditorUiState.hpp"

namespace DefectStudio
{

	namespace
	{
		template <typename EventType>
		SubscriptionHandle subscribeImGuiLayer(
			EventBus &bus,
			ImGuiLayer &layer,
			void (ImGuiLayer::*method)(const EventType &))
		{
			return bus.Subscribe<EventType>(std::bind_front(method, &layer));
		}
	}

	namespace ImGuiLayerDetail
	{
		constexpr float FontPixelSize = 16.0f;

		float clamp01(float value)
		{
			return std::clamp(value, 0.0f, 1.0f);
		}

		ImVec4 toImVec4(const std::array<float, 4> &color)
		{
			return ImVec4(color[0], color[1], color[2], color[3]);
		}

		ImVec4 withAlpha(ImVec4 color, float alpha)
		{
			color.w = clamp01(alpha);
			return color;
		}

		ImVec4 mix(const ImVec4 &left, const ImVec4 &right, float amount)
		{
			const float t = clamp01(amount);
			return ImVec4(
				left.x + (right.x - left.x) * t,
				left.y + (right.y - left.y) * t,
				left.z + (right.z - left.z) * t,
				left.w + (right.w - left.w) * t);
		}

		ImVec4 saturate(const ImVec4 &color, float amount)
		{
			const float multiplier = 1.0f + std::max(amount, -0.95f);
			const float grey = (color.x + color.y + color.z) / 3.0f;
			return ImVec4(
				clamp01(grey + (color.x - grey) * multiplier),
				clamp01(grey + (color.y - grey) * multiplier),
				clamp01(grey + (color.z - grey) * multiplier),
				color.w);
		}

		void applyAppearanceToImGui(const AppearanceConfig &appearance)
		{
			if (ImGui::GetCurrentContext() == nullptr)
				return;

			ImGuiStyle &style = ImGui::GetStyle();
			style.WindowRounding = appearance.windowRounding;
			style.ChildRounding = appearance.windowRounding;
			style.PopupRounding = appearance.popupRounding;
			style.FrameRounding = appearance.frameRounding;
			style.GrabRounding = appearance.grabRounding;
			style.ScrollbarRounding = appearance.scrollbarRounding;
			style.TabRounding = appearance.tabRounding;
			style.WindowPadding = ImVec2(appearance.windowPaddingX, appearance.windowPaddingY);
			style.FramePadding = ImVec2(appearance.framePaddingX, appearance.framePaddingY);
			style.ItemSpacing = ImVec2(appearance.itemSpacingX, appearance.itemSpacingY);
			style.DisabledAlpha = appearance.stateRules.disabledAlpha;

			const ImVec4 background = toImVec4(appearance.backgroundColor);
			const ImVec4 surface = toImVec4(appearance.surfaceColor);
			const ImVec4 raised = toImVec4(appearance.raisedSurfaceColor);
			const ImVec4 border = toImVec4(appearance.borderColor);
			const ImVec4 collapsible = toImVec4(appearance.collapsibleColor);
			const ImVec4 text = toImVec4(appearance.textColor);
			const ImVec4 mutedText = toImVec4(appearance.mutedTextColor);
			const ImVec4 accent = toImVec4(appearance.accentColor);
			const ImVec4 white(1.0f, 1.0f, 1.0f, 1.0f);
			const ImVec4 black(0.0f, 0.0f, 0.0f, 1.0f);
			const ImVec4 neutralHover = mix(surface, white, appearance.stateRules.neutralHoverLighten);
			const ImVec4 neutralActive = mix(surface, white, appearance.stateRules.neutralActiveLighten);
			const ImVec4 collapsibleHover = saturate(mix(collapsible, white, appearance.stateRules.accentHoverLighten),
			                                         appearance.stateRules.accentHoverSaturation);
			const ImVec4 collapsibleActive = saturate(mix(collapsible, black, appearance.stateRules.accentActiveDarken),
			                                          appearance.stateRules.accentActiveSaturation);
			const ImVec4 accentHover = saturate(mix(accent, white, appearance.stateRules.accentHoverLighten),
			                                    appearance.stateRules.accentHoverSaturation);
			const ImVec4 accentActive = saturate(mix(accent, black, appearance.stateRules.accentActiveDarken),
			                                     appearance.stateRules.accentActiveSaturation);

			ImVec4 *colors = style.Colors;
			colors[ImGuiCol_Text] = text;
			colors[ImGuiCol_TextDisabled] = mutedText;
			colors[ImGuiCol_WindowBg] = background;
			colors[ImGuiCol_ChildBg] = background;
			colors[ImGuiCol_PopupBg] = surface;
			colors[ImGuiCol_Border] = border;
			colors[ImGuiCol_BorderShadow] = withAlpha(black, 0.0f);
			colors[ImGuiCol_FrameBg] = surface;
			colors[ImGuiCol_FrameBgHovered] = neutralHover;
			colors[ImGuiCol_FrameBgActive] = neutralActive;
			colors[ImGuiCol_TitleBg] = surface;
			colors[ImGuiCol_TitleBgActive] = raised;
			colors[ImGuiCol_TitleBgCollapsed] = background;
			colors[ImGuiCol_MenuBarBg] = surface;
			colors[ImGuiCol_ScrollbarBg] = background;
			colors[ImGuiCol_ScrollbarGrab] = raised;
			colors[ImGuiCol_ScrollbarGrabHovered] = neutralHover;
			colors[ImGuiCol_ScrollbarGrabActive] = neutralActive;
			colors[ImGuiCol_CheckMark] = accent;
			colors[ImGuiCol_SliderGrab] = accent;
			colors[ImGuiCol_SliderGrabActive] = accentActive;
			colors[ImGuiCol_Button] = raised;
			colors[ImGuiCol_ButtonHovered] = accentHover;
			colors[ImGuiCol_ButtonActive] = accentActive;
			colors[ImGuiCol_Header] = withAlpha(collapsible, appearance.stateRules.selectedAlpha);
			colors[ImGuiCol_HeaderHovered] = withAlpha(collapsibleHover, appearance.stateRules.selectedHoverAlpha);
			colors[ImGuiCol_HeaderActive] = withAlpha(collapsibleActive, appearance.stateRules.selectedActiveAlpha);
			colors[ImGuiCol_Separator] = border;
			colors[ImGuiCol_SeparatorHovered] = accentHover;
			colors[ImGuiCol_SeparatorActive] = accentActive;
			colors[ImGuiCol_ResizeGrip] = withAlpha(accent, 0.22f);
			colors[ImGuiCol_ResizeGripHovered] = withAlpha(accentHover, 0.45f);
			colors[ImGuiCol_ResizeGripActive] = withAlpha(accentActive, 0.70f);
			colors[ImGuiCol_Tab] = surface;
			colors[ImGuiCol_TabHovered] = withAlpha(accentHover, 0.48f);
			colors[ImGuiCol_TabActive] = raised;
			colors[ImGuiCol_TabSelectedOverline] = withAlpha(accent, 0.90f);
			colors[ImGuiCol_TabUnfocused] = surface;
			colors[ImGuiCol_TabUnfocusedActive] = raised;
			colors[ImGuiCol_TabDimmed] = withAlpha(surface, 0.96f);
			colors[ImGuiCol_TabDimmedSelected] = withAlpha(raised, 0.96f);
			colors[ImGuiCol_TabDimmedSelectedOverline] = withAlpha(accent, 0.55f);
			colors[ImGuiCol_DockingPreview] = withAlpha(accent, 0.45f);
			colors[ImGuiCol_DockingEmptyBg] = background;
			colors[ImGuiCol_PlotLines] = accent;
			colors[ImGuiCol_PlotLinesHovered] = accentHover;
			colors[ImGuiCol_PlotHistogram] = accent;
			colors[ImGuiCol_PlotHistogramHovered] = accentHover;
			colors[ImGuiCol_TableHeaderBg] = raised;
			colors[ImGuiCol_TableBorderStrong] = border;
			colors[ImGuiCol_TableBorderLight] = mix(border, background, 0.35f);
			colors[ImGuiCol_TableRowBg] = withAlpha(background, 0.0f);
			colors[ImGuiCol_TableRowBgAlt] = withAlpha(raised, 0.18f);
			colors[ImGuiCol_TextSelectedBg] = withAlpha(accent, 0.30f);
			colors[ImGuiCol_DragDropTarget] = accentHover;
			colors[ImGuiCol_NavHighlight] = accent;
			colors[ImGuiCol_NavCursor] = accent;
			colors[ImGuiCol_NavWindowingHighlight] = withAlpha(white, 0.65f);
			colors[ImGuiCol_NavWindowingDimBg] = withAlpha(black, 0.20f);
			colors[ImGuiCol_ModalWindowDimBg] = withAlpha(black, 0.45f);
		}
	} // namespace ImGuiLayerDetail

	ImGuiLayer::ImGuiLayer(ImGuiLayerRuntime runtime)
		: Layer("ImGuiLayer"),
		  m_Window(std::move(runtime.window)),
		  m_Config(std::move(runtime.config)),
		  m_ResetLayoutOnAttach(runtime.resetLayout)
	{
		bindEventBus(std::move(runtime.eventBus));
		bindConfigManager(std::move(runtime.configManager));
	}

	void ImGuiLayer::ApplyUiConfig(const UIConfig &uiConfig)
	{
		m_Config.ui = uiConfig;
		applyUiConfigToContext();
	}

	void ImGuiLayer::bindEventBus(Ref<EventBus> eventBus)
	{
		ClearSubscriptions();
		m_EventBus = std::move(eventBus);

		if (m_EventBus == nullptr)
		{
			DS_LOG_WARN("ImGuiLayer event bus binding skipped: EventBus unavailable");
			return;
		}

		using namespace EditorUiEvents;
		using namespace AppEvents::Config;

		AddSubscription(subscribeImGuiLayer<Applied>(*m_EventBus, *this, &ImGuiLayer::onApplicationConfigApplied));
		AddSubscription(subscribeImGuiLayer<ConfigPreviewRequested>(*m_EventBus, *this, &ImGuiLayer::onUiConfigPreviewRequested));
		AddSubscription(subscribeImGuiLayer<FontListRefreshRequested>(*m_EventBus, *this, &ImGuiLayer::onFontListRefreshRequested));
		AddSubscription(subscribeImGuiLayer<FontReloadRequested>(*m_EventBus, *this, &ImGuiLayer::onFontReloadRequested));
		AddSubscription(subscribeImGuiLayer<FontScaleChanged>(*m_EventBus, *this, &ImGuiLayer::onFontScaleChanged));
		AddSubscription(subscribeImGuiLayer<AppearancePreviewRequested>(*m_EventBus, *this, &ImGuiLayer::onAppearancePreviewRequested));
		AddSubscription(subscribeImGuiLayer<AppearanceApplyRequested>(*m_EventBus, *this, &ImGuiLayer::onAppearanceApplyRequested));
		AddSubscription(subscribeImGuiLayer<ThemeSaveRequested>(*m_EventBus, *this, &ImGuiLayer::onThemeSaveRequested));
		AddSubscription(subscribeImGuiLayer<ThemeSaved>(*m_EventBus, *this, &ImGuiLayer::onThemeSaved));
		AddSubscription(subscribeImGuiLayer<ThemeSaveFailed>(*m_EventBus, *this, &ImGuiLayer::onThemeSaveFailed));
		AddSubscription(subscribeImGuiLayer<ThemeLoaded>(*m_EventBus, *this, &ImGuiLayer::onThemeLoaded));
		AddSubscription(subscribeImGuiLayer<ThemeLoadFailed>(*m_EventBus, *this, &ImGuiLayer::onThemeLoadFailed));
		AddSubscription(subscribeImGuiLayer<LayoutSaveRequested>(*m_EventBus, *this, &ImGuiLayer::onLayoutSaveRequested));
		AddSubscription(subscribeImGuiLayer<LayoutSaved>(*m_EventBus, *this, &ImGuiLayer::onLayoutSaved));
		AddSubscription(subscribeImGuiLayer<LayoutSaveFailed>(*m_EventBus, *this, &ImGuiLayer::onLayoutSaveFailed));
		AddSubscription(subscribeImGuiLayer<LayoutLoaded>(*m_EventBus, *this, &ImGuiLayer::onLayoutLoaded));
		AddSubscription(subscribeImGuiLayer<LayoutLoadFailed>(*m_EventBus, *this, &ImGuiLayer::onLayoutLoadFailed));
		AddSubscription(subscribeImGuiLayer<LayoutResetRequested>(*m_EventBus, *this, &ImGuiLayer::onLayoutResetRequested));
		DS_LOG_INFO("ImGuiLayer UI event handlers bound");
	}

	void ImGuiLayer::bindConfigManager(WeakRef<ConfigManager> configManager)
	{
		m_ConfigManager = std::move(configManager);
		DS_LOG_INFO("ImGuiLayer ConfigManager bound: available={}", !m_ConfigManager.expired());
	}

	void ImGuiLayer::BindUiState(WeakRef<EditorUiState> uiState)
	{
		m_UiState = std::move(uiState);
		DS_LOG_INFO("ImGuiLayer UI state bound: available={}", !m_UiState.expired());
		syncFontsFromSources();
		applySelectedFont();
		auto state = m_UiState.lock();

		if (!state)
			return;

		ImGuiLayerDetail::applyAppearanceToImGui(state->appearance);
		if (m_EventBus != nullptr && !state->layoutPath.empty())
		{
			const Path resolvedPath = Path::FromResolved(state->layoutPath);
			m_EventBus->Queue(EditorUiEvents::LayoutLoadRequested{resolvedPath});
			state->layoutStatusMessage = "Layout load queued: " + state->layoutPath;
			DS_LOG_INFO("ImGui layout load queued on UI state bind: {}", state->layoutPath);
		}
		
	}

	bool ImGuiLayer::IsInitialized() const
	{
		return m_Initialized;
	}

	void ImGuiLayer::BeginFrame()
	{
		if (!m_Initialized)
			return;

		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();
		ImGui::DockSpaceOverViewport(ImGui::GetMainViewport()->ID,
		                             ImGui::GetMainViewport(),
		                             ImGuiDockNodeFlags_PassthruCentralNode);
	}

	void ImGuiLayer::EndFrame()
	{
		if (!m_Initialized)
			return;
	}

	void ImGuiLayer::Render()
	{
		if (!m_Initialized)
			return;

		ImGui::Render();
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
	}

	void ImGuiLayer::OnAttach()
	{
		if (m_Initialized)
			return;

		auto window = m_Window.lock();
		if (window == nullptr || window->GetNativeHandle() == nullptr)
		{
			DS_LOG_ERROR("ImGuiLayer attach failed: window handle is unavailable");
			return;
		}

		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGuiIO &io = ImGui::GetIO();
		io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
		io.ConfigWindowsResizeFromEdges = true;
		io.IniFilename = nullptr;

		const Path layoutPath = resolveLayoutPath();
		m_Config.layout.imGuiIniPath = layoutPath.String();
		resetLayoutIfRequested(layoutPath);
		applyUiConfigToContext();
		ImGui::StyleColorsDark();
		ImGuiLayerDetail::applyAppearanceToImGui(m_Config.appearance);

		const bool glfwBackendInitialized = ImGui_ImplGlfw_InitForOpenGL(window->GetNativeHandle(), true);
		const bool openGlBackendInitialized = ImGui_ImplOpenGL3_Init("#version 330");
		if (!glfwBackendInitialized || !openGlBackendInitialized)
		{
			DS_LOG_ERROR("ImGuiLayer attach failed: backend initialization failed");
			ImGui_ImplOpenGL3_Shutdown();
			ImGui_ImplGlfw_Shutdown();
			ImGui::DestroyContext();
			return;
		}

		m_Initialized = true;
		DS_LOG_INFO("ImGuiLayer attached: context and backends initialized");
	}

	void ImGuiLayer::OnDetach()
	{
		saveLayout();
		ClearSubscriptions();
		shutdownImGui();
		m_UiState.reset();
		m_EventBus.reset();
		m_ConfigManager.reset();
		m_Window.reset();
		DS_LOG_INFO("ImGuiLayer detached");
	}

	void ImGuiLayer::OnUpdate(float)
	{
	}

	void ImGuiLayer::OnImGuiRender()
	{
	}

	Path ImGuiLayer::resolveLayoutPath() const
	{
		if (!m_Config.layout.imGuiIniPath.empty())
			return Path::FromResolved(m_Config.layout.imGuiIniPath);

		if (!m_Config.directory.Empty())
			return ConfigManager::GetLayoutPath(m_Config.directory);

		if (auto configManager = m_ConfigManager.lock())
			return ConfigManager::GetLayoutPath(configManager->GetConfigDirectory());

		return Path::FromResolved(FileSystem::CurrentPath() / "install" / "users" / "default" / "layouts" / ConfigManager::LayoutFileName);
	}

	void ImGuiLayer::resetLayoutIfRequested(const Path &layoutPath)
	{
		if (!m_ResetLayoutOnAttach)
			return;

		if (m_EventBus == nullptr)
		{
			DS_LOG_WARN("Layout reset requested but EventBus is unavailable");
			return;
		}

		m_EventBus->Publish(EditorUiEvents::PersistRequested{EditorUiEvents::PersistKind::Layout, layoutPath, {}});
	}

	void ImGuiLayer::applyUiConfigToContext() const
	{
		if (ImGui::GetCurrentContext() == nullptr)
			return;

		const float fontScale = std::clamp(m_Config.ui.fontScale, m_Config.ui.fontScaleMin, m_Config.ui.fontScaleMax);
		ImGui::GetIO().FontGlobalScale = fontScale;
		DS_LOG_INFO("UI settings loaded: font_scale={}, font_scale_step={}, font_path={}",
		            fontScale,
		            std::clamp(m_Config.ui.fontScaleStep, m_Config.ui.fontScaleStepMin, m_Config.ui.fontScaleStepMax),
		            m_Config.ui.fontPath.empty() ? "<default>" : m_Config.ui.fontPath);
	}

	void ImGuiLayer::saveLayout()
	{
		if (ImGui::GetCurrentContext() == nullptr)
			return;

		Path layoutPath = resolveLayoutPath();
		if (auto uiState = m_UiState.lock())
		{
			if (!uiState->layoutPath.empty())
				layoutPath = Path::FromResolved(uiState->layoutPath);
		}

		saveLayoutToPath(layoutPath, "Layout save on detach failed");
	}

	Path ImGuiLayer::resolveRequestedLayoutPath(const Path &requestedPath) const
	{
		if (!requestedPath.Empty())
			return requestedPath;

		return resolveLayoutPath();
	}

	void ImGuiLayer::saveLayoutToPath(const Path &layoutPath, const char *failurePrefix)
	{
		if (ImGui::GetCurrentContext() == nullptr || layoutPath.Empty())
			return;

		if (m_EventBus == nullptr)
		{
			DS_LOG_WARN("{}: EventBus unavailable", failurePrefix);
			return;
		}

		std::size_t size = 0;
		const char *data = ImGui::SaveIniSettingsToMemory(&size);
		std::string contents = (data != nullptr && size > 0) ? std::string(data, size) : std::string{};
		m_EventBus->Publish(EditorUiEvents::PersistRequested{EditorUiEvents::PersistKind::Layout, layoutPath, std::move(contents)});

		DS_LOG_INFO("ImGui layout persistence requested: {} bytes={}", layoutPath.String(), size);
	}

	void ImGuiLayer::shutdownImGui()
	{
		if (!m_Initialized)
			return;

		ImGui_ImplOpenGL3_Shutdown();
		ImGui_ImplGlfw_Shutdown();
		ImGui::DestroyContext();
		m_Initialized = false;
	}

	void ImGuiLayer::syncFontsFromSources()
	{
		auto uiState = m_UiState.lock();
		if (uiState == nullptr)
			return;

		const Path assetsFontsDirectory = Path::FromResolved(uiState->paths.assetsDirectory.Native() / "fonts");
		auto assetFonts = Platform::GetFontsFromDirectory(assetsFontsDirectory, "Assets");
		auto systemFonts = Platform::GetSystemFonts();
		uiState->fontOptions.clear();
		{
			EditorFontOption option;
			auto defaultFont = Platform::DefaultFontOption();
			option.label = std::move(defaultFont.label);
			option.source = std::move(defaultFont.source);
			option.path = std::move(defaultFont.path);
			uiState->fontOptions.push_back(std::move(option));
		}
		for (const Platform::FontOption &fontOption : assetFonts)
		{
			EditorFontOption option;
			option.label = fontOption.label;
			option.source = fontOption.source;
			option.path = fontOption.path;
			uiState->fontOptions.push_back(std::move(option));
		}
		for (const Platform::FontOption &fontOption : systemFonts)
		{
			EditorFontOption option;
			option.label = fontOption.label;
			option.source = fontOption.source;
			option.path = fontOption.path;
			uiState->fontOptions.push_back(std::move(option));
		}

		std::size_t selectedIndex = 0;
		const std::string selectedPath = uiState->selectedFontPath;
		if (!selectedPath.empty())
		{
			for (std::size_t index = 1; index < uiState->fontOptions.size(); ++index)
			{
				if (Platform::FontPathsEqual(uiState->fontOptions[index].path, selectedPath))
				{
					selectedIndex = index;
					break;
				}
			}
		}

		if (!selectedPath.empty() && selectedIndex == 0)
		{
			uiState->selectedFontPath.clear();
			uiState->fontStatusMessage = "Saved font was not found; using ImGui default.";
			DS_LOG_WARN("Saved UI font was not found: {}", selectedPath);
		}
		else
		{
			uiState->fontStatusMessage = "Font list refreshed: local="
				+ std::to_string(assetFonts.size())
				+ ", system="
				+ std::to_string(systemFonts.size())
				+ ".";
		}

		uiState->selectedFontIndex = selectedIndex;
		DS_LOG_INFO("Font scan complete: local={}, system={}, total={}",
		            assetFonts.size(),
		            systemFonts.size(),
		            uiState->fontOptions.size());
	}

	void ImGuiLayer::applySelectedFont()
	{
		auto uiState = m_UiState.lock();
		if (uiState == nullptr)
			return;

		if (uiState->fontOptions.empty())
			syncFontsFromSources();
		if (uiState->fontOptions.empty())
			return;

		if (!uiState->selectedFontPath.empty())
		{
			uiState->selectedFontIndex = 0;
			for (std::size_t index = 1; index < uiState->fontOptions.size(); ++index)
			{
				if (Platform::FontPathsEqual(uiState->fontOptions[index].path, uiState->selectedFontPath))
				{
					uiState->selectedFontIndex = index;
					break;
				}
			}
		}

		if (uiState->selectedFontIndex >= uiState->fontOptions.size())
			uiState->selectedFontIndex = 0;

		EditorFontOption &selectedFont = uiState->fontOptions[uiState->selectedFontIndex];
		const bool loaded = rebuildImGuiFont(selectedFont);
		if (loaded)
		{
			uiState->selectedFontPath = selectedFont.path;
			uiState->fontStatusMessage = "Active font: " + selectedFont.label;
			return;
		}

		uiState->selectedFontIndex = 0;
		uiState->selectedFontPath.clear();
		uiState->fontStatusMessage = "Font load failed; using ImGui default.";
		DS_LOG_WARN("Falling back to ImGui default font after failed UI font load");
		(void)rebuildImGuiFont(uiState->fontOptions[0]);
	}

	bool ImGuiLayer::rebuildImGuiFont(const EditorFontOption &fontOption)
	{
		if (ImGui::GetCurrentContext() == nullptr)
		{
			DS_LOG_WARN("UI font rebuild requested without an active ImGui context");
			return false;
		}

		ImGuiIO &io = ImGui::GetIO();
		io.Fonts->Clear();

		ImFont *font = nullptr;
		bool loadedRequestedFont = fontOption.path.empty();
		if (fontOption.path.empty())
		{
			font = io.Fonts->AddFontDefault();
		}
		else if (!FileSystem::Exists(fontOption.path))
		{
			DS_LOG_WARN("UI font path does not exist: {}", fontOption.path);
		}
		else
		{
			DS_LOG_INFO("Loading UI font: {} [{}]", fontOption.label, fontOption.path);
			font = io.Fonts->AddFontFromFileTTF(fontOption.path.c_str(), ImGuiLayerDetail::FontPixelSize);
			loadedRequestedFont = font != nullptr;
		}

		if (font == nullptr)
		{
			DS_LOG_ERROR("ImGui failed to load UI font: {} [{}]", fontOption.label, fontOption.path);
			font = io.Fonts->AddFontDefault();
			if (font == nullptr)
				return false;
		}

		io.FontDefault = font;

		ImGui_ImplOpenGL3_DestroyDeviceObjects();
		if (!ImGui_ImplOpenGL3_CreateDeviceObjects())
		{
			DS_LOG_ERROR("OpenGL ImGui device objects could not be recreated after font change");
			return false;
		}

		DS_LOG_INFO("Active UI font: {}{}", fontOption.label, fontOption.path.empty() ? "" : " [" + fontOption.path + "]");
		return loadedRequestedFont;
	}

	void ImGuiLayer::onUiConfigPreviewRequested(const EditorUiEvents::ConfigPreviewRequested &event)
	{
		m_Config.ui = event.ui;
		applyUiConfigToContext();
		DS_LOG_INFO("UI config preview applied: font_scale={}", event.ui.fontScale);
	}

	void ImGuiLayer::onFontListRefreshRequested(const EditorUiEvents::FontListRefreshRequested &)
	{
		DS_LOG_INFO("UI font list refresh requested");
		syncFontsFromSources();
		applySelectedFont();
	}

	void ImGuiLayer::onFontReloadRequested(const EditorUiEvents::FontReloadRequested &)
	{
		DS_LOG_INFO("UI font reload requested");
		applySelectedFont();
	}

	void ImGuiLayer::onFontScaleChanged(const EditorUiEvents::FontScaleChanged &event)
	{
		m_Config.ui.fontScale = event.fontScale;
		if (auto uiState = m_UiState.lock())
			uiState->fontScale = event.fontScale;

		applyUiConfigToContext();
		DS_LOG_INFO("UI font scale changed: {}", event.fontScale);
	}

	void ImGuiLayer::onAppearancePreviewRequested(const EditorUiEvents::AppearancePreviewRequested &event)
	{
		if (auto uiState = m_UiState.lock())
			uiState->appearance = event.appearance;

		ImGuiLayerDetail::applyAppearanceToImGui(event.appearance);
		DS_LOG_INFO("Appearance preview applied");
	}

	void ImGuiLayer::onAppearanceApplyRequested(const EditorUiEvents::AppearanceApplyRequested &event)
	{
		if (auto uiState = m_UiState.lock())
			uiState->appearance = event.appearance;

		m_Config.appearance = event.appearance;
		ImGuiLayerDetail::applyAppearanceToImGui(event.appearance);
		if (m_EventBus != nullptr)
		{
			m_EventBus->Queue(AppEvents::Config::SaveUserRequested{m_Config});
			if (auto uiState = m_UiState.lock())
				uiState->appearanceStatusMessage = "Appearance applied; save queued.";
			DS_LOG_INFO("Appearance applied; config save queued");
			return;
		}

		if (auto uiState = m_UiState.lock())
			uiState->appearanceStatusMessage = "Appearance applied.";
		DS_LOG_INFO("Appearance applied without ConfigManager persistence");
	}

	void ImGuiLayer::onThemeSaveRequested(const EditorUiEvents::ThemeSaveRequested &event)
	{
		auto uiState = m_UiState.lock();
		if (m_EventBus == nullptr)
		{
			if (uiState != nullptr)
				uiState->appearanceStatusMessage = "Theme save failed: EventBus unavailable.";
			return;
		}

		std::string contents;
		std::string error;
		if (!YamlCodecFacade::Default().SerializeAppearanceTheme(event.appearance, contents, error))
		{
			if (uiState != nullptr)
				uiState->appearanceStatusMessage = "Theme save failed: " + error;
			DS_LOG_WARN("Appearance theme serialization failed [{}]: {}", event.path.String(), error);
			return;
		}

		m_EventBus->Queue(EditorUiEvents::PersistRequested{EditorUiEvents::PersistKind::Theme, event.path, std::move(contents)});
		if (uiState != nullptr)
			uiState->appearanceStatusMessage = "Theme save queued: " + event.path.String();
		DS_LOG_INFO("Appearance theme persistence queued: {}", event.path.String());
	}

	void ImGuiLayer::onThemeSaved(const EditorUiEvents::ThemeSaved &event)
	{
		if (auto uiState = m_UiState.lock())
			uiState->appearanceStatusMessage = "Theme saved: " + event.path.String();
		DS_LOG_INFO("Appearance theme save confirmed: {}", event.path.String());
	}

	void ImGuiLayer::onThemeSaveFailed(const EditorUiEvents::ThemeSaveFailed &event)
	{
		if (auto uiState = m_UiState.lock())
			uiState->appearanceStatusMessage = "Theme save failed: " + event.error;
		DS_LOG_WARN("Appearance theme save failed [{}]: {}", event.path.String(), event.error);
	}

	void ImGuiLayer::onThemeLoaded(const EditorUiEvents::ThemeLoaded &event)
	{
		auto uiState = m_UiState.lock();
		std::string error;
		AppearanceConfig appearance = uiState != nullptr ? uiState->appearance : m_Config.appearance;
		if (!YamlCodecFacade::Default().DeserializeAppearanceTheme(event.contents, appearance, error))
		{
			if (uiState != nullptr)
				uiState->appearanceStatusMessage = "Theme load failed: " + error;
			DS_LOG_WARN("Appearance theme decode failed [{}]: {}", event.path.String(), error);
			return;
		}

		m_Config.appearance = appearance;
		ImGuiLayerDetail::applyAppearanceToImGui(appearance);
		if (m_EventBus != nullptr)
			m_EventBus->Queue(AppEvents::Config::SaveUserRequested{m_Config});
		if (uiState != nullptr)
		{
			uiState->appearance = appearance;
			uiState->appearanceStatusMessage = "Theme loaded: " + event.path.String();
		}
		DS_LOG_INFO("Appearance theme loaded from payload: {}", event.path.String());
	}

	void ImGuiLayer::onThemeLoadFailed(const EditorUiEvents::ThemeLoadFailed &event)
	{
		if (auto uiState = m_UiState.lock())
			uiState->appearanceStatusMessage = "Theme load failed: " + event.error;
		DS_LOG_WARN("Appearance theme load failed [{}]: {}", event.path.String(), event.error);
	}

	void ImGuiLayer::onLayoutSaveRequested(const EditorUiEvents::LayoutSaveRequested &event)
	{
		auto uiState = m_UiState.lock();
		if (m_EventBus == nullptr)
		{
			if (uiState != nullptr)
				uiState->layoutStatusMessage = "Layout save failed: EventBus unavailable.";
			return;
		}

		const Path path = resolveRequestedLayoutPath(event.path);
		std::size_t size = 0;
		const char *data = ImGui::SaveIniSettingsToMemory(&size);
		std::string contents = (data != nullptr && size > 0) ? std::string(data, size) : std::string{};
		m_EventBus->Queue(EditorUiEvents::PersistRequested{EditorUiEvents::PersistKind::Layout, path, std::move(contents)});
		if (uiState != nullptr)
			uiState->layoutStatusMessage = "Layout save queued: " + path.String();
		DS_LOG_INFO("Layout persistence queued from request: {} bytes={}", path.String(), size);
	}

	void ImGuiLayer::onLayoutSaved(const EditorUiEvents::LayoutSaved &event)
	{
		if (auto uiState = m_UiState.lock())
			uiState->layoutStatusMessage = "Layout saved: " + event.path.String();
		DS_LOG_INFO("Layout save confirmed: {} bytes={}", event.path.String(), event.bytes);
	}

	void ImGuiLayer::onLayoutSaveFailed(const EditorUiEvents::LayoutSaveFailed &event)
	{
		if (auto uiState = m_UiState.lock())
			uiState->layoutStatusMessage = "Layout save failed: " + event.error;
		DS_LOG_WARN("Layout save failed [{}]: {}", event.path.String(), event.error);
	}

	void ImGuiLayer::onLayoutLoaded(const EditorUiEvents::LayoutLoaded &event)
	{
		auto uiState = m_UiState.lock();
		const Path path = resolveRequestedLayoutPath(event.path);
		if (!event.contents.empty())
		{
			ImGui::LoadIniSettingsFromMemory(event.contents.data(), event.contents.size());
			if (uiState != nullptr)
				uiState->layoutStatusMessage = "Layout loaded: " + path.String();
			DS_LOG_INFO("Layout loaded from payload: {} bytes={}", path.String(), event.contents.size());
		}
	}

	void ImGuiLayer::onLayoutLoadFailed(const EditorUiEvents::LayoutLoadFailed &event)
	{
		if (auto uiState = m_UiState.lock())
			uiState->layoutStatusMessage = "Layout load failed: " + event.error;
		DS_LOG_WARN("Layout load failed [{}]: {}", event.path.String(), event.error);
	}

	void ImGuiLayer::onLayoutResetRequested(const EditorUiEvents::LayoutResetRequested &event)
	{
		ImGui::LoadIniSettingsFromMemory("", 0);
		const Path path = resolveRequestedLayoutPath(event.path);
		if (m_EventBus != nullptr)
			m_EventBus->Queue(EditorUiEvents::PersistRequested{EditorUiEvents::PersistKind::Layout, path, {}});

		if (auto uiState = m_UiState.lock())
			uiState->layoutStatusMessage = "Runtime layout reset.";
		DS_LOG_INFO("Layout reset requested: {}", path.String());
	}

	void ImGuiLayer::onApplicationConfigApplied(const AppEvents::Config::Applied &event)
	{
		const bool fontPathChanged = m_Config.ui.fontPath != event.config.ui.fontPath;
		m_Config = event.config;
		applyUiConfigToContext();

		if (fontPathChanged)
		{
			syncFontsFromSources();
			applySelectedFont();
		}

		ImGuiLayerDetail::applyAppearanceToImGui(m_Config.appearance);
		DS_LOG_INFO(
			"ImGuiLayer consumed application config: persisted={} font_path_changed={} font_scale={} layout={}",
			event.persisted,
			fontPathChanged,
			m_Config.ui.fontScale,
			m_Config.layout.imGuiIniPath.empty() ? "<default>" : m_Config.layout.imGuiIniPath);
	}
} // namespace DefectStudio
