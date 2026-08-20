#include "Core/dspch.hpp"

#include "Presentation/Panels/ProjectTreePanel.hpp"

#include <algorithm>
#include <vector>

#include <imgui.h>

#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/Logging/Logger.hpp"
#include "Core/Platform/FileDialog.hpp"
#include "Events/RendererEvents.hpp"
#include "IO/TextFileIO.hpp"

namespace DefectStudio
{
	namespace
	{
		// Minimal stand-in for real T07.5.1 per-project persistence (which needs a project
		// manifest format that doesn't exist yet, and the main YamlConfigSerializer is documented
		// as unsafe to extend until its own cleanup - see EditorLayer.cpp). One plain-text file
		// with just the last-picked root, independent of that system entirely, so the app doesn't
		// need re-Browsing to the same mounted drive on every single launch.
		[[nodiscard]] Path LastProjectRootStatePath()
		{
			return Path::FromResolved(
				FileSystem::CurrentPath() / "install" / "users" / "default" / "config" / "last_project_root.txt");
		}

		void SaveLastProjectRoot(const Path &root)
		{
			std::string error;
			if (!TextFileIO::Save(LastProjectRootStatePath(), root.String(), error))
				DS_LOG_WARN("ProjectTreePanel: failed to persist last project root: {}", error);
		}

		[[nodiscard]] Path LoadLastProjectRoot()
		{
			std::string text;
			std::string error;
			if (!TextFileIO::Load(LastProjectRootStatePath(), text, error))
				return {};
			while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
				text.pop_back();
			return Path(text);
		}

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

		// The leaf calc-type folder (e.g. "singlet_HSE") is shared across many different defects,
		// so it's a bad window title - the top-level folder directly under the project root (e.g.
		// "6-7", a defect-site-pair label) is what actually identifies which defect this is.
		// Two different calc types under the same defect deliberately end up with the same title
		// (window identity/docking never depends on this string - see RendererPanel).
		[[nodiscard]] std::string DeriveMainFolderName(const Path &root, const Path &directory)
		{
			if (root.Empty())
				return directory.filename().String();
			std::error_code error;
			const std::filesystem::path relative = std::filesystem::relative(directory.Native(), root.Native(), error);
			if (error || relative.empty() || relative == std::filesystem::path("."))
				return directory.filename().String();
			return relative.begin()->string();
		}
	} // namespace

	ProjectTreePanel::ProjectTreePanel(
		Ref<EventBus> eventBus, std::string title, bool visibleByDefault, Path initialRootPath)
		: IPanel(std::move(title), visibleByDefault),
		  m_EventBus(std::move(eventBus)),
		  m_RootPath(std::move(initialRootPath))
	{
		// The last folder the user actually picked wins over whatever hardcoded/passed-in default
		// the caller supplied - persists across restarts, see LastProjectRootStatePath.
		const Path persistedRoot = LoadLastProjectRoot();
		if (!persistedRoot.Empty() && FileSystem::Exists(persistedRoot.Native()))
			m_RootPath = persistedRoot;
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
		{
			// Consumes last frame's m_VisibleFlatList/m_SelectedPath, before this frame rebuilds
			// them - one frame of lag between a keypress and the flat list reflecting it is
			// imperceptible and far simpler than trying to keep both in sync mid-recursion.
			if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
				handleKeyboardNavigation();
			m_VisibleFlatList.clear();
			renderDirectoryContents(m_RootPath);
		}

		ImGui::End();
	}

	void ProjectTreePanel::openDefectAt(const Path &directory)
	{
		if (!HasDefectFile(directory) || m_EventBus == nullptr)
			return;
		RendererEvents::Windows::OpenStructureRequested request;
		request.filePath = ResolveDefectFile(directory);
		request.displayName = DeriveMainFolderName(m_RootPath, directory);
		m_EventBus->Queue(request);
	}

	void ProjectTreePanel::handleKeyboardNavigation()
	{
		if (m_VisibleFlatList.empty())
			return;

		int selectedIndex = -1;
		for (std::size_t index = 0; index < m_VisibleFlatList.size(); ++index)
		{
			if (m_VisibleFlatList[index].String() == m_SelectedPath)
			{
				selectedIndex = static_cast<int>(index);
				break;
			}
		}

		if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))
		{
			selectedIndex = std::clamp(selectedIndex + 1, 0, static_cast<int>(m_VisibleFlatList.size()) - 1);
			m_SelectedPath = m_VisibleFlatList[selectedIndex].String();
			return;
		}
		if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))
		{
			selectedIndex = std::clamp(selectedIndex - 1, 0, static_cast<int>(m_VisibleFlatList.size()) - 1);
			m_SelectedPath = m_VisibleFlatList[selectedIndex].String();
			return;
		}
		if (selectedIndex < 0)
			return;

		const Path &selected = m_VisibleFlatList[selectedIndex];
		const bool isDirectory = FileSystem::IsDirectory(selected.Native());
		const bool isOpen = m_ExpandedPaths.contains(selected.String()) && m_ExpandedPaths[selected.String()];

		if (ImGui::IsKeyPressed(ImGuiKey_RightArrow))
		{
			if (isDirectory)
				m_ExpandedPaths[selected.String()] = true;
		}
		else if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow))
		{
			if (isDirectory && isOpen)
				m_ExpandedPaths[selected.String()] = false;
			else
			{
				const std::string parentKey = selected.parent_path().String();
				if (!parentKey.empty() && parentKey != selected.String())
					m_SelectedPath = parentKey;
			}
		}
		else if (ImGui::GetIO().KeyShift && ImGui::IsKeyPressed(ImGuiKey_Enter))
		{
			openDefectAt(selected);
		}
		else if (ImGui::IsKeyPressed(ImGuiKey_Enter))
		{
			if (isDirectory)
				m_ExpandedPaths[selected.String()] = !isOpen;
		}
	}

	void ProjectTreePanel::renderPickRootButton()
	{
		if (ImGui::Button("Pick Folder..."))
		{
			Result<std::optional<Path>> picked = Platform::PickFolder(m_RootPath);
			if (picked && picked->has_value())
			{
				m_RootPath = picked->value();
				SaveLastProjectRoot(m_RootPath);
			}
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
			const std::string pathKey = entryPath.String();
			m_VisibleFlatList.push_back(entryPath);
			const bool isSelected = pathKey == m_SelectedPath;

			const std::string label = entry.path.filename().string();
			const std::string idLabel = label + "##" + entry.path.string();

			// ImGuiCol_Header is what ImGuiTreeNodeFlags_Selected paints as background - the
			// theme's default selection alpha (~0.22, see ui_settings.yaml state_rules) reads as
			// barely-there on a dark tree, so the selected row gets a solid, much more opaque tint
			// instead just for this one item.
			const int pushedColors = isSelected ? 3 : 0;
			if (isSelected)
			{
				ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.85f, 0.42f, 0.05f, 0.85f));
				ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.95f, 0.50f, 0.10f, 0.9f));
				ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.95f, 0.50f, 0.10f, 0.9f));
			}

			if (!entry.isDirectory)
			{
				ImGuiTreeNodeFlags leafFlags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen
					| ImGuiTreeNodeFlags_SpanFullWidth | ImGuiTreeNodeFlags_Bullet;
				if (isSelected)
					leafFlags |= ImGuiTreeNodeFlags_Selected;
				ImGui::TreeNodeEx(idLabel.c_str(), leafFlags, "%s", label.c_str());
				if (ImGui::IsItemClicked())
					m_SelectedPath = pathKey;
				ImGui::PopStyleColor(pushedColors);
				continue;
			}

			// Source of truth going into the frame is our own map (so keyboard Left/Right/Enter can
			// drive it); whatever TreeNodeEx actually returns (arrow-click, double-click, or our own
			// forced value) is written straight back so next frame's map read matches reality.
			ImGui::SetNextItemOpen(m_ExpandedPaths.contains(pathKey) && m_ExpandedPaths[pathKey]);
			ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanFullWidth | ImGuiTreeNodeFlags_OpenOnArrow
				| ImGuiTreeNodeFlags_OpenOnDoubleClick;
			if (isSelected)
				flags |= ImGuiTreeNodeFlags_Selected;
			const bool open = ImGui::TreeNodeEx(idLabel.c_str(), flags, "%s", label.c_str());
			ImGui::PopStyleColor(pushedColors);
			if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
				m_SelectedPath = pathKey;
			m_ExpandedPaths[pathKey] = open;
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
		if (ImGui::MenuItem("Open Defect", "Shift+Enter", false, hasDefectFile))
			openDefectAt(directory);
		ImGui::EndPopup();
	}
} // namespace DefectStudio
