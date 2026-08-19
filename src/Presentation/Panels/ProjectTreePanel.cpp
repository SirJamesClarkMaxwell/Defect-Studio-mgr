#include "Core/dspch.hpp"

#include "Presentation/Panels/ProjectTreePanel.hpp"

#include <algorithm>
#include <vector>

#include <imgui.h>

#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/Platform/FileDialog.hpp"
#include "Events/RendererEvents.hpp"

namespace DefectStudio
{
	namespace
	{
		[[nodiscard]] bool HasDefectFile(const Path &directory)
		{
			return FileSystem::Exists((directory / "CONTCAR").Native()) || FileSystem::Exists((directory / "POSCAR").Native());
		}

		// CONTCAR (relaxed result) wins over POSCAR (input) when both exist - see plan/user request.
		[[nodiscard]] Path ResolveDefectFile(const Path &directory)
		{
			const Path contcar = directory / "CONTCAR";
			if (FileSystem::Exists(contcar.Native()))
				return contcar;
			return directory / "POSCAR";
		}
	} // namespace

	ProjectTreePanel::ProjectTreePanel(
		Ref<EventBus> eventBus, std::string title, bool visibleByDefault, Path initialRootPath)
		: IPanel(std::move(title), visibleByDefault),
		  m_EventBus(std::move(eventBus)),
		  m_RootPath(std::move(initialRootPath))
	{
	}

	Ref<IPanel> ProjectTreePanel::Clone() const
	{
		return CreateRef<ProjectTreePanel>(*this);
	}

	void ProjectTreePanel::Render()
	{
		if (!IsVisible())
			return;

		bool visible = IsVisible();
		ImGui::SetNextWindowSize(ImVec2(320.0f, 420.0f), ImGuiCond_FirstUseEver);
		if (!ImGui::Begin(GetTitle().c_str(), &visible))
		{
			SetVisible(visible);
			ImGui::End();
			return;
		}
		SetVisible(visible);

		renderPickRootButton();
		ImGui::Separator();

		if (m_RootPath.Empty())
			ImGui::TextDisabled("No folder selected.");
		else if (!FileSystem::Exists(m_RootPath.Native()))
			ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.35f, 1.0f), "Folder not found (mount disconnected?): %s", m_RootPath.String().c_str());
		else
			renderDirectoryContents(m_RootPath);

		ImGui::End();
	}

	void ProjectTreePanel::renderPickRootButton()
	{
		if (ImGui::Button("Pick Folder..."))
		{
			Result<std::optional<Path>> picked = Platform::PickFolder(m_RootPath);
			if (picked && picked->has_value())
				m_RootPath = picked->value();
		}
		if (!m_RootPath.Empty())
		{
			ImGui::SameLine();
			ImGui::TextDisabled("%s", m_RootPath.String().c_str());
		}
	}

	void ProjectTreePanel::renderDirectoryContents(const Path &directory)
	{
		std::vector<DirectoryEntryInfo> entries = FileSystem::ListDirectory(directory.Native());
		std::sort(entries.begin(), entries.end(), [](const DirectoryEntryInfo &a, const DirectoryEntryInfo &b)
		{
			if (a.isDirectory != b.isDirectory)
				return a.isDirectory; // directories first
			return a.path.filename().string() < b.path.filename().string();
		});

		for (const DirectoryEntryInfo &entry : entries)
		{
			const Path entryPath(entry.path);
			const std::string label = entry.path.filename().string();
			const std::string idLabel = label + "##" + entry.path.string();

			if (!entry.isDirectory)
			{
				ImGui::TreeNodeEx(idLabel.c_str(), ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen
					| ImGuiTreeNodeFlags_SpanFullWidth | ImGuiTreeNodeFlags_Bullet, "%s", label.c_str());
				continue;
			}

			const ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanFullWidth | ImGuiTreeNodeFlags_OpenOnArrow
				| ImGuiTreeNodeFlags_OpenOnDoubleClick;
			const bool open = ImGui::TreeNodeEx(idLabel.c_str(), flags, "%s", label.c_str());
			renderDirectoryContextMenu(entryPath);
			if (open)
			{
				renderDirectoryContents(entryPath);
				ImGui::TreePop();
			}
		}
	}

	void ProjectTreePanel::renderDirectoryContextMenu(const Path &directory)
	{
		if (!ImGui::BeginPopupContextItem())
			return;

		const bool hasDefectFile = HasDefectFile(directory);
		if (ImGui::MenuItem("Open Defect", nullptr, false, hasDefectFile))
		{
			if (m_EventBus != nullptr)
			{
				RendererEvents::Windows::OpenStructureRequested request;
				request.filePath = ResolveDefectFile(directory);
				request.displayName = directory.filename().string();
				m_EventBus->Queue(request);
			}
		}
		ImGui::EndPopup();
	}
} // namespace DefectStudio
