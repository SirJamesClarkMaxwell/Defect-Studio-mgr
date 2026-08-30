#pragma once

#include <optional>
#include <string>

#include "Core/JobSystem/JobSystemTypes.hpp"
#include "Core/Utils/Memory.hpp"
#include "Core/Utils/Path.hpp"
#include "Domain/Crystal/ElementProperties.hpp"
#include "Presentation/Panels/IPanel.hpp"

namespace DefectStudio
{
	class EventBus;
	class JobSystem;
	class DomainLayer;
	class RendererLayer;
	class CompareStructuresJob;

	// Atoms-displacement comparison (T08 item 0 / T16 item 8, docs/work/project/plans/
	// 2026-08-24-calc-tools.md sections 10+12): picks a "comparison" structure file, matches its
	// atoms against the currently-focused renderer window's structure (the "reference"), and draws
	// displacement arrows into that reference window's viewport
	// (OpenGlRendererBackend::renderDisplacementArrows). One panel instance, no tabs - TextEditorPanel-
	// style. The reference is whichever window was focused when "Compare" was last clicked (not
	// re-derived from focus every frame - see m_TargetWindowId), so the panel stays bound to one
	// comparison even if the user clicks into another viewport afterwards.
	class DisplacementComparisonPanel final : public IPanel
	{
	public:
		explicit DisplacementComparisonPanel(
			RendererLayer &rendererLayer,
			WeakRef<DomainLayer> domainLayer,
			WeakRef<JobSystem> jobSystem,
			Ref<EventBus> eventBus,
			ElementPropertiesTable elementPropertiesTable,
			std::string title = "Atoms Displacement",
			bool visibleByDefault = false);
		DisplacementComparisonPanel(const DisplacementComparisonPanel &other) = default;

		void Render() override;
		[[nodiscard]] Ref<IPanel> Clone() const override;

		// Prefills the comparison file path + last threshold from the just-opened project's manifest
		// (EditorLayer::openProject) - does NOT dispatch a comparison (the reference window may not
		// even be open yet at project-load time). The threshold is applied once, to whichever
		// comparison next completes, then forgotten (not sticky across every future Compare).
		void SetPersistedState(Path comparisonFilePath, float thresholdAngstrom);

		// ProjectTreePanel's per-directory RMB "Set as Displacement Comparison" - picking the
		// comparison file by browsing the in-app project tree instead of an OS file dialog (which is
		// painful to navigate to a mounted-drive path). Makes the panel visible; does not touch
		// m_TargetWindowId or dispatch a comparison - the user still picks/confirms the reference
		// window separately (via focus + Compare, or the vertical-toolbar button below).
		void SetComparisonFile(Path filePath);

		// This window's vertical-toolbar quick-launch button (RendererPanel::drawViewportVerticalToolbar)
		// - pins windowId as the reference for the next Compare and makes the panel visible. Overrides
		// whatever GetLastFocusedViewportWindowId() would otherwise resolve to in dispatchCompare(),
		// same "explicit binding wins" reasoning as m_TargetWindowId already documents above.
		void OpenForWindow(const std::string &windowId);

	private:
		void dispatchCompare();
		void pollJob();
		void publishPersistedStateChanged();

		RendererLayer &m_RendererLayer;
		WeakRef<DomainLayer> m_DomainLayer;
		WeakRef<JobSystem> m_JobSystem;
		Ref<EventBus> m_EventBus;
		ElementPropertiesTable m_ElementPropertiesTable;

		std::string m_TargetWindowId;
		Path m_ComparisonFilePath;
		std::optional<float> m_PersistedThresholdAngstrom;
		Ref<CompareStructuresJob> m_PendingJob;
		JobId m_PendingJobId = 0;
		std::string m_Error;
	};
} // namespace DefectStudio
