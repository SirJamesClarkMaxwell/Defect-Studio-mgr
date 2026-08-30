#pragma once

#include "Core/EventSystem/BusEventSystem/Event.hpp"
#include "Core/Utils/Path.hpp"

#include <string>
#include <utility>
#include <vector>

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

	// Queued from ProjectTreePanel's leaf-node double-click (any file, no extension gating -
	// POSCAR/CONTCAR keep their separate RMB "Open Defect" path, no conflict). EditorLayer routes
	// it to the single TextEditorPanel instance.
	struct TextFileOpenRequested final : public BusEvent
	{
		Path path;
	};

	// Queued from ProjectTreePanel's per-node RMB "Show Calculation Summary" (same context menu as
	// "Set as Bulk Reference"/"Open Defect"). EditorLayer routes it to the single
	// CalculationSummaryPanel instance (TextEditorPanel-style: opening a new directory replaces
	// whatever was open, no tabs - see CalculationSummaryPanel's class comment).
	struct CalculationSummaryOpenRequested final : public BusEvent
	{
		Path directory;
	};

	// Queued from ElectronicStructurePanel's custom irrep-label editor (add/remove/edit a row).
	// EditorLayer applies the whole list to ElectronicStructureSession immediately (works even with
	// no project open) and persists it into the active project's manifest if one exists - same
	// shape as BulkDirectoryChangeRequested.
	struct IrrepLabelOverridesChanged final : public BusEvent
	{
		std::vector<std::pair<std::string, std::string>> overrides;
	};

	// Queued from ProjectTreePanel's per-directory RMB "Set as Displacement Comparison" (same context
	// menu as "Open Defect"/"Set as Bulk Reference"/"Show Calculation Summary", same
	// ResolveDefectFile(directory) file resolution "Open Defect" uses) - EditorLayer routes it to the
	// single DisplacementComparisonPanel instance (DisplacementComparisonPanel::SetComparisonFile).
	// Exists because navigating an OS file dialog to a file already visible in the project tree
	// (especially on a mounted network drive) is slower than just picking it from the tree.
	struct DisplacementComparisonFileRequested final : public BusEvent
	{
		Path filePath;
	};

	// Queued from DisplacementComparisonPanel whenever the comparison file changes, or the display
	// threshold slider settles (on release, not every frame it moves). EditorLayer persists both
	// into the active project's manifest if one exists - same shape as IrrepLabelOverridesChanged.
	struct DisplacementComparisonStateChanged final : public BusEvent
	{
		Path comparisonFilePath;
		float thresholdAngstrom = 0.0f;
	};

	// Queued from DisplacementComparisonPanel's "Browse..." (in-app) button - EditorLayer routes it
	// to the single ProjectTreePanel instance (ProjectTreePanel::RequestFilePick), arming picker
	// mode: arrow keys navigate, Enter (or a single click) on a file confirms and fires
	// DisplacementComparisonFilePicked below, Esc cancels. Exists because the per-directory RMB
	// "Set as Displacement Comparison" only resolves CONTCAR/POSCAR - a comparison file with a
	// different name (or not the "defect file" in its directory) still needed an OS dialog before
	// this, defeating the whole point of picking from the tree (2026-08-28 feedback).
	struct DisplacementComparisonFilePickRequested final : public BusEvent
	{
	};

	// Queued from ProjectTreePanel when a file is confirmed while picker mode (armed by
	// DisplacementComparisonFilePickRequested above) is active.
	struct DisplacementComparisonFilePicked final : public BusEvent
	{
		Path filePath;
	};
} // namespace DefectStudio::ProjectEvents
