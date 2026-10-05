#include "Core/dspch.hpp"

#include "Presentation/Panels/OperatorRedoPanel.hpp"

#include <algorithm>
#include <cmath>
#include <string>

#include <imgui.h>

#include "Renderer/RendererLayer.hpp"

namespace DefectStudio
{
	namespace
	{
		StructuredError PanelError(const char *code, const char *message)
		{
			return {ErrorCategory::Validation, Severity::Error, message, message,
				"Spróbuj ponownie.", "Presentation/OperatorRedoPanel", code, DisplayPolicy::Silent};
		}
	}

	Result<void> OperatorRedoPanel::RunAndOpen(const SceneOperator &op, RendererWindowState &window)
	{
		Close();
		m_Operator = &op;
		m_Before = CaptureSceneObjectsSnapshot(window);
		m_SelectedAtoms = window.selectedAtomIndices;
		m_SelectedVacancies = window.selectedVacancies;
		m_Values = op.defaults;
		m_WindowId = window.windowId;

		const Result<std::vector<SceneObjectId>> result = op.execute(window, m_Values);
		if (!result)
		{
			RestoreSceneObjectsSnapshot(window, *m_Before);
			window.selectedAtomIndices = m_SelectedAtoms;
			window.selectedVacancies = m_SelectedVacancies;
			Close();
			return result.Error();
		}

		PushSceneObjectsUndoSnapshot(window, *m_Before);
		if (const Ref<UndoStack> undoStack = GetBoundRendererUndoStack().lock())
			m_UndoDepth = undoStack->GetUndoDepth();
		else
			m_UndoDepth = 0;
		m_Collapsed = false;
		m_Open = true;
		return {};
	}

	Result<void> OperatorRedoPanel::Reapply(RendererWindowState &window, const SceneOperatorValues &values)
	{
		if (!m_Open || m_Operator == nullptr || !m_Before.has_value() || window.windowId != m_WindowId)
			return PanelError("operator_redo.closed", "Panel ponownego uruchamiania operacji jest zamknięty.");

		const SceneObjectsSnapshot lastGood = CaptureSceneObjectsSnapshot(window);
		RestoreSceneObjectsSnapshot(window, *m_Before);
		window.selectedAtomIndices = m_SelectedAtoms;
		window.selectedVacancies = m_SelectedVacancies;

		const Result<std::vector<SceneObjectId>> result = m_Operator->execute(window, values);
		if (!result)
		{
			RestoreSceneObjectsSnapshot(window, lastGood);
			Close();
			return result.Error();
		}

		m_Values = values;
		return {};
	}

	void OperatorRedoPanel::PollInvalidation(const UndoStack &undoStack, const RendererWindowState *window) noexcept
	{
		if (!m_Open)
			return;
		if (window == nullptr)
		{
			Close();
			return;
		}
		if (window->windowId != m_WindowId)
			return;
		if (undoStack.GetUndoDepth() != m_UndoDepth)
			Close();
	}

	void OperatorRedoPanel::Close() noexcept
	{
		m_Operator = nullptr;
		m_Before.reset();
		m_SelectedAtoms.clear();
		m_SelectedVacancies.clear();
		m_Values.clear();
		m_WindowId.clear();
		m_UndoDepth = 0;
		m_Open = false;
		m_Collapsed = false;
	}

	bool OperatorRedoPanel::IsOpen() const noexcept
	{
		return m_Open;
	}

	const SceneOperatorValues &OperatorRedoPanel::Values() const noexcept
	{
		return m_Values;
	}

	const SceneOperator *OperatorRedoPanel::CurrentOperator() const noexcept
	{
		return m_Operator;
	}

	void OperatorRedoPanel::Draw(RendererWindowState &window)
	{
		if (!m_Open || m_Operator == nullptr || window.windowId != m_WindowId)
			return;

		const ImVec2 viewportPosition = ImGui::GetWindowPos();
		const ImVec2 viewportSize = ImGui::GetWindowSize();
		ImGui::SetNextWindowPos(
			ImVec2(viewportPosition.x + 8.0f, viewportPosition.y + viewportSize.y - 8.0f),
			ImGuiCond_Always, ImVec2(0.0f, 1.0f));
		const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoDocking |
			ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings;
		bool visible = true;
		if (!ImGui::Begin("Dostosuj operację###OperatorRedoPanel", &visible, flags))
		{
			ImGui::End();
			return;
		}
		if (!visible)
		{
			Close();
			ImGui::End();
			return;
		}

		const std::string header = m_Operator->label + "##OperatorRedoHeader";
		const bool expanded = ImGui::CollapsingHeader(
			header.c_str(), m_Collapsed ? 0 : ImGuiTreeNodeFlags_DefaultOpen);
		m_Collapsed = !expanded;
		if (expanded)
		{
			for (const SceneOperatorParameter &parameter : m_Operator->schema)
			{
				auto value = m_Values.find(parameter.key);
				if (value == m_Values.end())
					continue;
				const std::string label = parameter.label + "##OperatorRedo_" + parameter.key;
				bool changed = false;
				switch (parameter.kind)
				{
				case SceneOperatorParameter::Kind::Float:
					if (auto *current = std::get_if<float>(&value->second))
						changed = ImGui::SliderFloat(label.c_str(), current, parameter.minimum, parameter.maximum);
					break;
				case SceneOperatorParameter::Kind::Int:
					if (auto *current = std::get_if<int>(&value->second))
						changed = ImGui::SliderInt(label.c_str(), current,
							static_cast<int>(std::ceil(parameter.minimum)), static_cast<int>(std::floor(parameter.maximum)));
					break;
				case SceneOperatorParameter::Kind::Bool:
					if (auto *current = std::get_if<bool>(&value->second))
						changed = ImGui::Checkbox(label.c_str(), current);
					break;
				case SceneOperatorParameter::Kind::Enum:
					if (auto *current = std::get_if<int>(&value->second))
					{
						std::vector<const char *> labels;
						labels.reserve(parameter.enumLabels.size());
						for (const std::string &entry : parameter.enumLabels)
							labels.push_back(entry.c_str());
						changed = !labels.empty() && ImGui::Combo(label.c_str(), current, labels.data(),
							static_cast<int>(labels.size()));
					}
					break;
				case SceneOperatorParameter::Kind::Color:
					if (auto *current = std::get_if<glm::vec3>(&value->second))
						changed = ImGui::ColorEdit3(label.c_str(), &current->x);
					break;
				}
				// Live update, Blender-style. `changed` and IsItemDeactivatedAfterEdit() are true on
				// different frames, and a Combo or a Checkbox never reports the second one at all, so
				// waiting for it means the panel never reacts.
				// ponytail: one rebuild per drag frame. If a large cycle ever stutters, buffer the
				// value and re-run on IsItemDeactivatedAfterEdit() || IsItemEdited() instead.
				if (changed)
				{
					if (!Reapply(window, m_Values))
						break;
				}
			}
		}
		ImGui::End();
	}
}
