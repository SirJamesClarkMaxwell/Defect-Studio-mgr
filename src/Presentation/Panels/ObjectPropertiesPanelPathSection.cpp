#include "Core/dspch.hpp"

#include "Presentation/Panels/ObjectPropertiesPanelSections.hpp"

#include <numbers>

#include <imgui.h>

#include "Core/Logging/Logger.hpp"
#include "Presentation/Panels/SceneObjectEditActions.hpp"
#include "Presentation/Panels/ScenePathEditorWidget.hpp"
#include "Presentation/Panels/ScenePathOperations.hpp"

namespace DefectStudio
{
	namespace
	{
		template <typename T>
		bool ReportPathEditResult(const Result<T> &result)
		{
			if (result)
				return true;
			DS_LOG_WARN("Path edit failed: {}", result.Error().technicalDetails);
			return false;
		}

		void DrawPathEditActions(RendererWindowState &windowState)
		{
			if (!windowState.pathEdit.IsActive())
				return;

			ImGui::SeparatorText("Path Edit");
			ImGui::TextDisabled("1 Nodes/Handles   2 Segments   3 Whole path");
			ImGui::TextDisabled("E Extend   Delete Remove nodes   V Handle type");

			if (ImGui::Button("Extend endpoint"))
				ReportPathEditResult(ExtendSelectedScenePathEnd(windowState));
			ImGui::SameLine();
			if (ImGui::Button("Insert midpoint"))
				ReportPathEditResult(InsertSelectedScenePathSegment(windowState));
			if (ImGui::Button("Delete nodes"))
				ReportPathEditResult(DeleteSelectedScenePathNodes(windowState));
			ImGui::SameLine();
			if (ImGui::Button("Reverse path"))
				ReportPathEditResult(ReverseEditedScenePath(windowState));

			ImGui::TextUnformatted("Bezier handle type");
			for (const auto &[label, type] : {
				std::pair{"Free", BezierHandleType::Free},
				std::pair{"Aligned", BezierHandleType::Aligned},
				std::pair{"Vector", BezierHandleType::Vector},
				std::pair{"Auto", BezierHandleType::Auto}})
			{
				if (ImGui::SmallButton(label))
					ReportPathEditResult(SetSelectedScenePathHandleType(windowState, type));
				if (type != BezierHandleType::Auto)
					ImGui::SameLine();
			}

			const Result<PathArcParameters> resolved = ResolveSelectedScenePathArc(windowState);
			if (!resolved)
				return;

			ImGui::SeparatorText("Circular arc");
			PathArcParameters edited = resolved.Value();
			constexpr double kRadiansPerDegree = std::numbers::pi_v<double> / 180.0;
			double startDegrees = edited.startAngleRadians / kRadiansPerDegree;
			double sweepDegrees = edited.signedSweepRadians / kRadiansPerDegree;
			constexpr ImGuiInputTextFlags commitOnEnter = ImGuiInputTextFlags_EnterReturnsTrue;
			bool commit = false;
			commit |= ImGui::InputDouble("Center X", &edited.center.x, 0.0, 0.0, "%.9g", commitOnEnter);
			commit |= ImGui::InputDouble("Center Y", &edited.center.y, 0.0, 0.0, "%.9g", commitOnEnter);
			commit |= ImGui::InputDouble("Center Z", &edited.center.z, 0.0, 0.0, "%.9g", commitOnEnter);
			commit |= ImGui::InputDouble("Axis X", &edited.axis.x, 0.0, 0.0, "%.9g", commitOnEnter);
			commit |= ImGui::InputDouble("Axis Y", &edited.axis.y, 0.0, 0.0, "%.9g", commitOnEnter);
			commit |= ImGui::InputDouble("Axis Z", &edited.axis.z, 0.0, 0.0, "%.9g", commitOnEnter);
			commit |= ImGui::InputDouble("Radius", &edited.radius, 0.0, 0.0, "%.9g", commitOnEnter);
			const bool startCommitted = ImGui::InputDouble(
				"Start angle (deg)", &startDegrees, 0.0, 0.0, "%.9g", commitOnEnter);
			const bool sweepCommitted = ImGui::InputDouble(
				"Sweep (deg)", &sweepDegrees, 0.0, 0.0, "%.9g", commitOnEnter);
			edited.startAngleRadians = startDegrees * kRadiansPerDegree;
			edited.signedSweepRadians = sweepDegrees * kRadiansPerDegree;
			commit |= startCommitted || sweepCommitted;
			if (commit)
				ReportPathEditResult(ApplySelectedScenePathArc(windowState, edited));
			ImGui::TextDisabled("Press Enter to commit an exact value.");
		}
	} // namespace

	void DrawSelectedScenePathSection(RendererWindowState &windowState)
	{
		ImGui::Separator();
		DrawScenePathTransformEditor(windowState);
		DrawScenePathEditor(windowState);
		DrawPathEditActions(windowState);
		if (ImGui::Button("Delete##SelectedPath"))
			ExecuteSceneObjectEditAction(windowState, SceneObjectEditKind::Path, SceneObjectEditAction::Delete);
		ImGui::SameLine();
		if (ImGui::Button("Duplicate##SelectedPath"))
			ExecuteSceneObjectEditAction(windowState, SceneObjectEditKind::Path, SceneObjectEditAction::Duplicate);
		ImGui::SameLine();
		if (ImGui::Button("Copy##SelectedPath"))
			ExecuteSceneObjectEditAction(windowState, SceneObjectEditKind::Path, SceneObjectEditAction::Copy);
		ImGui::SameLine();
		if (ImGui::Button("Paste##SelectedPath"))
			ExecuteSceneObjectEditAction(windowState, SceneObjectEditKind::Path, SceneObjectEditAction::Paste);
	}
}
