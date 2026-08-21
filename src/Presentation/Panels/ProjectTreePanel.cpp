#include "Core/dspch.hpp"

#include "Presentation/Panels/ProjectTreePanel.hpp"

#include <algorithm>
#include <cstdio>

#include <imgui.h>

#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/Platform/FileDialog.hpp"
#include "Events/ProjectEvents.hpp"
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

		// The leaf calc-type folder (e.g. "singlet_HSE") is shared across many different defects,
		// so it's a bad window title - the top-level folder directly under whichever registered
		// root contains it (e.g. "6-7", a defect-site-pair label) is what actually identifies
		// which defect this is. Two different calc types under the same defect deliberately end up
		// with the same title (window identity/docking never depends on this string - see
		// RendererPanel). Falls back to just the folder's own name if `directory` isn't under any
		// known root (shouldn't happen - defensive only).
		[[nodiscard]] std::string DeriveMainFolderName(const std::vector<ProjectRootEntry> &roots, const Path &directory)
		{
			for (const ProjectRootEntry &root : roots)
			{
				std::error_code error;
				const std::filesystem::path relative = std::filesystem::relative(directory.Native(), root.path.Native(), error);
				if (error || relative.empty() || relative == std::filesystem::path("."))
					continue;
				const std::string first = relative.begin()->string();
				if (first == "..")
					continue;
				return first;
			}
			return directory.filename().String();
		}
	} // namespace

	ProjectTreePanel::ProjectTreePanel(Ref<EventBus> eventBus, std::string title, bool visibleByDefault)
		: IPanel(std::move(title), visibleByDefault), m_EventBus(std::move(eventBus))
	{
	}

	Ref<IPanel> ProjectTreePanel::Clone() const
	{
		return CreateRef<ProjectTreePanel>(*this);
	}

	void ProjectTreePanel::SetRoots(std::vector<ProjectRootEntry> roots)
	{
		m_Roots = std::move(roots);
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

		renderToolbar();
		ImGui::Separator();

		if (m_Roots.empty())
			ImGui::TextDisabled("No folders registered - use \"+ Add Root...\" above.");
		else
		{
			// Consumes last frame's m_VisibleFlatList/m_SelectedPath, before this frame rebuilds
			// them - one frame of lag between a keypress and the flat list reflecting it is
			// imperceptible and far simpler than trying to keep both in sync mid-recursion.
			if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
				handleKeyboardNavigation();
			m_VisibleFlatList.clear();
			for (const ProjectRootEntry &section : m_Roots)
				renderRootSection(section);
		}

		ImGui::End();
	}

	void ProjectTreePanel::openDefectAt(const Path &directory)
	{
		if (!HasDefectFile(directory) || m_EventBus == nullptr)
			return;
		RendererEvents::Windows::OpenStructureRequested request;
		request.filePath = ResolveDefectFile(directory);
		request.displayName = DeriveMainFolderName(m_Roots, directory);
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

	void ProjectTreePanel::renderToolbar()
	{
		if (ImGui::Button("+ Add Root..."))
		{
			Result<std::optional<Path>> picked = Platform::PickFolder({});
			if (picked && picked->has_value())
			{
				m_AddRootPendingPath = picked->value();
				const std::string defaultLabel = m_AddRootPendingPath.filename().String();
				std::snprintf(m_AddRootLabelBuffer.data(), m_AddRootLabelBuffer.size(), "%s", defaultLabel.c_str());
				m_AddRootPopupOpen = true;
				ImGui::OpenPopup("Add Project Root");
			}
		}

		renderAddRootPopup();
	}

	void ProjectTreePanel::renderAddRootPopup()
	{
		if (!ImGui::BeginPopupModal("Add Project Root", &m_AddRootPopupOpen, ImGuiWindowFlags_AlwaysAutoResize))
			return;

		ImGui::TextDisabled("%s", m_AddRootPendingPath.String().c_str());
		ImGui::SetNextItemWidth(280.0f);
		ImGui::InputText("Label", m_AddRootLabelBuffer.data(), m_AddRootLabelBuffer.size());

		const bool canAdd = m_AddRootLabelBuffer[0] != '\0';
		ImGui::BeginDisabled(!canAdd);
		if (ImGui::Button("Add"))
		{
			if (m_EventBus != nullptr)
			{
				ProjectEvents::RootAddRequested request;
				request.path = m_AddRootPendingPath;
				request.label = m_AddRootLabelBuffer.data();
				m_EventBus->Queue(request);
			}
			m_AddRootPopupOpen = false;
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndDisabled();

		ImGui::SameLine();
		if (ImGui::Button("Cancel"))
		{
			m_AddRootPopupOpen = false;
			ImGui::CloseCurrentPopup();
		}

		ImGui::EndPopup();
	}

	void ProjectTreePanel::renderRootSection(const ProjectRootEntry &section)
	{
		const std::string headerLabel =
			section.label + " (" + section.path.filename().String() + ")##root_" + section.id;
		const bool open = ImGui::CollapsingHeader(headerLabel.c_str(), ImGuiTreeNodeFlags_DefaultOpen);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("%s", section.path.String().c_str());
		renderRootSectionContextMenu(section);

		if (!open)
			return;

		ImGui::Indent();
		if (!FileSystem::Exists(section.path.Native()))
			ImGui::TextColored(
				ImVec4(0.95f, 0.35f, 0.35f, 1.0f), "Folder not found (mount disconnected?): %s", section.path.String().c_str());
		else
			renderDirectoryContents(section.path);
		ImGui::Unindent();
	}

	void ProjectTreePanel::renderRootSectionContextMenu(const ProjectRootEntry &section)
	{
		if (!ImGui::BeginPopupContextItem())
			return;

		if (ImGui::MenuItem("Change Folder..."))
		{
			Result<std::optional<Path>> picked = Platform::PickFolder(section.path);
			if (picked && picked->has_value() && m_EventBus != nullptr)
			{
				ProjectEvents::RootPathChangedRequested request;
				request.rootId = section.id;
				request.newPath = picked->value();
				m_EventBus->Queue(request);
			}
		}
		if (ImGui::MenuItem("Remove") && m_EventBus != nullptr)
		{
			ProjectEvents::RootRemoveRequested request;
			request.rootId = section.id;
			m_EventBus->Queue(request);
		}
		ImGui::EndPopup();
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
				if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && m_EventBus != nullptr)
				{
					ProjectEvents::TextFileOpenRequested request;
					request.path = entryPath;
					m_EventBus->Queue(request);
				}
				// WAVECAR = drag-drop onto an open structure's viewport (T08.6.4); drop target lives
				// in RendererPanel. POSCAR/CONTCAR deliberately stay context-menu-only (see "Open
				// Defect" above) - user explicitly rejected drag-drop for those, see TODO.md T08.6.4.
				if (label == "WAVECAR" && ImGui::BeginDragDropSource())
				{
					ImGui::SetDragDropPayload("DS_WAVECAR_PATH", pathKey.c_str(), pathKey.size() + 1);
					ImGui::TextUnformatted(pathKey.c_str());
					ImGui::EndDragDropSource();
				}
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
		if (ImGui::MenuItem("Set as Bulk Reference") && m_EventBus != nullptr)
		{
			ProjectEvents::BulkDirectoryChangeRequested request;
			request.directory = directory;
			m_EventBus->Queue(request);
		}
		ImGui::EndPopup();
	}
} // namespace DefectStudio
