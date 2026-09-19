#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "Core/JobSystem/JobSystemTypes.hpp"
#include "Core/Utils/Memory.hpp"
#include "Domain/Crystal/ElementProperties.hpp"
#include "Presentation/Panels/IPanel.hpp"
#include "Renderer/RendererLayer.hpp"

namespace DefectStudio
{
	class DomainLayer;
	class JobSystem;
	class CopyWindowStateJob;

	// The ordered registry-backed rows used by the label, arrow, and orbital outliner groups.
	// Planes deliberately bypass this path and walk RendererWindowState::scenePlanes directly.
	[[nodiscard]] std::vector<std::size_t> CollectSceneOutlinerSourceIndices(
		const SceneRegistry &scene, SceneObjectKind kind);

	// Lists open renderer windows (one structure per window - no Collections, see docs/work/
	// project/plans/2026-08-23-outliner-bonds-displacement.md). Every row carries Blender's two
	// visibility columns - the eye (drawn in the viewport, what H/Alt+H toggle) and the camera
	// (drawn in an exported render), see Presentation/Panels/SceneOutlinerVisibilityColumns.hpp -
	// a parent row applying its value to everything under it, and
	// name rename (double-click, or F2 on the last-clicked row - writes RendererWindowState::title,
	// which already drives the docked viewport window's own tab label). Expanding a window's tree
	// arrow lists its atoms grouped by species (a flat per-atom list doesn't scale to a supercell's
	// atom count, but its handful of distinct elements do) - a species row's own checkbox/arrow
	// mirrors the window row's, and an atom row click selects it in the viewport (Ctrl+click is
	// additive, matching RendererPanel::handleViewportPick).
	class SceneOutlinerPanel final : public IPanel
	{
	public:
		explicit SceneOutlinerPanel(
			RendererLayer &layer,
			WeakRef<DomainLayer> domainLayer,
			WeakRef<JobSystem> jobSystem,
			ElementPropertiesTable elementPropertiesTable,
			std::string title = "Scene Outliner",
			bool visibleByDefault = false);
		SceneOutlinerPanel(const SceneOutlinerPanel &other) = default;

		void Render() override;
		[[nodiscard]] PanelCategory GetCategory() const override { return PanelCategory::Scene; }
		[[nodiscard]] Ref<IPanel> Clone() const override;

	private:
		void drawSpeciesGroup(RendererWindowState &windowState, const std::string &species, const std::vector<std::size_t> &atomIndices);
		void drawAtomRow(
			RendererWindowState &windowState, std::size_t atomIndex,
			const std::vector<std::size_t> &orderedAtomIndices);
		// Free labels + pinned bond/angle measurements together ("Labels"), sceneArrows
		// ("Arrows") and sceneOrbitals ("Orbitals") - child groups under a window row, same nesting shape as
		// drawSpeciesGroup/drawAtomRow above, both visibility columns included. Row click selects in
		// the viewport the same way an atom row's does.
		void drawLabelsGroup(RendererWindowState &windowState);
		bool drawFreeLabelRow(
			RendererWindowState &windowState, std::size_t labelIndex,
			const std::vector<SceneObjectId> &orderedIds);
		void drawPinnedMeasurementRow(
			RendererWindowState &windowState, std::size_t pinIndex,
			const std::vector<SceneObjectId> &orderedIds);
		void drawArrowsGroup(RendererWindowState &windowState);
		bool drawSceneArrowRow(
			RendererWindowState &windowState, std::size_t arrowIndex,
			const std::vector<SceneObjectId> &orderedIds);
		void drawOrbitalsGroup(RendererWindowState &windowState);
		bool drawSceneOrbitalRow(
			RendererWindowState &windowState, std::size_t orbitalIndex,
			const std::vector<SceneObjectId> &orderedIds);
		// scenePlanes. Unlike the groups above it walks the vector directly: a plane has no
		// SceneRegistry entity, because nothing about it needs a transform or a selection component.
		void drawPlanesGroup(RendererWindowState &windowState);

		enum class SelectionRowKind
		{
			Atom,
			FreeLabel,
			PinnedMeasurement,
			Arrow,
			Orbital,
			Plane
		};
		struct SelectionAnchor
		{
			std::string windowId;
			SelectionRowKind kind = SelectionRowKind::Atom;
			std::size_t atomIndex = 0;
			SceneObjectId objectId;
		};
		void applyAtomRowSelection(
			RendererWindowState &windowState, const std::vector<std::size_t> &orderedRows,
			std::size_t clickedIndex);
		void applyAnnotationRowSelection(
			RendererWindowState &windowState, SelectionRowKind kind,
			const std::vector<SceneObjectId> &orderedRows, SceneObjectId clickedId,
			std::vector<SceneObjectId> &selection);
		void selectAnnotationRowForContextMenu(
			RendererWindowState &windowState, SelectionRowKind kind,
			SceneObjectId clickedId, std::vector<SceneObjectId> &selection);
		bool drawSceneObjectContextMenu(
			RendererWindowState &windowState, SelectionRowKind rowKind,
			SceneObjectId clickedId, std::vector<SceneObjectId> &selection,
			bool openRequested);

		// "Copy view + visibility to..." (RMB on a window row) - atom-matches source against target
		// (both already-open windows, unlike DisplacementComparisonPanel's file-based comparison) via
		// CopyWindowStateJob, then on completion copies the matched atoms' visibility and does an
		// animated camera transition to source's current view. Async (scipy subprocess), so dispatch
		// + poll like every other job-backed panel in this codebase.
		void dispatchCopyViewAndVisibility(const RendererWindowState &source, const std::string &targetWindowId);
		void pollCopyJob();
		// Esc: drop every window's selection - atoms, bonds and all five annotation kinds - so the
		// key means the same thing here as clicking empty space does in the viewport.
		void clearSelection();
		// Writes ImGuiConfigFlags_NavEnableKeyboard for the NEXT frame and records the focus it was
		// derived from. Called once at the end of Render() on every path, including the early one.
		void applyKeyboardNavFlag(bool enabled);

		RendererLayer &m_Layer;
		WeakRef<DomainLayer> m_DomainLayer;
		WeakRef<JobSystem> m_JobSystem;
		ElementPropertiesTable m_ElementPropertiesTable;
		Ref<CopyWindowStateJob> m_PendingCopyJob;
		JobId m_PendingCopyJobId = 0;
		std::string m_CopySourceWindowId;
		std::string m_CopyTargetWindowId;
		std::string m_CopyError;
		// Index into m_Layer.GetWindows(), not a stable windowId - fine since both editing state and
		// active-row tracking are cleared the moment the window list changes shape (see Render()).
		int m_EditingWindowIndex = -1;
		int m_ActiveWindowIndex = -1;
		bool m_JustStartedEditing = false;
		// Whether this panel held keyboard focus last frame - what gates ImGui's Nav flag, which is
		// read at NewFrame and so cannot be decided from this frame's focus.
		bool m_HadKeyboardFocus = false;
		std::optional<SelectionAnchor> m_SelectionAnchor;
		std::array<char, 128> m_EditingBuffer{};
	};
} // namespace DefectStudio
