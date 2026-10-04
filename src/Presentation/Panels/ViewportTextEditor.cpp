#include "Core/dspch.hpp"

#include "Presentation/Panels/ViewportTextEditor.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_map>
#include <utility>

#include <misc/cpp/imgui_stdlib.h>

#include "Presentation/Panels/ViewportPicking.hpp"
#include "Presentation/Panels/ViewportVacancySelection.hpp"
#include "Renderer/RendererLayer.hpp"
#include "Renderer/RendererViewCamera.hpp"
#include "Renderer/RendererWindowState.hpp"
#include "Renderer/Scene/SceneFreeLabelAnchors.hpp"
#include "Renderer/Scene/SceneSystem.hpp"
#include "Renderer/Scene/SelectionHitTest.hpp"

namespace DefectStudio
{
	namespace
	{
		struct TextEditSession
		{
			SceneObjectId label;
			std::string originalText;
			std::string draft;
			bool created = false;
			bool openRequested = true;
			int lastDrawFrame = -1;
		};

		std::unordered_map<std::string, TextEditSession> s_TextEditors;

		void CancelTextEdit(RendererWindowState &window, const TextEditSession &session)
		{
			const std::size_t index = AnnotationIndex(window.freeLabels, session.label);
			if (session.created && index < window.freeLabels.size() && window.freeLabels[index].text.empty())
			{
				window.freeLabels.erase(window.freeLabels.begin() + index);
				std::erase(window.selectedFreeLabels, session.label);
				SceneSystem::SyncLabelEntities(window.sceneRegistry, window);
				SceneSystem::SyncLabelSelection(window.sceneRegistry, window);
			}
		}

		void DrawMarkupHelp()
		{
			ImGui::SameLine();
			ImGui::TextDisabled("(?)");
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("TeX: V_B, V_{Si}^{-}, E_g^{(1)}, x^2\n"
					"Groups: {text}; nested scripts: x^{a_b}\n"
					"Greek: \\alpha ... \\omega, \\Alpha ... \\Omega\n"
					"Symbols: \\pm \\mp \\cdot \\times \\to \\rightarrow \\leftarrow\n"
					"\\leftrightarrow \\uparrow \\downarrow \\circ \\deg \\AA \\infty\n"
					"\\approx \\neq \\leq \\geq \\sim \\prime \\hbar \\langle \\rangle\n"
					"Spaces: \\, (thin), \\ (space). Escapes: \\_ \\^ \\{ \\} \\\\\n"
					"Unknown commands are literal; minus in a script is a charge sign.");
		}
	} // namespace

	void OpenViewportTextEditor(RendererWindowState &window, SceneObjectId label, bool created)
	{
		const std::size_t index = AnnotationIndex(window.freeLabels, label);
		if (index >= window.freeLabels.size())
			return;
		window.freeLabelDragging = false;
		const std::string text = window.freeLabels[index].text;
		s_TextEditors.insert_or_assign(window.windowId, TextEditSession{label, text, text, created, true});
	}

	bool IsViewportTextEditorActive(const RendererWindowState &window)
	{
		return s_TextEditors.contains(window.windowId);
	}

	void DrawViewportTextEditor(RendererWindowState &window, const ImVec2 &imageOrigin, const ImVec2 &imageSize)
	{
		const auto found = s_TextEditors.find(window.windowId);
		if (found == s_TextEditors.end())
			return;
		TextEditSession &session = found->second;
		if (session.lastDrawFrame == ImGui::GetFrameCount())
			return;
		session.lastDrawFrame = ImGui::GetFrameCount();
		const std::size_t index = AnnotationIndex(window.freeLabels, session.label);
		if (index >= window.freeLabels.size() || window.camera == nullptr ||
			window.freeLabels[index].text != session.originalText)
		{
			CancelTextEdit(window, session);
			s_TextEditors.erase(found);
			return;
		}
		RendererWindowState::FreeLabel &label = window.freeLabels[index];
		const auto screen = SelectionHitTest::ProjectToScreen(
			window.camera->ProjectionMatrix() * window.camera->ViewMatrix(), glm::vec2(imageSize.x, imageSize.y),
			label.worldPosition + window.viewOffset);
		const std::string popup = "Text###ViewportText_" + window.windowId;
		const bool opening = session.openRequested;
		if (screen)
			ImGui::SetNextWindowPos(ImVec2(imageOrigin.x + screen->x, imageOrigin.y + screen->y), ImGuiCond_Always);
		if (opening)
		{
			ImGui::OpenPopup(popup.c_str());
			session.openRequested = false;
		}
		bool finished = false;
		if (ImGui::BeginPopup(popup.c_str(), ImGuiWindowFlags_AlwaysAutoResize))
		{
			if (opening)
				ImGui::SetKeyboardFocusHere();
			ImGui::SetNextItemWidth(260.0f);
			const bool commit = ImGui::InputText("##Text", &session.draft, ImGuiInputTextFlags_EnterReturnsTrue);
			const bool cancel = ImGui::IsKeyPressed(ImGuiKey_Escape, false);
			if (cancel)
				CancelTextEdit(window, session);
			else if (commit && label.text != session.draft)
			{
				if (!session.created)
					PushPinnedMeasurementUndoSnapshot(window);
				label.text = session.draft;
			}
			if (commit || cancel)
			{
				ImGui::CloseCurrentPopup();
				finished = true;
			}
			ImGui::EndPopup();
		}
		else
		{
			CancelTextEdit(window, session);
			finished = true;
		}
		if (finished)
			s_TextEditors.erase(found);
	}

	void HandleViewportTextToolClick(RendererWindowState &window, const ImVec2 &imageOrigin,
		const ImVec2 &imageSize, const glm::vec3 &clickPosition)
	{
		if (window.camera == nullptr || imageSize.x <= 0.0f || imageSize.y <= 0.0f)
			return;
		const ImVec2 mouse = ImGui::GetMousePos();
		const float x = mouse.x - imageOrigin.x;
		const float y = mouse.y - imageOrigin.y;
		if (x < 0.0f || y < 0.0f || x >= imageSize.x || y >= imageSize.y)
			return;
		const glm::mat4 inverse = glm::inverse(window.camera->ProjectionMatrix() * window.camera->ViewMatrix());
		const glm::vec2 ndc(2.0f * x / imageSize.x - 1.0f, 1.0f - 2.0f * y / imageSize.y);
		const glm::vec4 nearPoint = inverse * glm::vec4(ndc, -1.0f, 1.0f);
		const glm::vec4 farPoint = inverse * glm::vec4(ndc, 1.0f, 1.0f);
		if (std::abs(nearPoint.w) <= 1.0e-6f || std::abs(farPoint.w) <= 1.0e-6f)
			return;
		const glm::vec3 origin = glm::vec3(nearPoint) / nearPoint.w - window.viewOffset;
		const glm::vec3 direction = glm::vec3(farPoint) / farPoint.w - glm::vec3(nearPoint) / nearPoint.w;

		RendererWindowState::FreeLabel label;
		label.text.clear();
		label.worldPosition = clickPosition;
		label.anchorAtom = PickAtomAlongRay(window, origin, direction);
		if (!label.anchorAtom)
			label.anchorVacancy = PickVacancyAlongRay(window, origin, direction);
		if (const auto anchor = ResolveFreeLabelAnchor(window, label))
			label.worldPosition = *anchor;
		PushPinnedMeasurementUndoSnapshot(window);
		label.id = window.sceneRegistry.AllocateObjectId();
		const SceneObjectId id = label.id;
		window.freeLabels.push_back(std::move(label));
		window.selectedPinnedMeasurements.clear();
		window.selectedSceneOrbitals.clear();
		window.selectedScenePlanes.clear();
		window.selectedScenePaths.clear();
		window.selectedVacancies.clear();
		window.defectFrameSelected = false;
		window.selectedFreeLabels = {id};
		SceneSystem::ClearStructureSelection(window.sceneRegistry, window);
		SceneSystem::SyncLabelEntities(window.sceneRegistry, window);
		SceneSystem::SyncLabelSelection(window.sceneRegistry, window);
		OpenViewportTextEditor(window, id, true);
	}

	void DrawFreeLabelTextInput(RendererWindowState &window, std::size_t index, const char *id, float width)
	{
		if (index >= window.freeLabels.size())
			return;
		RendererWindowState::FreeLabel &label = window.freeLabels[index];
		std::string draft = label.text;
		ImGui::SetNextItemWidth(width);
		const bool changed = ImGui::InputText(id, &draft);
		if (ImGui::IsItemActivated())
			PushPinnedMeasurementUndoSnapshot(window);
		if (changed)
			label.text = std::move(draft);
	}

	void DrawSelectedFreeLabelTextProperties(RendererWindowState &window)
	{
		if (!window.selectedPinnedMeasurements.empty() || window.selectedFreeLabels.size() != 1)
			return;
		const std::size_t index = AnnotationIndex(window.freeLabels, window.selectedFreeLabels.front());
		if (index >= window.freeLabels.size())
			return;
		DrawFreeLabelTextInput(window, index, "Text##SelectedFreeLabel", 240.0f);
		DrawMarkupHelp();
		RendererWindowState::FreeLabel &label = window.freeLabels[index];
		if (label.anchorAtom)
		{
			if (*label.anchorAtom < window.structure.atoms.size())
				ImGui::Text("Anchor: atom %zu (%s)", *label.anchorAtom,
					window.structure.atoms[*label.anchorAtom].element.c_str());
			else
				ImGui::Text("Anchor: atom %zu (missing)", *label.anchorAtom);
		}
		else if (label.anchorVacancy)
		{
			if (*label.anchorVacancy < window.structure.vacancies.size())
				ImGui::Text("Anchor: vacancy %s", window.structure.vacancies[*label.anchorVacancy].label.c_str());
			else
				ImGui::Text("Anchor: vacancy %zu (missing)", *label.anchorVacancy);
		}
		else
			ImGui::TextUnformatted("Anchor: free");
		if (label.anchorAtom || label.anchorVacancy)
		{
			ImGui::SameLine();
			if (ImGui::Button("Detach##FreeLabelAnchor"))
			{
				PushPinnedMeasurementUndoSnapshot(window);
				label.anchorAtom.reset();
				label.anchorVacancy.reset();
				label.anchorOffset = glm::vec3(0.0f);
			}
		}
	}
} // namespace DefectStudio
