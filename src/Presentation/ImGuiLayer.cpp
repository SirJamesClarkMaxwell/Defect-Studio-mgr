#include "Core/dspch.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <set>
#include <string_view>

#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <imgui.h>

#include "Presentation/ImGuiLayer.hpp"

#include "App/ConfigManager.hpp"
#include "App/Window.hpp"
#include "Core/Platform/PlatformPaths.hpp"
#include "Core/Utils/Logger.hpp"
#include "Core/Utils/Path.hpp"
#include "Presentation/EditorUiState.hpp"

namespace DefectStudio
{
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

	ImGuiLayer::ImGuiLayer() : Layer("ImGuiLayer")
	{
	}

	ImGuiLayer::ImGuiLayer(WeakRef<Window> window, WeakRef<ConfigManager> configManager, ApplicationConfig config, bool resetLayout)
		: ImGuiLayer()
	{
		BindRuntime(window, std::move(configManager), std::move(config), resetLayout);
	}

	void ImGuiLayer::ApplyAppearanceToCurrentContext(const AppearanceConfig &appearance)
	{
		ImGuiLayerDetail::applyAppearanceToImGui(appearance);
	}

	void ImGuiLayer::ApplyUiConfig(const UIConfig &uiConfig)
	{
		m_Config.ui = uiConfig;
		applyUiConfigToContext();
	}

	void ImGuiLayer::BindRuntime(WeakRef<Window> window, WeakRef<ConfigManager> configManager, ApplicationConfig config, bool resetLayout)
	{
		m_Window = std::move(window);
		m_ConfigManager = std::move(configManager);
		m_Config = std::move(config);
		m_ResetLayoutOnAttach = resetLayout;
	}

	void ImGuiLayer::BindConfigManager(WeakRef<ConfigManager> configManager)
	{
		m_ConfigManager = std::move(configManager);
	}

	void ImGuiLayer::BindUiState(WeakRef<EditorUiState> uiState)
	{
		m_UiState = std::move(uiState);
		syncFontsFromSources();
		applySelectedFont();
		if (auto state = m_UiState.lock())
		{
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
				}
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

	void ImGuiLayer::RenderDrawData()
	{
		if (!m_Initialized)
			return;

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
		shutdownImGui();
		m_UiState.reset();
		m_ConfigManager.reset();
		m_Window.reset();
		DS_LOG_INFO("ImGuiLayer detached");
	}

	void ImGuiLayer::OnUpdate(float)
	{
		handleUiRequests();
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

		auto configManager = m_ConfigManager.lock();
		if (configManager == nullptr)
			return;

		Path layoutPath = resolveLayoutPath();
		if (auto uiState = m_UiState.lock())
		{
			if (!uiState->layoutPath.empty())
				layoutPath = Path::FromResolved(uiState->layoutPath);
		}

		if (layoutPath.Empty())
			return;

		std::size_t size = 0;
		const char *data = ImGui::SaveIniSettingsToMemory(&size);
		std::string error;
		if (!configManager->SaveTextFile(layoutPath, std::string_view(data, size), error))
			DS_LOG_WARN("Layout save on detach failed: {}", error);
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

	void ImGuiLayer::handleUiRequests()
	{
		auto uiState = m_UiState.lock();
		if (uiState == nullptr)
			return;

		if (uiState->fontListRefreshRequested)
		{
			uiState->fontListRefreshRequested = false;
			syncFontsFromSources();
			applySelectedFont();
		}

		if (uiState->fontReloadRequested)
		{
			uiState->fontReloadRequested = false;
			applySelectedFont();
		}

		if (uiState->appearancePreviewRequested)
		{
			uiState->appearancePreviewRequested = false;
			ImGuiLayerDetail::applyAppearanceToImGui(uiState->appearance);
		}

		if (uiState->appearanceApplyRequested)
		{
			uiState->appearanceApplyRequested = false;
			ImGuiLayerDetail::applyAppearanceToImGui(uiState->appearance);
			if (auto configManager = m_ConfigManager.lock())
			{
				configManager->SetAppearanceConfig(uiState->appearance);
				std::string saveError;
				if (configManager->SaveUserSettings(saveError))
					uiState->appearanceStatusMessage = "Appearance applied and saved.";
				else
					uiState->appearanceStatusMessage = "Appearance applied, save failed: " + saveError;
			}
			else
			{
				uiState->appearanceStatusMessage = "Appearance applied.";
			}
		}

		if (uiState->themeSaveRequested)
		{
			uiState->themeSaveRequested = false;
			auto configManager = m_ConfigManager.lock();
			if (configManager == nullptr)
			{
				uiState->appearanceStatusMessage = "Theme save failed: ConfigManager unavailable.";
			}
			else
			{
				std::string error;
				const Path path = Path::FromResolved(uiState->themeSavePath);
				if (configManager->SaveAppearanceTheme(path, uiState->appearance, error))
					uiState->appearanceStatusMessage = "Theme saved: " + path.String();
				else
					uiState->appearanceStatusMessage = "Theme save failed: " + error;
			}
		}

		if (uiState->themeLoadRequested)
		{
			uiState->themeLoadRequested = false;
			auto configManager = m_ConfigManager.lock();
			if (configManager == nullptr)
			{
				uiState->appearanceStatusMessage = "Theme load failed: ConfigManager unavailable.";
			}
			else
			{
				std::string error;
				const Path path = Path::FromResolved(uiState->themeLoadPath);
				AppearanceConfig appearance = uiState->appearance;
				if (configManager->LoadAppearanceTheme(path, appearance, error))
				{
					uiState->appearance = appearance;
					ImGuiLayerDetail::applyAppearanceToImGui(uiState->appearance);
					configManager->SetAppearanceConfig(uiState->appearance);
					std::string saveError;
					(void)configManager->SaveUserSettings(saveError);
					uiState->appearanceStatusMessage = "Theme loaded: " + path.String();
				}
				else
				{
					uiState->appearanceStatusMessage = "Theme load failed: " + error;
				}
			}
		}

		if (uiState->layoutSaveRequested)
		{
			uiState->layoutSaveRequested = false;
			auto configManager = m_ConfigManager.lock();
			if (configManager == nullptr)
			{
				uiState->layoutStatusMessage = "Layout save failed: ConfigManager unavailable.";
			}
			else
			{
				std::size_t size = 0;
				const char *data = ImGui::SaveIniSettingsToMemory(&size);
				std::string error;
				const Path path = Path::FromResolved(uiState->layoutPath);
				if (configManager->SaveTextFile(path, std::string_view(data, size), error))
					uiState->layoutStatusMessage = "Layout saved: " + path.String();
				else
					uiState->layoutStatusMessage = "Layout save failed: " + error;
			}
		}

		if (uiState->layoutLoadRequested)
		{
			uiState->layoutLoadRequested = false;
			auto configManager = m_ConfigManager.lock();
			if (configManager == nullptr)
			{
				uiState->layoutStatusMessage = "Layout load failed: ConfigManager unavailable.";
			}
			else
			{
				std::string text;
				std::string error;
				const Path path = Path::FromResolved(uiState->layoutPath);
				if (configManager->LoadTextFile(path, text, error))
				{
					ImGui::LoadIniSettingsFromMemory(text.data(), text.size());
					uiState->layoutStatusMessage = "Layout loaded: " + path.String();
				}
				else
				{
					uiState->layoutStatusMessage = "Layout load failed: " + error;
				}
			}
		}

		if (uiState->layoutResetRequested)
		{
			uiState->layoutResetRequested = false;
			ImGui::LoadIniSettingsFromMemory("", 0);
			if (auto configManager = m_ConfigManager.lock())
			{
				std::string error;
				(void)configManager->SaveTextFile(Path::FromResolved(uiState->layoutPath), "", error);
			}
			uiState->layoutStatusMessage = "Runtime layout reset.";
		}
	}
} // namespace DefectStudio
