#include "Core/dspch.hpp"

#include "Renderer/RendererLayer.hpp"

#include <algorithm>
#include <array>

#include <yaml-cpp/yaml.h>

#include "Core/Utils/Logger.hpp"
#include "Core/Utils/Time.hpp"
#include "Renderer/OpenGl/OpenGlRendererBackend.hpp"
#include "Renderer/RendererQuickTestBootstrap.hpp"
#include "Renderer/RendererViewCamera.hpp"

namespace DefectStudio
{
	namespace
	{
		[[nodiscard]] Path BuildShaderDirectoryFromCurrentPath()
		{
			return Path::FromResolved(
				FileSystem::CurrentPath()
				/ "src"
				/ "Renderer"
				/ "OpenGl"
				/ "Shaders");
		}

		[[nodiscard]] Path BuildShaderDirectoryFromAssetsRoot(const Path &assetsDirectory)
		{
			if (assetsDirectory.Empty())
				return {};

			const FilePath repositoryRoot = assetsDirectory.Native()
				.parent_path() // app
				.parent_path() // install
				.parent_path(); // repository root
			if (repositoryRoot.empty())
				return {};

			return Path::FromResolved(
				repositoryRoot
				/ "src"
				/ "Renderer"
				/ "OpenGl"
				/ "Shaders");
		}
	}

	RendererLayer::RendererLayer(RendererQuickTestRuntime runtime)
		: Layer("RendererLayer"), m_Runtime(std::move(runtime))
	{
	}

	RendererLayer::~RendererLayer() = default;

	void RendererLayer::OnAttach()
	{
		m_RendererBackend = CreateUnique<OpenGlRendererBackend>();
		const Path shaderDirectory = resolveShaderDirectory();
		if (shaderDirectory.Empty() || !FileSystem::Exists(shaderDirectory.Native()))
		{
			DS_LOG_ERROR(
				"RendererLayer initialization failed: shader directory not found [{}]",
				shaderDirectory.String());
			m_RendererBackend.reset();
			return;
		}

		Result<void> initializeResult = m_RendererBackend->Initialize(shaderDirectory);
		if (!initializeResult.HasValue())
		{
			DS_LOG_ERROR(
				"RendererLayer initialization failed: {}",
				initializeResult.Error().technicalDetails);
			m_RendererBackend.reset();
			return;
		}

		if (m_Runtime.enableQuickTestingStartup)
			loadQuickTestWindows();
		loadCameraState();
		m_Attached = true;
		DS_LOG_INFO("Renderer shader root: {}", shaderDirectory.String());
		DS_LOG_INFO("RendererLayer attached with {} quick-test windows", m_Windows.size());
	}

	void RendererLayer::OnDetach()
	{
		saveCameraState();
		if (m_RendererBackend != nullptr)
			m_RendererBackend->Shutdown();
		m_RendererBackend.reset();
		m_Windows.clear();
		m_Attached = false;
		DS_LOG_INFO("RendererLayer detached");
	}

	void RendererLayer::OnUpdate(float deltaTime)
	{
		m_LastDeltaTime = deltaTime;
		if (m_RendererBackend != nullptr)
			m_RendererBackend->ReloadShadersIfNeeded();
	}

	void RendererLayer::OnImGuiRender()
	{
		if (!m_Attached || m_RendererBackend == nullptr)
			return;

		// Exception-free rendering boundary:
		// per-frame renderer calls do not throw and are expected to fail through logs/results.
		for (RendererWindowState &windowState : m_Windows)
			renderStructureWindow(windowState, m_LastDeltaTime);
	}

	void RendererLayer::loadQuickTestWindows()
	{
		m_Windows = BuildRendererQuickTestWindows(m_Runtime.assetsDirectory);
	}

	void RendererLayer::loadCameraState()
	{
		const Path statePath = rendererQuickStatePath();
		if (!FileSystem::Exists(statePath.Native()))
			return;

		try
		{
			YAML::Node root = YAML::LoadFile(statePath.String());
			const YAML::Node windowsNode = root["windows"];
			if (!windowsNode || !windowsNode.IsMap())
				return;

			for (RendererWindowState &windowState : m_Windows)
			{
				if (windowState.camera == nullptr)
					continue;
				const YAML::Node node = windowsNode[windowState.title];
				if (!node || !node.IsMap())
					continue;

				const YAML::Node targetNode = node["target"];
				if (targetNode && targetNode.IsSequence() && targetNode.size() == 3)
				{
					const glm::vec3 target(
						targetNode[0].as<float>(),
						targetNode[1].as<float>(),
						targetNode[2].as<float>());
					windowState.camera->SetTarget(target);
				}

				if (node["distance"])
					windowState.camera->SetDistance(node["distance"].as<float>());
				if (node["yaw"] && node["pitch"])
					windowState.camera->SetYawPitch(node["yaw"].as<float>(), node["pitch"].as<float>());
			}
		}
		catch (const std::exception &exception)
		{
			DS_LOG_WARN("Renderer camera state load failed: {}", exception.what());
		}
	}

	void RendererLayer::saveCameraState() const
	{
		if (m_Windows.empty())
			return;

		std::error_code directoryError;
		FileSystem::CreateDirectories(m_Runtime.configDirectory.Native(), directoryError);
		if (directoryError)
		{
			DS_LOG_WARN("Renderer camera state directory create failed: {}", directoryError.message());
			return;
		}

		YAML::Node root;
		YAML::Node windowsNode(YAML::NodeType::Map);
		for (const RendererWindowState &windowState : m_Windows)
		{
			if (windowState.camera == nullptr)
				continue;
			YAML::Node windowNode;
			const glm::vec3 target = windowState.camera->Target();
			windowNode["target"].push_back(target.x);
			windowNode["target"].push_back(target.y);
			windowNode["target"].push_back(target.z);
			windowNode["distance"] = windowState.camera->Distance();
			windowNode["yaw"] = windowState.camera->Yaw();
			windowNode["pitch"] = windowState.camera->Pitch();
			windowsNode[windowState.title] = windowNode;
		}
		root["windows"] = windowsNode;

		std::ofstream output(rendererQuickStatePath().Native(), std::ios::trunc);
		if (!output)
		{
			DS_LOG_WARN("Renderer camera state save failed: cannot open output file");
			return;
		}
		output << root;
	}

	void RendererLayer::renderStructureWindow(RendererWindowState &windowState, float deltaTime)
	{
		(void)deltaTime;
		if (windowState.camera == nullptr)
			return;

		const bool began = ImGui::Begin(windowState.title.c_str());
		if (!began)
		{
			ImGui::End();
			return;
		}

		drawViewportToolbar(windowState);
		ImGui::Separator();

		const ImVec2 available = ImGui::GetContentRegionAvail();
		windowState.viewportSize.x = std::max(available.x, 64.0f);
		windowState.viewportSize.y = std::max(available.y, 64.0f);
		windowState.camera->SetViewport(windowState.viewportSize.x, windowState.viewportSize.y);

		const ImVec2 imageOrigin = ImGui::GetCursorScreenPos();
		const unsigned int textureId = m_RendererBackend->RenderWindow(
			windowState.title,
			windowState.structure,
			*windowState.camera,
			static_cast<int>(windowState.viewportSize.x),
			static_cast<int>(windowState.viewportSize.y),
			windowState.showAtoms,
			windowState.showBonds,
			windowState.showCellBox,
			windowState.showGrid);

		ImGui::Image(
			static_cast<ImTextureID>(static_cast<uintptr_t>(textureId)),
			windowState.viewportSize,
			ImVec2(0.0f, 1.0f),
			ImVec2(1.0f, 0.0f));

		const bool hovered = ImGui::IsItemHovered();
		if (hovered)
		{
			if (ImGui::IsMouseDragging(ImGuiMouseButton_Left))
				windowState.camera->Orbit(ImGui::GetIO().MouseDelta.x, ImGui::GetIO().MouseDelta.y);
			if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle))
				windowState.camera->Pan(ImGui::GetIO().MouseDelta.x, ImGui::GetIO().MouseDelta.y);
			const float wheel = ImGui::GetIO().MouseWheel;
			if (wheel != 0.0f)
				windowState.camera->Zoom(wheel);
		}

		ImGui::SetCursorScreenPos(imageOrigin);
		drawViewportGizmo(windowState);
		ImGui::End();
	}

	void RendererLayer::drawViewportToolbar(RendererWindowState &windowState)
	{
		ImGui::Checkbox("Atoms", &windowState.showAtoms);
		ImGui::SameLine();
		ImGui::Checkbox("Bonds", &windowState.showBonds);
		ImGui::SameLine();
		ImGui::Checkbox("Cell", &windowState.showCellBox);
		ImGui::SameLine();
		ImGui::Checkbox("Grid", &windowState.showGrid);
		ImGui::SameLine();
		if (ImGui::Button("Reset View"))
		{
			glm::vec3 minimum(1e6f, 1e6f, 1e6f);
			glm::vec3 maximum(-1e6f, -1e6f, -1e6f);
			for (const RendererAtomData &atom : windowState.structure.atoms)
			{
				minimum = glm::min(minimum, atom.cartesianPosition);
				maximum = glm::max(maximum, atom.cartesianPosition);
			}
			windowState.camera->FocusBounds(minimum, maximum);
			windowState.camera->SetFromDirection(glm::normalize(glm::vec3(1.0f, 1.0f, 0.9f)));
		}
	}

	void RendererLayer::drawViewportGizmo(RendererWindowState &windowState)
	{
		ImGui::BeginGroup();
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.13f, 0.14f, 0.85f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.18f, 0.24f, 0.32f, 0.95f));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.20f, 0.28f, 0.38f, 0.95f));

		if (ImGui::SmallButton("X"))
			windowState.camera->SetFromDirection(glm::normalize(glm::vec3(1.0f, 0.0f, 0.0f)));
		ImGui::SameLine();
		if (ImGui::SmallButton("Y"))
			windowState.camera->SetFromDirection(glm::normalize(glm::vec3(0.0f, 1.0f, 0.0f)));
		ImGui::SameLine();
		if (ImGui::SmallButton("Z"))
			windowState.camera->SetFromDirection(glm::normalize(glm::vec3(0.0f, 0.0f, 1.0f)));
		ImGui::SameLine();
		if (ImGui::SmallButton("ISO"))
			windowState.camera->SetFromDirection(glm::normalize(glm::vec3(1.0f, 1.0f, 1.0f)));

		ImGui::PopStyleColor(3);
		ImGui::EndGroup();
	}

	Path RendererLayer::rendererQuickStatePath() const
	{
		return m_Runtime.configDirectory / Path("renderer_quicktest.yaml");
	}

	Path RendererLayer::resolveShaderDirectory() const
	{
		if (!m_Runtime.shaderDirectory.Empty())
		{
			const Path resolvedExplicit = Path::FromResolved(m_Runtime.shaderDirectory.Native());
			if (FileSystem::Exists(resolvedExplicit.Native()))
				return resolvedExplicit;
		}

		const std::array<Path, 2> candidates = {
			BuildShaderDirectoryFromCurrentPath(),
			BuildShaderDirectoryFromAssetsRoot(m_Runtime.assetsDirectory)};

		for (const Path &candidate : candidates)
		{
			if (candidate.Empty())
				continue;
			if (FileSystem::Exists(candidate.Native()))
				return candidate;
		}

		if (!m_Runtime.shaderDirectory.Empty())
			return Path::FromResolved(m_Runtime.shaderDirectory.Native());
		return candidates[0];
	}
} // namespace DefectStudio
