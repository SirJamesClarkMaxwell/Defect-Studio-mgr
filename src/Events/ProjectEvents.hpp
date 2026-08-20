#pragma once

#include "Core/EventSystem/BusEventSystem/Event.hpp"
#include "Core/Utils/Path.hpp"

#include <string>

namespace DefectStudio::ProjectEvents
{
	// Queued from ProjectTreePanel's "+ Add Root..." popup - EditorLayer generates the persisted
	// id, spawns the new panel instance, and saves project_roots.yaml.
	struct RootAddRequested final : public BusEvent
	{
		Path path;
		std::string label;
	};

	// Queued from a tracked ProjectTreePanel instance's "Remove" button (only shown when the
	// instance has a persisted root id - ad-hoc clones from the generic View menu "Klonuj panel"
	// are session-only and never show this button).
	struct RootRemoveRequested final : public BusEvent
	{
		std::string rootId;
	};

	// Queued when "Pick Folder..." repoints an already-tracked root to a different path.
	struct RootPathChangedRequested final : public BusEvent
	{
		std::string rootId;
		Path newPath;
	};

	// Queued from ProjectTreePanel's per-node RMB "Set as Bulk Reference" (any folder, any depth -
	// same context menu as "Open Defect"). EditorLayer applies it to the live
	// ElectronicStructureSession immediately (works even with no project open) and persists it
	// into the active project's manifest if one exists.
	struct BulkDirectoryChangeRequested final : public BusEvent
	{
		Path directory;
	};
} // namespace DefectStudio::ProjectEvents
