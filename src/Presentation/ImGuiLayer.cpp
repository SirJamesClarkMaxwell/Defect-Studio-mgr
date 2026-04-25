#include "Core/dspch.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <functional>
#include <set>
#include <string_view>

#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <imgui.h>

#include "Presentation/ImGuiLayer.hpp"

#include "App/ConfigManager.hpp"
#include "App/Events/ApplicationConfigEvents.hpp"
#include "App/Window.hpp"
#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/Platform/PlatformPaths.hpp"
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

		Path assetsFontsDirectory(const EditorUiState &uiState)
		{
			if (!uiState.paths.fontsDirectory.Empty())
				return uiState.paths.fontsDirectory;

			const Path portableFonts = Path::FromResolved(FileSystem::CurrentPath() / "install" / "app" / "assets" / "fonts");
			if (FileSystem::Exists(portableFonts.Native()))
				return portableFonts;

			return Path::FromResolved(FileSystem::CurrentPath() / "assets" / "fonts");
		}

		std::string toLowerCopy(std::string value)
		{
			for (char &character : value)
				character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
			return value;
		}

		bool isSupportedFontFile(const std::filesystem::path &path)
		{
			const std::string extension = toLowerCopy(path.extension().string());
			return extension == ".ttf" || extension == ".otf";
		}

		std::string normalizePathForCompare(const std::filesystem::path &path)
		{
			std::error_code error;
			std::filesystem::path normalized = FileSystem::WeaklyCanonical(path, error);
			if (error)
			{
				error.clear();
				normalized = FileSystem::Absolute(path, error);
			}
			if (error)
				normalized = path;

			std::string result = normalized.lexically_normal().string();
			result = toLowerCopy(std::move(result));
			return result;
		}

		std::string fontLabelFromPath(const std::filesystem::path &fontPath, std::string_view source)
		{
			const std::string stem = fontPath.stem().string();
			const std::string filename = fontPath.filename().string();
			return std::string(source) + ": " + (!stem.empty() ? stem : filename);
		}

		void collectFontsFromDirectory(std::vector<EditorFontOption> &fontOptions,
		                               std::set<std::string> &seenPaths,
		                               const std::filesystem::path &directory,
		                               std::string_view source,
		                               std::size_t &addedCount)
		{
			std::error_code error;
			if (!std::filesystem::exists(directory, error) || error)
				return;

			std::filesystem::recursive_directory_iterator iterator(
				directory,
				std::filesystem::directory_options::skip_permission_denied,
				error);
			const std::filesystem::recursive_directory_iterator end;
			if (error)
			{
				DS_LOG_WARN("Font scan skipped [{}]: {}", directory.string(), error.message());
				return;
			}

			for (; iterator != end; iterator.increment(error))
			{
				if (error)
				{
					DS_LOG_WARN("Font scan warning [{}]: {}", directory.string(), error.message());
					error.clear();
					continue;
				}

				const std::filesystem::directory_entry &entry = *iterator;
				if (!entry.is_regular_file(error) || error)
				{
					error.clear();
					continue;
				}

				const std::filesystem::path fontPath = entry.path();
				if (!isSupportedFontFile(fontPath))
					continue;

				const std::string normalizedPath = normalizePathForCompare(fontPath);
				if (!seenPaths.insert(normalizedPath).second)
					continue;

				EditorFontOption option;
				option.label = fontLabelFromPath(fontPath, source);
				option.source = std::string(source);
				option.path = FileSystem::Absolute(fontPath, error).lexically_normal().string();
				if (error)
				{
					error.clear();
					option.path = fontPath.lexically_normal().string();
				}

				fontOptions.push_back(std::move(option));
				++addedCount;
			}
		}

		EditorFontOption defaultFontOption()
		{
			EditorFontOption option;
			option.label = "Default: ImGui";
			option.source = "Default";
			return option;
		}

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
		AddSubscription(subscribeImGuiLayer<ThemeLoadRequested>(*m_EventBus, *this, &ImGuiLayer::onThemeLoadRequested));
		AddSubscription(subscribeImGuiLayer<LayoutSaveRequested>(*m_EventBus, *this, &ImGuiLayer::onLayoutSaveRequested));
		AddSubscription(subscribeImGuiLayer<LayoutLoadRequested>(*m_EventBus, *this, &ImGuiLayer::onLayoutLoadRequested));
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
		auto configManager = m_ConfigManager.lock();
		if (configManager != nullptr && !state->layoutPath.empty())
		{
			std::string text;
			std::string error;
			const Path resolvedPath = Path::FromResolved(state->layoutPath);
			if (configManager->LoadTextFile(resolvedPath, text, error) && !text.empty())
			{
				ImGui::LoadIniSettingsFromMemory(text.data(), text.size());
				state->layoutStatusMessage = "Layout loaded: " + state->layoutPath;
				DS_LOG_INFO("ImGui layout loaded on UI state bind: {}", state->layoutPath);
			}
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

		auto configManager = m_ConfigManager.lock();
		if (configManager == nullptr)
		{
			DS_LOG_WARN("Layout reset requested but ConfigManager is unavailable");
			return;
		}

		std::string error;
		if (!configManager->SaveTextFile(layoutPath, "", error))
			DS_LOG_WARN("Layout reset failed: {}", error);
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

		auto configManager = m_ConfigManager.lock();
		if (configManager == nullptr)
			return;

		std::size_t size = 0;
		const char *data = ImGui::SaveIniSettingsToMemory(&size);
		std::string error;
		if (!configManager->SaveTextFile(layoutPath, std::string_view(data, size), error))
		{
			DS_LOG_WARN("{}: {}", failurePrefix, error);
			return;
		}

		DS_LOG_INFO("ImGui layout saved: {} bytes={}", layoutPath.String(), size);
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

		const Path localFontsDirectory = ImGuiLayerDetail::assetsFontsDirectory(*uiState);
		std::error_code directoryError;
		FileSystem::CreateDirectories(localFontsDirectory.Native(), directoryError);
		if (directoryError)
			DS_LOG_WARN("Unable to create assets font directory [{}]: {}",
			            localFontsDirectory.String(),
			            directoryError.message());

		std::vector<EditorFontOption> scannedFonts;
		std::set<std::string> seenPaths;
		std::size_t localFontCount = 0;
		std::size_t systemFontCount = 0;

		ImGuiLayerDetail::collectFontsFromDirectory(scannedFonts, seenPaths, localFontsDirectory.Native(), "Local", localFontCount);
		for (const FilePath &directory : Platform::GetSystemFontDirectories())
			ImGuiLayerDetail::collectFontsFromDirectory(scannedFonts, seenPaths, directory, "System", systemFontCount);

		std::sort(scannedFonts.begin(), scannedFonts.end(), [](const EditorFontOption &left, const EditorFontOption &right) {
			if (left.source != right.source)
				return left.source < right.source;
			return left.label < right.label;
		});

		uiState->fontOptions.clear();
		uiState->fontOptions.push_back(ImGuiLayerDetail::defaultFontOption());
		uiState->fontOptions.insert(uiState->fontOptions.end(), scannedFonts.begin(), scannedFonts.end());

		std::size_t selectedIndex = 0;
		const std::string selectedPath = uiState->selectedFontPath;
		if (!selectedPath.empty())
		{
			const std::string normalizedSelectedPath = ImGuiLayerDetail::normalizePathForCompare(selectedPath);
			for (std::size_t index = 1; index < uiState->fontOptions.size(); ++index)
			{
				if (ImGuiLayerDetail::normalizePathForCompare(uiState->fontOptions[index].path) == normalizedSelectedPath)
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
				+ std::to_string(localFontCount)
				+ ", system="
				+ std::to_string(systemFontCount)
				+ ".";
		}

		uiState->selectedFontIndex = selectedIndex;
		DS_LOG_INFO("Font scan complete: local={}, system={}, total={}",
		            localFontCount,
		            systemFontCount,
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
			const std::string normalizedSelectedPath = ImGuiLayerDetail::normalizePathForCompare(uiState->selectedFontPath);
			uiState->selectedFontIndex = 0;
			for (std::size_t index = 1; index < uiState->fontOptions.size(); ++index)
			{
				if (ImGuiLayerDetail::normalizePathForCompare(uiState->fontOptions[index].path) == normalizedSelectedPath)
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

		ImGuiLayerDetail::applyAppearanceToImGui(event.appearance);
		if (auto configManager = m_ConfigManager.lock())
		{
			configManager->SetAppearanceConfig(event.appearance);
			std::string saveError;
			const bool saved = configManager->SaveUserSettings(saveError);
			if (auto uiState = m_UiState.lock())
			{
				uiState->appearanceStatusMessage = saved
					? "Appearance applied and saved."
					: "Appearance applied, save failed: " + saveError;
			}
			if (saved)
				DS_LOG_INFO("Appearance applied and saved");
			else
				DS_LOG_WARN("Appearance applied but save failed: {}", saveError);
			return;
		}

		if (auto uiState = m_UiState.lock())
			uiState->appearanceStatusMessage = "Appearance applied.";
		DS_LOG_INFO("Appearance applied without ConfigManager persistence");
	}

	void ImGuiLayer::onThemeSaveRequested(const EditorUiEvents::ThemeSaveRequested &event)
	{
		auto uiState = m_UiState.lock();
		auto configManager = m_ConfigManager.lock();
		if (configManager == nullptr)
		{
			if (uiState != nullptr)
				uiState->appearanceStatusMessage = "Theme save failed: ConfigManager unavailable.";
			return;
		}

		std::string error;
		if (configManager->SaveAppearanceTheme(event.path, event.appearance, error))
		{
			if (uiState != nullptr)
				uiState->appearanceStatusMessage = "Theme saved: " + event.path.String();
			DS_LOG_INFO("Appearance theme saved: {}", event.path.String());
		}
		else if (uiState != nullptr)
		{
			uiState->appearanceStatusMessage = "Theme save failed: " + error;
			DS_LOG_WARN("Appearance theme save failed [{}]: {}", event.path.String(), error);
		}
	}

	void ImGuiLayer::onThemeLoadRequested(const EditorUiEvents::ThemeLoadRequested &event)
	{
		auto uiState = m_UiState.lock();
		auto configManager = m_ConfigManager.lock();
		if (configManager == nullptr)
		{
			if (uiState != nullptr)
				uiState->appearanceStatusMessage = "Theme load failed: ConfigManager unavailable.";
			return;
		}

		std::string error;
		AppearanceConfig appearance = uiState != nullptr ? uiState->appearance : m_Config.appearance;
		if (!configManager->LoadAppearanceTheme(event.path, appearance, error))
		{
			if (uiState != nullptr)
				uiState->appearanceStatusMessage = "Theme load failed: " + error;
			DS_LOG_WARN("Appearance theme load failed [{}]: {}", event.path.String(), error);
			return;
		}

		m_Config.appearance = appearance;
		ImGuiLayerDetail::applyAppearanceToImGui(appearance);
		configManager->SetAppearanceConfig(appearance);
		std::string saveError;
		(void)configManager->SaveUserSettings(saveError);
		if (uiState != nullptr)
		{
			uiState->appearance = appearance;
			uiState->appearanceStatusMessage = "Theme loaded: " + event.path.String();
		}
		DS_LOG_INFO("Appearance theme loaded: {}", event.path.String());
	}

	void ImGuiLayer::onLayoutSaveRequested(const EditorUiEvents::LayoutSaveRequested &event)
	{
		auto uiState = m_UiState.lock();
		auto configManager = m_ConfigManager.lock();
		if (configManager == nullptr)
		{
			if (uiState != nullptr)
				uiState->layoutStatusMessage = "Layout save failed: ConfigManager unavailable.";
			return;
		}

		const Path path = resolveRequestedLayoutPath(event.path);
		std::size_t size = 0;
		const char *data = ImGui::SaveIniSettingsToMemory(&size);
		std::string error;
		if (configManager->SaveTextFile(path, std::string_view(data, size), error))
		{
			if (uiState != nullptr)
				uiState->layoutStatusMessage = "Layout saved: " + path.String();
			DS_LOG_INFO("Layout saved from request: {} bytes={}", path.String(), size);
		}
		else if (uiState != nullptr)
		{
			uiState->layoutStatusMessage = "Layout save failed: " + error;
			DS_LOG_WARN("Layout save request failed [{}]: {}", path.String(), error);
		}
	}

	void ImGuiLayer::onLayoutLoadRequested(const EditorUiEvents::LayoutLoadRequested &event)
	{
		auto uiState = m_UiState.lock();
		auto configManager = m_ConfigManager.lock();
		if (configManager == nullptr)
		{
			if (uiState != nullptr)
				uiState->layoutStatusMessage = "Layout load failed: ConfigManager unavailable.";
			return;
		}

		std::string text;
		std::string error;
		const Path path = resolveRequestedLayoutPath(event.path);
		if (configManager->LoadTextFile(path, text, error))
		{
			ImGui::LoadIniSettingsFromMemory(text.data(), text.size());
			if (uiState != nullptr)
				uiState->layoutStatusMessage = "Layout loaded: " + path.String();
			DS_LOG_INFO("Layout loaded from request: {} bytes={}", path.String(), text.size());
		}
		else if (uiState != nullptr)
		{
			uiState->layoutStatusMessage = "Layout load failed: " + error;
			DS_LOG_WARN("Layout load request failed [{}]: {}", path.String(), error);
		}
	}

	void ImGuiLayer::onLayoutResetRequested(const EditorUiEvents::LayoutResetRequested &event)
	{
		ImGui::LoadIniSettingsFromMemory("", 0);
		const Path path = resolveRequestedLayoutPath(event.path);
		if (auto configManager = m_ConfigManager.lock())
		{
			std::string error;
			(void)configManager->SaveTextFile(path, "", error);
		}

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
