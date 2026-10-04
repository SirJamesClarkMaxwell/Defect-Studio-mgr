#include "Core/dspch.hpp"
#include "Presentation/Panels/SceneOrientationControls.hpp"

#include <algorithm>
#include <array>
#include <string_view>
#include <imgui.h>
#include "Renderer/RendererLayer.hpp"
#include "Renderer/Scene/SceneAxisAlignment.hpp"
#include "Renderer/Scene/SceneOrbitalAim.hpp"
#include "Renderer/Scene/SceneOrbitalGeometry.hpp"
#include "Renderer/Commands/SceneObjectsSnapshotCommand.hpp"

namespace DefectStudio
{
	namespace
	{
		void AlignAxes(RendererWindowState &windowState, int ownAxis, int defectAxis)
		{
			auto before = CaptureSceneObjectsSnapshot(windowState);
			if (AlignSelectedSceneObjectAxes(windowState, ownAxis, defectAxis) > 0)
				PushSceneObjectsUndoSnapshot(windowState, std::move(before));
		}
		void AlignmentTooltip()
		{
			if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
				ImGui::SetTooltip("Wybierz obiekt z własnymi osiami i utwórz osie defektu.\n"
					"Obrót wokół własnego środka. Płaszczyzny odczepiają się od atomów (Ctrl+Z cofa).\n"
					"Druga oś: x obiektu na x defektu, a gdy x jest zajęte, używane jest y.");
		}
	}

	void DrawSceneOrbitalAimControls(RendererWindowState &windowState, const std::vector<SceneObjectId> &selection)
	{
		const bool many = selection.size() > 1;
		const auto targets = CollectOrbitalAimTargets(windowState);
		auto &storage = *ImGui::GetStateStorage();
		const ImGuiID key = ImGui::GetID("OrbitalAimTarget");
		// -1 is a per-orbital nearest-vacancy target, never a shared centroid.
		int selected = std::clamp(storage.GetInt(key), -1, static_cast<int>(targets.size()) - 1);
		if (selected == -1 && windowState.structure.vacancies.empty())
			selected = 0;
		if (ImGui::BeginCombo("Skieruj na", selected == -1 ? "Każdy na najbliższy wakans" : targets[selected].label.c_str()))
		{
			for (int i = 0; i < static_cast<int>(targets.size()); ++i)
			{
				if (ImGui::Selectable(targets[i].label.c_str(), i == selected))
					storage.SetInt(key, selected = i);
				if (i == selected)
					ImGui::SetItemDefaultFocus();
			}
			ImGui::BeginDisabled(windowState.structure.vacancies.empty());
			if (ImGui::Selectable("Każdy na najbliższy wakans", selected == -1))
				storage.SetInt(key, selected = -1);
			ImGui::EndDisabled();
			if (windowState.structure.vacancies.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
				ImGui::SetTooltip("Dodaj wakans w tej strukturze.");
			ImGui::EndCombo();
		}
		const auto rotations = AimSelectedSceneOrbitals(windowState, selection,
			selected == -1 ? std::nullopt : std::optional<glm::vec3>(targets[selected].position));
		ImGui::BeginDisabled(rotations.empty());
		if (ImGui::Button(many ? "Skieruj wszystkie" : "Skieruj"))
		{
			PushPinnedMeasurementUndoSnapshot(windowState);
			for (const auto &rotation : rotations)
			{
				const auto index = AnnotationIndex(windowState.sceneOrbitals, rotation.id);
				if (index < windowState.sceneOrbitals.size())
					windowState.sceneOrbitals[index].rotationEuler = rotation.eulerDegrees;
			}
		}
		ImGui::EndDisabled();
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
			ImGui::SetTooltip("Każdy orbital celuje ze swojego środka. Pomijane są orbitale bez osi,\n"
				"dwuośrodkowe, LCAO oraz cele pokrywające się ze środkiem.");

		std::vector<SceneObjectId> orientable;
		for (const auto &orbital : windowState.sceneOrbitals)
			if (std::find(selection.begin(), selection.end(), orbital.id) != selection.end() &&
				orbital.lcaoComponents.empty() && !IsTwoCenterPreset(orbital.preset))
				orientable.push_back(orbital.id);
		const auto &frame = windowState.structure.defectFrame;
		ImGui::BeginDisabled(!frame || orientable.empty());
		if (ImGui::Button(many ? "Ustaw wszystkie w osiach defektu" : "Ustaw w osiach defektu"))
		{
			PushPinnedMeasurementUndoSnapshot(windowState);
			const auto euler = glm::degrees(glm::eulerAngles(glm::quat_cast(glm::mat3(frame->x, frame->y, frame->z))));
			for (const auto id : orientable)
			{
				const auto index = AnnotationIndex(windowState.sceneOrbitals, id);
				if (index < windowState.sceneOrbitals.size())
					windowState.sceneOrbitals[index].rotationEuler = euler;
			}
		}
		ImGui::EndDisabled();
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
			ImGui::SetTooltip("Własne x/y/z = osie defektu. Wymaga osi defektu i orbitalu jednoośrodkowego.");
	}

	void DrawSceneAxisAlignmentMenu(RendererWindowState &windowState)
	{
		const bool enabled = windowState.structure.defectFrame && !CollectSceneAxisAlignmentTargets(windowState).empty();
		if (ImGui::BeginMenu("Wyrównaj oś obiektu...", enabled))
		{
			constexpr std::array<std::string_view, 3> axes = {"x", "y", "z / normalna"};
			constexpr std::array<std::string_view, 3> defectAxes = {"x", "y", "z"};
			for (int own = 0; own < 3; ++own)
				for (int defect = 0; defect < 3; ++defect)
				{
					const auto label = std::string(axes[own]) + " obiektu wzdłuż " + std::string(defectAxes[defect]) + " defektu";
					if (ImGui::MenuItem(label.c_str()))
						AlignAxes(windowState, own, defect);
					AlignmentTooltip();
				}
			ImGui::EndMenu();
		}
		else
			AlignmentTooltip();
	}

	void DrawSceneAxisAlignmentControls(RendererWindowState &windowState)
	{
		auto &storage = *ImGui::GetStateStorage();
		const ImGuiID ownKey = ImGui::GetID("OwnAlignmentAxis");
		const ImGuiID defectKey = ImGui::GetID("DefectAlignmentAxis");
		int own = std::clamp(storage.GetInt(ownKey, 1), 0, 2);
		int defect = std::clamp(storage.GetInt(defectKey, 2), 0, 2);
		if (ImGui::Combo("Os obiektu", &own, "x\0y\0z / normalna\0"))
			storage.SetInt(ownKey, own);
		if (ImGui::Combo("Os defektu", &defect, "x\0y\0z\0"))
			storage.SetInt(defectKey, defect);
		ImGui::BeginDisabled(!windowState.structure.defectFrame || CollectSceneAxisAlignmentTargets(windowState).empty());
		if (ImGui::Button("Wyrównaj oś obiektu"))
			AlignAxes(windowState, own, defect);
		ImGui::EndDisabled();
		AlignmentTooltip();
	}
}
