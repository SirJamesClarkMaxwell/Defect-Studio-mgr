#include "Core/dspch.hpp"

#include "Presentation/Panels/ProjectTreePanel.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>

#include <imgui.h>
#include "IconsFontAwesome6.h"

#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/Notifications/NotificationEvents.hpp"
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

		// In-process clipboard for the tree panel's Copy/Cut/Paste (notes.txt pt. 18) - process-wide
		// static, same rationale as GetArrowStyleClipboard/GetLabelStyleClipboard (RendererLayer.hpp):
		// there is only ever one of this panel, but a static keeps the pattern identical to those and
		// needs no special handling if the panel is ever ImPanel::Clone()'d.
		struct TreeClipboardState
		{
			std::vector<Path> paths;
			bool isCut = false;
		};

		TreeClipboardState &GetTreeClipboard()
		{
			static TreeClipboardState state;
			return state;
		}

		// "name.ext" -> "name (copy).ext", then "name (copy 2).ext", etc. - used to pre-fill the
		// Paste Conflict popup's Rename field with something already guaranteed not to collide.
		[[nodiscard]] std::string SuggestNonConflictingName(const Path &directory, const std::string &originalName)
		{
			const std::filesystem::path original(originalName);
			const std::string stem = original.stem().string();
			const std::string extension = original.extension().string();
			for (int attempt = 1; attempt < 1000; ++attempt)
			{
				const std::string candidate = attempt == 1
					? stem + " (copy)" + extension
					: stem + " (copy " + std::to_string(attempt) + ")" + extension;
				if (!FileSystem::Exists((directory / candidate).Native()))
					return candidate;
			}
			return originalName; // pathological fallback (999 same-named conflicts) - essentially unreachable
		}

		// True when targetDirectory is sourcePath itself or lives underneath it - pasting/dropping
		// there would make std::filesystem::copy recursively copy a directory into a copy of itself
		// (grows without bound until a path-length error - reads as "app froze" for a few seconds,
		// 2026-08-29 bug report: Ctrl+C then Ctrl+V on the still-selected copied folder).
		[[nodiscard]] bool WouldNestIntoSelf(const Path &sourcePath, const Path &targetDirectory)
		{
			std::error_code error;
			const std::filesystem::path relative =
				std::filesystem::relative(targetDirectory.Native(), sourcePath.Native(), error);
			if (error || relative.empty())
				return false;
			// "." (same dir) or any component that isn't ".." means target is sourcePath itself or
			// nested inside it; a real ancestor/sibling relative path starts with "..".
			return relative.begin()->string() != "..";
		}

		// Inverse of the join done in renderDirectoryContents' buildDragPayload lambda below.
		[[nodiscard]] std::vector<Path> ParseDragPayloadPaths(const ImGuiPayload &payload)
		{
			std::vector<Path> paths;
			if (payload.DataSize <= 0)
				return paths;
			const std::string joined(static_cast<const char *>(payload.Data), static_cast<std::size_t>(payload.DataSize) - 1);
			std::size_t start = 0;
			while (start <= joined.size())
			{
				const std::size_t newline = joined.find('\n', start);
				const std::size_t end = newline == std::string::npos ? joined.size() : newline;
				if (end > start)
					paths.emplace_back(joined.substr(start, end - start));
				if (newline == std::string::npos)
					break;
				start = newline + 1;
			}
			return paths;
		}

		// Directories first, then alphabetical within each group - shared by renderDirectoryContents
		// (what actually draws) and collectVisibleEntries (what handleEntryClicked's Shift-click range
		// resolves against) so the two stay in the same order; a mismatch there would make Shift-click
		// select the wrong range even though both individually looked correct.
		void SortDirectoryEntries(std::vector<DirectoryEntryInfo> &entries)
		{
			std::sort(entries.begin(), entries.end(), [](const DirectoryEntryInfo &a, const DirectoryEntryInfo &b)
			{
				if (a.isDirectory != b.isDirectory)
					return a.isDirectory;
				return a.path.filename().string() < b.path.filename().string();
			});
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

	void ProjectTreePanel::RequestFilePick()
	{
		m_FilePickModeActive = true;
		SetVisible(true);
	}

	void ProjectTreePanel::confirmFilePick(const Path &filePath)
	{
		m_FilePickModeActive = false;
		if (m_EventBus == nullptr)
			return;
		ProjectEvents::DisplacementComparisonFilePicked event;
		event.filePath = filePath;
		m_EventBus->Queue(event);
	}

	void ProjectTreePanel::pushNotification(const std::string &message, bool isError)
	{
		if (m_EventBus == nullptr)
			return;
		m_EventBus->Queue(NotificationRequestedEvent{Notification{
			isError ? NotificationSeverity::Error : NotificationSeverity::Info,
			NotificationCategory::Project,
			"Project Tree",
			message,
			"ProjectTreePanel"}});
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
		if (m_FilePickModeActive)
		{
			ImGui::TextColored(
				ImVec4(0.95f, 0.7f, 0.2f, 1.0f), "Picking comparison file - Enter/click selects, Esc cancels");
		}
		ImGui::Separator();

		if (m_Roots.empty())
			ImGui::TextDisabled("No folders registered - use \"+ Add Root...\" above.");
		else
		{
			// Fresh and complete before anything else touches it this frame - lets both keyboard nav
			// and a Shift-click's range resolve immediately against accurate data instead of a stale
			// "last frame" list (see rebuildVisibleFlatList's declaration in the header for why this
			// replaced the old deferred-to-next-frame design).
			rebuildVisibleFlatList();
			if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
			{
				handleKeyboardNavigation();
				handleFileOpsKeyboardShortcuts();
			}
			for (const ProjectRootEntry &section : m_Roots)
				renderRootSection(section);
		}

		// ImGui::OpenPopup() must fire from here - the top level, never from inside another open
		// popup (a right-click context menu is itself a popup). Opening one popup while a sibling
		// popup is still open closes the new one before it ever shows, a well-known Dear ImGui
		// footgun; every trigger site below (context-menu items, F2/Del shortcuts) only sets these
		// bools now. Calling OpenPopup every frame a bool is true is documented-safe - harmless
		// no-op once the popup is already open.
		if (m_CreatePopupOpen)
		{
			ImGui::OpenPopup(m_CreatePopupKind == CreateEntryKind::Folder ? "New Folder"
				: m_CreatePopupKind == CreateEntryKind::File ? "New File" : "Create Defect");
		}
		if (m_RenamePopupOpen)
			ImGui::OpenPopup("Rename");
		if (m_DeleteConfirmPopupOpen)
			ImGui::OpenPopup("Delete");

		renderCreateEntryPopup();
		renderRenamePopup();
		renderDeleteConfirmPopup();
		renderPasteConflictPopup();
		processPasteQueue();

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
		if (m_FilePickModeActive && ImGui::IsKeyPressed(ImGuiKey_Escape))
		{
			m_FilePickModeActive = false;
			return;
		}

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

		const bool downPressed = ImGui::IsKeyPressed(ImGuiKey_DownArrow);
		const bool upPressed = ImGui::IsKeyPressed(ImGuiKey_UpArrow);
		if (downPressed || upPressed)
		{
			// The keyboard cursor (m_KeyboardCursorPath) is the row Up/Down actually moves - normally
			// the same row as m_SelectedPath (the anchor), but the two split apart during a Shift+Up/
			// Down run: the anchor stays put so the range keeps growing/shrinking from the same end,
			// same as Explorer/Finder's Shift+arrow range-select.
			int cursorIndex = -1;
			for (std::size_t index = 0; index < m_VisibleFlatList.size(); ++index)
			{
				if (m_VisibleFlatList[index].String() == m_KeyboardCursorPath)
				{
					cursorIndex = static_cast<int>(index);
					break;
				}
			}
			if (cursorIndex < 0)
				cursorIndex = selectedIndex; // no cursor yet this session - fall back to the anchor
			cursorIndex = std::clamp(
				cursorIndex + (downPressed ? 1 : -1), 0, static_cast<int>(m_VisibleFlatList.size()) - 1);
			m_KeyboardCursorPath = m_VisibleFlatList[cursorIndex].String();
			m_ScrollToSelectedPending = true;

			if (ImGui::GetIO().KeyShift)
			{
				if (m_SelectedPath.empty())
					m_SelectedPath = m_KeyboardCursorPath; // no anchor yet - the cursor becomes one
				const auto anchorIt = std::find_if(m_VisibleFlatList.begin(), m_VisibleFlatList.end(),
					[this](const Path &path) { return path.String() == m_SelectedPath; });
				if (anchorIt != m_VisibleFlatList.end())
				{
					const auto cursorIt = m_VisibleFlatList.begin() + cursorIndex;
					const auto [rangeBegin, rangeEnd] = std::minmax(anchorIt, cursorIt);
					m_SelectedPaths.clear();
					for (auto it = rangeBegin; it <= rangeEnd; ++it)
						m_SelectedPaths.push_back(it->String());
				}
			}
			else
			{
				m_SelectedPath = m_KeyboardCursorPath;
				m_SelectedPaths = {m_KeyboardCursorPath};
			}
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
				{
					m_SelectedPath = parentKey;
					m_SelectedPaths = {parentKey};
					m_KeyboardCursorPath = parentKey;
					m_ScrollToSelectedPending = true;
				}
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
			else if (m_FilePickModeActive)
				confirmFilePick(selected);
		}
	}

	void ProjectTreePanel::handleEntryClicked(const std::string &pathKey)
	{
		const ImGuiIO &io = ImGui::GetIO();
		if (io.KeyShift)
		{
			// The clicked row becomes the new keyboard cursor either way - a following Shift+Up/Down
			// continues extending from here, same as it would after a Shift+click in Explorer.
			m_KeyboardCursorPath = pathKey;
			if (m_SelectedPath.empty())
			{
				m_SelectedPath = pathKey;
				m_SelectedPaths = {pathKey};
				return;
			}

			const auto anchorIt = std::find_if(m_VisibleFlatList.begin(), m_VisibleFlatList.end(),
				[this](const Path &path) { return path.String() == m_SelectedPath; });
			const auto clickedIt = std::find_if(m_VisibleFlatList.begin(), m_VisibleFlatList.end(),
				[&pathKey](const Path &path) { return path.String() == pathKey; });
			if (anchorIt == m_VisibleFlatList.end() || clickedIt == m_VisibleFlatList.end())
			{
				// Anchor scrolled out of the visible/expanded set since it was set - fall back to a
				// plain single selection rather than guessing a range.
				m_SelectedPath = pathKey;
				m_SelectedPaths = {pathKey};
				return;
			}

			const auto [rangeBegin, rangeEnd] = std::minmax(anchorIt, clickedIt);
			m_SelectedPaths.clear();
			for (auto it = rangeBegin; it <= rangeEnd; ++it)
				m_SelectedPaths.push_back(it->String());
			// m_SelectedPath (the anchor) intentionally untouched - repeated Shift-clicks keep
			// extending from the same anchor, matching Explorer/Finder.
			return;
		}
		if (io.KeyCtrl)
		{
			const auto it = std::find(m_SelectedPaths.begin(), m_SelectedPaths.end(), pathKey);
			if (it != m_SelectedPaths.end())
				m_SelectedPaths.erase(it);
			else
				m_SelectedPaths.push_back(pathKey);
			m_SelectedPath = pathKey; // new anchor either way, matches Explorer
			m_KeyboardCursorPath = pathKey;
			return;
		}
		m_SelectedPath = pathKey;
		m_SelectedPaths = {pathKey};
		m_KeyboardCursorPath = pathKey;
	}

	void ProjectTreePanel::rebuildVisibleFlatList()
	{
		m_VisibleFlatList.clear();
		for (const ProjectRootEntry &section : m_Roots)
		{
			if (FileSystem::Exists(section.path.Native()))
				collectVisibleEntries(section.path);
		}
	}

	void ProjectTreePanel::collectVisibleEntries(const Path &directory)
	{
		std::vector<DirectoryEntryInfo> entries = FileSystem::ListDirectory(directory.Native());
		SortDirectoryEntries(entries);
		for (const DirectoryEntryInfo &entry : entries)
		{
			const Path entryPath(entry.path);
			m_VisibleFlatList.push_back(entryPath);
			if (entry.isDirectory)
			{
				const std::string pathKey = entryPath.String();
				if (m_ExpandedPaths.contains(pathKey) && m_ExpandedPaths[pathKey])
					collectVisibleEntries(entryPath);
			}
		}
	}

	void ProjectTreePanel::handleFileOpsKeyboardShortcuts()
	{
		if (m_FilePickModeActive)
			return; // picker mode owns Enter/Esc; don't also fire file-ops shortcuts underneath it

		const ImGuiIO &io = ImGui::GetIO();
		if (io.KeyCtrl && io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_D, false))
		{
			// "Create Defect" - toolbar/menu equivalent targets the selected directory (or its
			// parent, if a file is selected); no-op with nothing selected, same as those.
			if (!m_SelectedPath.empty())
			{
				const Path selected(m_SelectedPath);
				m_CreatePopupKind = CreateEntryKind::Defect;
				m_CreatePopupParent = FileSystem::IsDirectory(selected.Native()) ? selected : selected.parent_path();
				m_CreateNameBuffer[0] = '\0';
				m_CreatePopupOpen = true;
			}
		}
		else if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C, false))
		{
			beginCopyOrCut(false);
		}
		else if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_X, false))
		{
			beginCopyOrCut(true);
		}
		else if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V, false))
		{
			if (!m_SelectedPath.empty())
			{
				const Path selected(m_SelectedPath);
				const Path target = FileSystem::IsDirectory(selected.Native()) ? selected : selected.parent_path();
				beginPasteInto(target);
			}
		}
		else if (ImGui::IsKeyPressed(ImGuiKey_F2, false) && m_SelectedPaths.size() == 1)
		{
			m_RenamePopupTarget = Path(m_SelectedPaths.front());
			const std::string currentName = m_RenamePopupTarget.filename().String();
			std::snprintf(m_RenameNameBuffer.data(), m_RenameNameBuffer.size(), "%s", currentName.c_str());
			m_RenamePopupOpen = true;
		}
		else if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) && !m_SelectedPaths.empty())
		{
			m_DeleteConfirmTargets.clear();
			for (const std::string &path : m_SelectedPaths)
				m_DeleteConfirmTargets.emplace_back(Path(path));
			m_DeleteConfirmPopupOpen = true;
		}
	}

	void ProjectTreePanel::beginCopyOrCut(bool isCut)
	{
		if (m_SelectedPaths.empty())
			return;
		TreeClipboardState &clipboard = GetTreeClipboard();
		clipboard.paths.clear();
		for (const std::string &path : m_SelectedPaths)
			clipboard.paths.emplace_back(Path(path));
		clipboard.isCut = isCut;
	}

	void ProjectTreePanel::beginPasteInto(const Path &targetDirectory)
	{
		TreeClipboardState &clipboard = GetTreeClipboard();
		if (clipboard.paths.empty())
			return;

		for (const Path &source : clipboard.paths)
		{
			if (!FileSystem::Exists(source.Native()))
				continue; // source vanished since copy/cut - skip silently, matches Explorer
			if (WouldNestIntoSelf(source, targetDirectory))
			{
				pushNotification("Can't paste \"" + source.filename().String() + "\" inside itself.", true);
				continue;
			}
			m_PasteQueue.push_back(PendingPasteOperation{source, targetDirectory, clipboard.isCut});
		}
		// A cut is a one-shot move - once queued, the clipboard has nothing left to paste again
		// (a second Ctrl+V would otherwise try to move files that already moved).
		if (clipboard.isCut)
		{
			clipboard.paths.clear();
			clipboard.isCut = false;
		}
	}

	void ProjectTreePanel::processPasteQueue()
	{
		if (m_PasteConflictPopupOpen)
			return; // waiting on the user's Overwrite/Skip/Rename/Cancel choice for the front item

		while (!m_PasteQueue.empty())
		{
			const PendingPasteOperation &op = m_PasteQueue.front();
			const Path destination = op.destinationDirectory / op.source.filename().String();
			const bool conflicts = FileSystem::Exists(destination.Native());

			if (!conflicts)
			{
				std::error_code error;
				const bool ok = op.isCut ? FileSystem::Rename(op.source.Native(), destination.Native(), error)
										  : FileSystem::Copy(op.source.Native(), destination.Native(), error);
				if (!ok)
				{
					pushNotification(
						"Failed to " + std::string(op.isCut ? "move " : "copy ") + op.source.filename().String() +
							": " + error.message(),
						true);
				}
				m_PasteQueue.erase(m_PasteQueue.begin());
				continue;
			}

			if (m_PasteConflictApplyToAll && m_PasteConflictAppliedChoice != PasteConflictChoice::None)
			{
				if (m_PasteConflictAppliedChoice == PasteConflictChoice::Overwrite)
				{
					std::error_code error;
					const bool ok = op.isCut ? FileSystem::Rename(op.source.Native(), destination.Native(), error)
											  : FileSystem::Copy(op.source.Native(), destination.Native(), error);
					if (!ok)
					{
						pushNotification(
							"Failed to " + std::string(op.isCut ? "move " : "copy ") + op.source.filename().String() +
								": " + error.message(),
							true);
					}
				}
				// Skip: drop it without touching the filesystem.
				m_PasteQueue.erase(m_PasteQueue.begin());
				continue;
			}

			// Fresh, unresolved conflict - open the popup for this one item and stop draining until
			// a button click resolves it (renderPasteConflictPopup does the actual copy/skip/rename).
			m_PasteConflictPopupOpen = true;
			const std::string suggested = SuggestNonConflictingName(op.destinationDirectory, op.source.filename().String());
			std::snprintf(m_PasteConflictRenameBuffer.data(), m_PasteConflictRenameBuffer.size(), "%s", suggested.c_str());
			ImGui::OpenPopup("Paste Conflict");
			return;
		}

		m_PasteConflictApplyToAll = false;
		m_PasteConflictAppliedChoice = PasteConflictChoice::None;
	}

	void ProjectTreePanel::queueDroppedMove(const std::vector<Path> &sources, const Path &targetDirectory)
	{
		for (const Path &source : sources)
		{
			if (!FileSystem::Exists(source.Native()))
				continue;
			if (source.parent_path() == targetDirectory)
				continue; // dropped back where it already was - silent no-op, matches Explorer
			if (WouldNestIntoSelf(source, targetDirectory))
			{
				pushNotification("Can't move \"" + source.filename().String() + "\" inside itself.", true);
				continue;
			}
			m_PasteQueue.push_back(PendingPasteOperation{source, targetDirectory, /*isCut=*/true});
		}
	}

	bool ProjectTreePanel::modalButton(const char *label, int index, bool enabled)
	{
		const bool highlighted = m_ModalHighlightedButton == index;
		if (highlighted)
		{
			ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
		}
		ImGui::BeginDisabled(!enabled);
		const bool clicked = ImGui::Button(label);
		ImGui::EndDisabled();
		if (highlighted)
			ImGui::PopStyleColor(2);
		// Hovering a button also moves the keyboard cursor onto it - standard "mouse and keyboard
		// agree on what's focused" UX, and means the mouse never fights the last arrow-key press.
		if (enabled && ImGui::IsItemHovered())
			m_ModalHighlightedButton = index;
		const bool activatedByEnter = enabled && highlighted
			&& (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false));
		return clicked || activatedByEnter;
	}

	void ProjectTreePanel::renderToolbar()
	{
		if (ImGui::Button(ICON_FA_FOLDER_PLUS " Add Root..."))
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

		// VSCode-Explorer-style quick actions, right-aligned like VSCode's own Explorer toolbar
		// (2026-08-29 feedback: were left-aligned/small before). New Folder/New File/Create Defect
		// target the selected directory (or its parent, if a file is selected) - disabled with
		// nothing selected rather than guessing which of several registered roots the user means.
		// Bigger frame padding than the rest of the app's buttons ("trochę większe") - just for this
		// row, popped right after.
		struct ToolbarIcon
		{
			const char *icon;
			const char *id;
			const char *tooltip;
		};
		const ToolbarIcon icons[] = {
			{ICON_FA_FOLDER_PLUS, "##NewFolderToolbar", "New Folder"},
			{ICON_FA_FILE, "##NewFileToolbar", "New File"},
			{ICON_FA_ATOM, "##CreateDefectToolbar", "Create Defect... (Ctrl+Alt+D)"},
			{ICON_FA_COMPRESS, "##CollapseAll", "Collapse All"},
		};
		const ImVec2 iconPadding(8.0f, 6.0f);
		float totalWidth = 0.0f;
		for (const ToolbarIcon &entry : icons)
			totalWidth += ImGui::CalcTextSize(entry.icon).x + iconPadding.x * 2.0f;
		constexpr std::size_t iconCount = sizeof(icons) / sizeof(icons[0]);
		totalWidth += ImGui::GetStyle().ItemSpacing.x * static_cast<float>(iconCount - 1);

		ImGui::SameLine();
		const float rightEdge = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
		ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), rightEdge - totalWidth));
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, iconPadding);

		const bool hasCreateTarget = !m_SelectedPath.empty();
		ImGui::BeginDisabled(!hasCreateTarget);
		for (std::size_t index = 0; index < 3; ++index) // Folder/File/Defect - Collapse All below needs no target
		{
			const ToolbarIcon &entry = icons[index];
			if (index > 0)
				ImGui::SameLine();
			if (ImGui::Button((std::string(entry.icon) + entry.id).c_str()))
			{
				const Path selected(m_SelectedPath);
				m_CreatePopupKind = index == 0 ? CreateEntryKind::Folder
					: index == 1 ? CreateEntryKind::File : CreateEntryKind::Defect;
				m_CreatePopupParent = FileSystem::IsDirectory(selected.Native()) ? selected : selected.parent_path();
				m_CreateNameBuffer[0] = '\0';
				m_CreatePopupOpen = true;
			}
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("%s", entry.tooltip);
		}
		ImGui::EndDisabled();

		ImGui::SameLine();
		if (ImGui::Button((std::string(icons[3].icon) + icons[3].id).c_str()))
			m_ExpandedPaths.clear();
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("%s", icons[3].tooltip);

		ImGui::PopStyleVar();
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

	void ProjectTreePanel::renderCreateEntryPopup()
	{
		const char *title = m_CreatePopupKind == CreateEntryKind::Folder ? "New Folder"
			: m_CreatePopupKind == CreateEntryKind::File ? "New File" : "Create Defect";
		if (!ImGui::BeginPopupModal(title, &m_CreatePopupOpen, ImGuiWindowFlags_AlwaysAutoResize))
			return;
		if (ImGui::IsWindowAppearing())
			m_ModalHighlightedButton = 0; // "Create" - non-destructive, safe as the stray-Enter default

		ImGui::TextDisabled("%s", m_CreatePopupParent.String().c_str());
		if (m_CreatePopupKind == CreateEntryKind::Defect)
			ImGui::TextDisabled("Creates the folder plus stub POSCAR/KPOINTS inside it");
		ImGui::SetNextItemWidth(280.0f);
		const bool confirmedByEnter = ImGui::InputText(
			"Name", m_CreateNameBuffer.data(), m_CreateNameBuffer.size(), ImGuiInputTextFlags_EnterReturnsTrue);

		constexpr int buttonCount = 2;
		if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, false))
			m_ModalHighlightedButton = (m_ModalHighlightedButton - 1 + buttonCount) % buttonCount;
		if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, false))
			m_ModalHighlightedButton = (m_ModalHighlightedButton + 1) % buttonCount;

		const bool canCreate = m_CreateNameBuffer[0] != '\0';
		if (modalButton("Create", 0, canCreate) || (confirmedByEnter && canCreate))
		{
			const Path target = m_CreatePopupParent / std::string(m_CreateNameBuffer.data());
			std::error_code error;
			bool ok;
			if (m_CreatePopupKind == CreateEntryKind::File)
			{
				std::ofstream file(target.Native());
				ok = file.good();
			}
			else
			{
				ok = FileSystem::CreateDirectories(target.Native(), error);
				if (ok && m_CreatePopupKind == CreateEntryKind::Defect)
				{
					// Stub calc-input files - just POSCAR/KPOINTS for now, more will follow as this
					// workflow gets built out (2026-08-29 request); empty on purpose, this button
					// only scaffolds the folder shape.
					for (const char *fileName : {"POSCAR", "KPOINTS"})
					{
						std::ofstream stub(target.Native() / fileName);
						if (!stub.good())
							pushNotification("Failed to create " + target.String() + "/" + fileName, true);
					}
				}
			}
			if (!ok)
				pushNotification("Failed to create " + target.String() + (error ? ": " + error.message() : ""), true);
			m_CreatePopupOpen = false;
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (modalButton("Cancel", 1))
		{
			m_CreatePopupOpen = false;
			ImGui::CloseCurrentPopup();
		}

		ImGui::EndPopup();
	}

	void ProjectTreePanel::renderRenamePopup()
	{
		if (!ImGui::BeginPopupModal("Rename", &m_RenamePopupOpen, ImGuiWindowFlags_AlwaysAutoResize))
			return;
		if (ImGui::IsWindowAppearing())
			m_ModalHighlightedButton = 0; // "Rename" - matches the New-Folder/File popup's default

		ImGui::TextDisabled("%s", m_RenamePopupTarget.String().c_str());
		ImGui::SetNextItemWidth(280.0f);
		const bool confirmedByEnter = ImGui::InputText(
			"New name", m_RenameNameBuffer.data(), m_RenameNameBuffer.size(), ImGuiInputTextFlags_EnterReturnsTrue);

		constexpr int buttonCount = 2;
		if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, false))
			m_ModalHighlightedButton = (m_ModalHighlightedButton - 1 + buttonCount) % buttonCount;
		if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, false))
			m_ModalHighlightedButton = (m_ModalHighlightedButton + 1) % buttonCount;

		const bool canRename = m_RenameNameBuffer[0] != '\0';
		if (modalButton("Rename", 0, canRename) || (confirmedByEnter && canRename))
		{
			const Path destination = m_RenamePopupTarget.parent_path() / std::string(m_RenameNameBuffer.data());
			std::error_code error;
			if (!FileSystem::Rename(m_RenamePopupTarget.Native(), destination.Native(), error))
			{
				pushNotification(
					"Failed to rename " + m_RenamePopupTarget.filename().String() + ": " + error.message(), true);
			}
			else
			{
				const std::string oldKey = m_RenamePopupTarget.String();
				if (m_SelectedPath == oldKey)
					m_SelectedPath = destination.String();
				if (m_KeyboardCursorPath == oldKey)
					m_KeyboardCursorPath = destination.String();
				std::replace(m_SelectedPaths.begin(), m_SelectedPaths.end(), oldKey, destination.String());
			}
			m_RenamePopupOpen = false;
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (modalButton("Cancel", 1))
		{
			m_RenamePopupOpen = false;
			ImGui::CloseCurrentPopup();
		}

		ImGui::EndPopup();
	}

	void ProjectTreePanel::renderDeleteConfirmPopup()
	{
		if (!ImGui::BeginPopupModal("Delete", &m_DeleteConfirmPopupOpen, ImGuiWindowFlags_AlwaysAutoResize))
			return;
		if (ImGui::IsWindowAppearing())
			m_ModalHighlightedButton = 1; // "Cancel" - destructive action, don't make Delete the stray-Enter default

		ImGui::Text("Delete %zu item(s)? This cannot be undone.", m_DeleteConfirmTargets.size());
		for (const Path &target : m_DeleteConfirmTargets)
			ImGui::BulletText("%s", target.String().c_str());

		constexpr int buttonCount = 2;
		if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, false))
			m_ModalHighlightedButton = (m_ModalHighlightedButton - 1 + buttonCount) % buttonCount;
		if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, false))
			m_ModalHighlightedButton = (m_ModalHighlightedButton + 1) % buttonCount;

		if (modalButton(ICON_FA_TRASH_CAN " Delete", 0))
		{
			for (const Path &target : m_DeleteConfirmTargets)
			{
				std::error_code error;
				if (FileSystem::IsDirectory(target.Native()))
					FileSystem::RemoveAll(target.Native(), error);
				else
					FileSystem::Remove(target.Native(), error);
				if (error)
					pushNotification("Failed to delete " + target.filename().String() + ": " + error.message(), true);
			}
			m_SelectedPath.clear();
			m_SelectedPaths.clear();
			m_KeyboardCursorPath.clear();
			m_DeleteConfirmTargets.clear();
			m_DeleteConfirmPopupOpen = false;
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (modalButton("Cancel", 1))
		{
			m_DeleteConfirmTargets.clear();
			m_DeleteConfirmPopupOpen = false;
			ImGui::CloseCurrentPopup();
		}

		ImGui::EndPopup();
	}

	void ProjectTreePanel::renderPasteConflictPopup()
	{
		if (!ImGui::BeginPopupModal("Paste Conflict", &m_PasteConflictPopupOpen, ImGuiWindowFlags_AlwaysAutoResize))
			return;
		if (m_PasteQueue.empty())
		{
			m_PasteConflictPopupOpen = false;
			ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
			return;
		}

		if (ImGui::IsWindowAppearing())
			m_ModalHighlightedButton = 1; // "Skip" - the non-destructive, non-disruptive middle ground

		const PendingPasteOperation op = m_PasteQueue.front();
		const Path destination = op.destinationDirectory / op.source.filename().String();
		ImGui::Text("\"%s\" already exists at the destination.", op.source.filename().String().c_str());

		if (m_PasteQueue.size() > 1)
			ImGui::Checkbox("Apply to all remaining conflicts", &m_PasteConflictApplyToAll);

		constexpr int buttonCount = 3; // Overwrite/Skip/Cancel Paste - Rename&&Paste isn't part of this cycle, see below
		if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, false))
			m_ModalHighlightedButton = (m_ModalHighlightedButton - 1 + buttonCount) % buttonCount;
		if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, false))
			m_ModalHighlightedButton = (m_ModalHighlightedButton + 1) % buttonCount;

		if (modalButton("Overwrite", 0))
		{
			std::error_code error;
			const bool ok = op.isCut ? FileSystem::Rename(op.source.Native(), destination.Native(), error)
									  : FileSystem::Copy(op.source.Native(), destination.Native(), error);
			if (!ok)
			{
				pushNotification(
					"Failed to " + std::string(op.isCut ? "move " : "copy ") + op.source.filename().String() + ": " +
						error.message(),
					true);
			}
			m_PasteQueue.erase(m_PasteQueue.begin());
			if (m_PasteConflictApplyToAll)
				m_PasteConflictAppliedChoice = PasteConflictChoice::Overwrite;
			m_PasteConflictPopupOpen = false;
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (modalButton("Skip", 1))
		{
			m_PasteQueue.erase(m_PasteQueue.begin());
			if (m_PasteConflictApplyToAll)
				m_PasteConflictAppliedChoice = PasteConflictChoice::Skip;
			m_PasteConflictPopupOpen = false;
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (modalButton("Cancel Paste", 2))
		{
			m_PasteQueue.clear();
			m_PasteConflictPopupOpen = false;
			ImGui::CloseCurrentPopup();
		}

		// Rename only ever applies to this one item - renaming every conflicting item in a batch
		// paste at once isn't offered (each would need its own distinct name typed in anyway).
		ImGui::Separator();
		ImGui::SetNextItemWidth(240.0f);
		ImGui::InputText("##PasteConflictRename", m_PasteConflictRenameBuffer.data(), m_PasteConflictRenameBuffer.size());
		ImGui::SameLine();
		const bool canRename = m_PasteConflictRenameBuffer[0] != '\0';
		ImGui::BeginDisabled(!canRename);
		if (ImGui::Button("Rename && Paste"))
		{
			const Path renamedDestination = op.destinationDirectory / std::string(m_PasteConflictRenameBuffer.data());
			std::error_code error;
			const bool ok = op.isCut ? FileSystem::Rename(op.source.Native(), renamedDestination.Native(), error)
									  : FileSystem::Copy(op.source.Native(), renamedDestination.Native(), error);
			if (!ok)
			{
				pushNotification(
					"Failed to paste " + op.source.filename().String() + ": " + error.message(), true);
			}
			m_PasteQueue.erase(m_PasteQueue.begin());
			m_PasteConflictPopupOpen = false;
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndDisabled();

		ImGui::EndPopup();
	}

	void ProjectTreePanel::renderRootSection(const ProjectRootEntry &section)
	{
		const std::string headerLabel =
			ICON_FA_FOLDER " " + section.label + " (" + section.path.filename().String() + ")##root_" + section.id;
		const bool open = ImGui::CollapsingHeader(headerLabel.c_str(), ImGuiTreeNodeFlags_DefaultOpen);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("%s", section.path.String().c_str());
		renderRootSectionContextMenu(section);
		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("DS_TREE_ENTRY_PATHS"))
				queueDroppedMove(ParseDragPayloadPaths(*payload), section.path);
			ImGui::EndDragDropTarget();
		}

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

		// A root is a registered pointer to a folder (ProjectRootsIO), not itself a filesystem entry
		// - only the container operations (New Folder/File, Paste) apply here; Copy/Cut/Rename/
		// Delete stay scoped to real entries inside it (renderFileOpsMenuItems isRealEntry=false).
		renderFileOpsMenuItems(section.path, /*isContainer=*/true, /*isRealEntry=*/false);
		ImGui::Separator();

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

	void ProjectTreePanel::renderFileOpsMenuItems(const Path &entryPath, bool isContainer, bool isRealEntry)
	{
		if (isContainer)
		{
			if (ImGui::MenuItem(ICON_FA_FOLDER_PLUS " New Folder..."))
			{
				m_CreatePopupKind = CreateEntryKind::Folder;
				m_CreatePopupParent = entryPath;
				m_CreateNameBuffer[0] = '\0';
				m_CreatePopupOpen = true;
			}
			if (ImGui::MenuItem(ICON_FA_FILE " New File..."))
			{
				m_CreatePopupKind = CreateEntryKind::File;
				m_CreatePopupParent = entryPath;
				m_CreateNameBuffer[0] = '\0';
				m_CreatePopupOpen = true;
			}
			if (ImGui::MenuItem(ICON_FA_ATOM " Create Defect...", "Ctrl+Alt+D"))
			{
				m_CreatePopupKind = CreateEntryKind::Defect;
				m_CreatePopupParent = entryPath;
				m_CreateNameBuffer[0] = '\0';
				m_CreatePopupOpen = true;
			}
			const bool canPaste = !GetTreeClipboard().paths.empty();
			if (ImGui::MenuItem(ICON_FA_PASTE " Paste", "Ctrl+V", false, canPaste))
				beginPasteInto(entryPath);
		}

		if (isRealEntry)
		{
			if (isContainer)
				ImGui::Separator();
			const bool hasSelection = !m_SelectedPaths.empty();
			if (ImGui::MenuItem(ICON_FA_COPY " Copy", "Ctrl+C", false, hasSelection))
				beginCopyOrCut(false);
			if (ImGui::MenuItem(ICON_FA_SCISSORS " Cut", "Ctrl+X", false, hasSelection))
				beginCopyOrCut(true);
			if (ImGui::MenuItem(ICON_FA_PEN_TO_SQUARE " Rename...", "F2", false, m_SelectedPaths.size() == 1))
			{
				m_RenamePopupTarget = Path(m_SelectedPaths.front());
				const std::string currentName = m_RenamePopupTarget.filename().String();
				std::snprintf(m_RenameNameBuffer.data(), m_RenameNameBuffer.size(), "%s", currentName.c_str());
				m_RenamePopupOpen = true;
			}
			if (ImGui::MenuItem(ICON_FA_TRASH_CAN " Delete", "Del", false, hasSelection))
			{
				m_DeleteConfirmTargets.clear();
				for (const std::string &path : m_SelectedPaths)
					m_DeleteConfirmTargets.emplace_back(Path(path));
				m_DeleteConfirmPopupOpen = true;
			}
		}
	}

	void ProjectTreePanel::renderDirectoryContents(const Path &directory)
	{
		std::vector<DirectoryEntryInfo> entries = FileSystem::ListDirectory(directory.Native());
		SortDirectoryEntries(entries);

		const TreeClipboardState &clipboard = GetTreeClipboard();

		// Drag payload for a row: the whole current multi-selection (newline-joined - see
		// ParseDragPayloadPaths) if the dragged row is part of it, otherwise just that one row -
		// matches Explorer (dragging a non-selected item drags only it).
		const auto buildDragPayload = [this](const std::string &draggedPath) -> std::string
		{
			if (std::find(m_SelectedPaths.begin(), m_SelectedPaths.end(), draggedPath) == m_SelectedPaths.end())
				return draggedPath;
			std::string joined;
			for (const std::string &path : m_SelectedPaths)
			{
				if (!joined.empty())
					joined += '\n';
				joined += path;
			}
			return joined;
		};

		for (const DirectoryEntryInfo &entry : entries)
		{
			const Path entryPath(entry.path);
			const std::string pathKey = entryPath.String();
			const bool isSelected =
				std::find(m_SelectedPaths.begin(), m_SelectedPaths.end(), pathKey) != m_SelectedPaths.end();
			// Dims a pending Cut's source until it's actually moved by a Paste (or the cut is
			// replaced by a new Copy/Cut) - same "greyed out until moved" convention Explorer uses.
			const bool isCutPending =
				clipboard.isCut && std::find(clipboard.paths.begin(), clipboard.paths.end(), entryPath) != clipboard.paths.end();

			const std::string label = entry.path.filename().string();

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
			if (isCutPending)
				ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);

			if (!entry.isDirectory)
			{
				const std::string iconLabel = std::string(ICON_FA_FILE_LINES) + " " + label;
				const std::string idLabel = iconLabel + "##" + entry.path.string();
				ImGuiTreeNodeFlags leafFlags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen
					| ImGuiTreeNodeFlags_SpanFullWidth;
				if (isSelected)
					leafFlags |= ImGuiTreeNodeFlags_Selected;
				ImGui::TreeNodeEx(idLabel.c_str(), leafFlags, "%s", iconLabel.c_str());
				if (pathKey == m_KeyboardCursorPath && m_ScrollToSelectedPending)
				{
					ImGui::SetScrollHereY(0.5f);
					m_ScrollToSelectedPending = false;
				}
				if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && !isSelected)
					handleEntryClicked(pathKey);
				if (ImGui::IsItemClicked())
				{
					handleEntryClicked(pathKey);
					if (m_FilePickModeActive)
						confirmFilePick(entryPath);
				}
				if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && m_EventBus != nullptr)
				{
					ProjectEvents::TextFileOpenRequested request;
					request.path = entryPath;
					m_EventBus->Queue(request);
				}
				// General drag source (notes.txt pt. 18 follow-up: drag-and-drop between folders) -
				// payload is this row, or the whole multi-selection if it's part of one (see
				// buildDragPayload above). WAVECAR additionally gets its own dedicated payload
				// alongside the general one - drop target for that lives in RendererPanel (T08.6.4),
				// unrelated to the tree's own move-on-drop handling below. POSCAR/CONTCAR still don't
				// get that second payload (see "Open Defect" above; TODO.md T08.6.4), but do
				// participate in a plain move like any other file now.
				if (!isCutPending && ImGui::BeginDragDropSource())
				{
					const std::string joinedPaths = buildDragPayload(pathKey);
					ImGui::SetDragDropPayload("DS_TREE_ENTRY_PATHS", joinedPaths.c_str(), joinedPaths.size() + 1);
					if (label == "WAVECAR")
						ImGui::SetDragDropPayload("DS_WAVECAR_PATH", pathKey.c_str(), pathKey.size() + 1);
					ImGui::TextUnformatted(label.c_str());
					ImGui::EndDragDropSource();
				}
				renderFileContextMenu(entryPath);
				if (isCutPending)
					ImGui::PopStyleVar();
				ImGui::PopStyleColor(pushedColors);
				continue;
			}

			// Source of truth going into the frame is our own map (so keyboard Left/Right/Enter can
			// drive it); whatever TreeNodeEx actually returns (arrow-click, double-click, or our own
			// forced value) is written straight back so next frame's map read matches reality.
			const bool wasOpen = m_ExpandedPaths.contains(pathKey) && m_ExpandedPaths[pathKey];
			const std::string iconLabel = std::string(wasOpen ? ICON_FA_FOLDER_OPEN : ICON_FA_FOLDER) + " " + label;
			const std::string idLabel = iconLabel + "##" + entry.path.string();
			ImGui::SetNextItemOpen(wasOpen);
			ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanFullWidth | ImGuiTreeNodeFlags_OpenOnArrow
				| ImGuiTreeNodeFlags_OpenOnDoubleClick;
			if (isSelected)
				flags |= ImGuiTreeNodeFlags_Selected;
			const bool open = ImGui::TreeNodeEx(idLabel.c_str(), flags, "%s", iconLabel.c_str());
			if (pathKey == m_KeyboardCursorPath && m_ScrollToSelectedPending)
			{
				ImGui::SetScrollHereY(0.5f);
				m_ScrollToSelectedPending = false;
			}
			if (isCutPending)
				ImGui::PopStyleVar();
			ImGui::PopStyleColor(pushedColors);
			if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && !isSelected)
				handleEntryClicked(pathKey);
			if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
				handleEntryClicked(pathKey);
			m_ExpandedPaths[pathKey] = open;
			renderDirectoryContextMenu(entryPath);
			if (!isCutPending && ImGui::BeginDragDropSource())
			{
				const std::string joinedPaths = buildDragPayload(pathKey);
				ImGui::SetDragDropPayload("DS_TREE_ENTRY_PATHS", joinedPaths.c_str(), joinedPaths.size() + 1);
				ImGui::TextUnformatted(label.c_str());
				ImGui::EndDragDropSource();
			}
			if (ImGui::BeginDragDropTarget())
			{
				if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("DS_TREE_ENTRY_PATHS"))
					queueDroppedMove(ParseDragPayloadPaths(*payload), entryPath);
				ImGui::EndDragDropTarget();
			}
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
		if (ImGui::MenuItem("Show Calculation Summary") && m_EventBus != nullptr)
		{
			ProjectEvents::CalculationSummaryOpenRequested request;
			request.directory = directory;
			m_EventBus->Queue(request);
		}
		if (ImGui::MenuItem("Set as Displacement Comparison", nullptr, false, hasDefectFile) && m_EventBus != nullptr)
		{
			ProjectEvents::DisplacementComparisonFileRequested request;
			request.filePath = ResolveDefectFile(directory);
			m_EventBus->Queue(request);
		}

		ImGui::Separator();
		renderFileOpsMenuItems(directory, /*isContainer=*/true, /*isRealEntry=*/true);

		ImGui::EndPopup();
	}

	void ProjectTreePanel::renderFileContextMenu(const Path &filePath)
	{
		if (!ImGui::BeginPopupContextItem())
			return;

		renderFileOpsMenuItems(filePath, /*isContainer=*/false, /*isRealEntry=*/true);

		ImGui::EndPopup();
	}
} // namespace DefectStudio
