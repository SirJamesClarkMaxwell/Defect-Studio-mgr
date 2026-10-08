#include "Core/dspch.hpp"

#include "Presentation/Panels/SceneHideVolumeEditor.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <utility>

#include <glm/gtc/quaternion.hpp>
#include <imgui.h>

#include "Presentation/Panels/PropertyGrid.hpp"
#include "Renderer/RendererLayer.hpp"
#include "Renderer/Scene/SceneSelection.hpp"
#include "Renderer/Scene/SceneSystem.hpp"

namespace DefectStudio
{
	namespace
	{
		void RefreshMask(RendererWindowState &window)
		{
			SceneSystem::PushSelectionAndVisibilityToWindowState(window.sceneRegistry, window);
		}

		std::vector<std::size_t> SelectedAtoms(const RendererWindowState &window)
		{
			std::vector<std::size_t> atoms = window.selectedAtomIndices;
			std::erase_if(atoms, [&window](std::size_t index) { return index >= window.structure.atoms.size(); });
			// ECS iteration order is not atom-index order; keep persisted anchors deterministic.
			std::sort(atoms.begin(), atoms.end());
			return atoms;
		}

		std::vector<SceneHideVolume> SelectedVolumes(const RendererWindowState &window)
		{
			std::vector<SceneHideVolume> volumes;
			for (const SceneObjectId id : window.selectedSceneHideVolumes)
			{
				const std::size_t index = AnnotationIndex(window.sceneHideVolumes, id);
				if (index < window.sceneHideVolumes.size()) volumes.push_back(window.sceneHideVolumes[index]);
			}
			return volumes;
		}

		void InsertCopies(RendererWindowState &window, std::vector<SceneHideVolume> copies)
		{
			PushPinnedMeasurementUndoSnapshot(window);
			ClearAllSceneSelection(window);
			for (SceneHideVolume &copy : copies)
			{
				copy.id = window.sceneRegistry.AllocateObjectId();
				copy.persistKey.clear();
				window.selectedSceneHideVolumes.push_back(copy.id);
				window.sceneHideVolumes.push_back(std::move(copy));
			}
			RefreshMask(window);
		}

		template <typename T, typename Widget>
		bool DrawUndoableValue(RendererWindowState &window, T &value, Widget &&widget)
		{
			T edited = value;
			const bool changed = widget(edited);
			if (ImGui::IsItemActivated()) PushPinnedMeasurementUndoSnapshot(window);
			if (changed)
			{
				value = edited;
				RefreshMask(window);
			}
			return changed;
		}

		void SetCylinderAxis(RendererWindowState &window, SceneHideVolume &volume, glm::vec3 axis)
		{
			if (glm::dot(axis, axis) <= 1e-12f) return;
			axis = glm::normalize(axis);
			const glm::vec3 reference = std::abs(axis.z) < 0.9f ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
			const glm::vec3 tangent = glm::normalize(glm::cross(reference, axis));
			PushPinnedMeasurementUndoSnapshot(window);
			volume.orientation = glm::mat3(tangent, glm::cross(axis, tangent), axis);
			RefreshMask(window);
		}
	} // namespace

	SceneHideVolume MakeSceneHideVolume(const RendererWindowState &windowState, HideVolumeKind kind,
		std::optional<std::size_t> targetAtom, std::optional<glm::vec3> targetPosition)
	{
		SceneHideVolume volume;
		volume.kind = kind;
		volume.invert = true;
		volume.anchorAtoms = SelectedAtoms(windowState);
		if (volume.anchorAtoms.empty() && targetAtom && *targetAtom < windowState.structure.atoms.size())
			volume.anchorAtoms = {*targetAtom};
		volume.center = ResolveHideVolumeCenter(volume, windowState.structure).value_or(
			targetPosition.value_or(windowState.structure.lattice * glm::vec3(0.5f)));
		float radius = kDefaultHideVolumeRadius;
		for (const std::size_t index : volume.anchorAtoms)
			radius = std::max(radius, glm::distance(windowState.structure.atoms[index].cartesianPosition, volume.center) + 1.0f);
		volume.halfExtents = glm::vec3(radius);
		const auto count = std::count_if(windowState.sceneHideVolumes.begin(), windowState.sceneHideVolumes.end(),
			[kind](const SceneHideVolume &item) { return item.kind == kind; });
		const char *name = kind == HideVolumeKind::Sphere ? "Kula" : kind == HideVolumeKind::Box ? "Prostopadłościan" : "Walec";
		volume.name = std::string(name) + " " + std::to_string(count + 1);
		return volume;
	}

	void AddSceneHideVolume(RendererWindowState &windowState, HideVolumeKind kind,
		std::optional<std::size_t> targetAtom, std::optional<glm::vec3> targetPosition)
	{
		AddSceneHideVolume(windowState, MakeSceneHideVolume(windowState, kind, targetAtom, targetPosition));
	}

	SceneObjectId AddSceneHideVolume(RendererWindowState &windowState, SceneHideVolume volume, SceneOperationUndo undo)
	{
		if (undo == SceneOperationUndo::Push) PushPinnedMeasurementUndoSnapshot(windowState);
		volume.id = windowState.sceneRegistry.AllocateObjectId();
		const SceneObjectId id = volume.id;
		windowState.sceneHideVolumes.push_back(std::move(volume));
		ClearAllSceneSelection(windowState);
		windowState.selectedSceneHideVolumes = {id};
		RefreshMask(windowState);
		return id;
	}

	void SetSceneHideVolumeActive(RendererWindowState &windowState, SceneObjectId id, bool active)
	{
		const auto index = AnnotationIndex(windowState.sceneHideVolumes, id);
		if (index >= windowState.sceneHideVolumes.size() || windowState.sceneHideVolumes[index].active == active) return;
		PushPinnedMeasurementUndoSnapshot(windowState);
		windowState.sceneHideVolumes[index].active = active;
		RefreshMask(windowState);
	}

	void EraseSceneHideVolumes(RendererWindowState &windowState, const std::vector<SceneObjectId> &ids)
	{
		if (ids.empty()) return;
		PushPinnedMeasurementUndoSnapshot(windowState);
		std::erase_if(windowState.sceneHideVolumes, [&ids](const SceneHideVolume &volume) {
			return std::find(ids.begin(), ids.end(), volume.id) != ids.end();
		});
		std::erase_if(windowState.selectedSceneHideVolumes, [&windowState](SceneObjectId id) {
			return AnnotationIndex(windowState.sceneHideVolumes, id) == windowState.sceneHideVolumes.size();
		});
		RefreshMask(windowState);
	}

	void DuplicateSelectedSceneHideVolumes(RendererWindowState &windowState)
	{
		std::vector<SceneHideVolume> copies = SelectedVolumes(windowState);
		if (!copies.empty()) InsertCopies(windowState, std::move(copies));
	}

	std::vector<SceneHideVolume> &GetSceneHideVolumeClipboard()
	{
		// ponytail: in-process clipboard; emit YAML if presets need to move between app sessions.
		static std::vector<SceneHideVolume> clipboard;
		return clipboard;
	}

	void CopySceneHideVolumesToClipboard(const RendererWindowState &windowState)
	{
		auto copies = SelectedVolumes(windowState);
		for (SceneHideVolume &copy : copies)
			copy.center = ResolveHideVolumeCenter(copy, windowState.structure).value_or(copy.center);
		GetSceneHideVolumeClipboard() = std::move(copies);
	}

	void PasteSceneHideVolumesFromClipboard(RendererWindowState &windowState)
	{
		auto copies = GetSceneHideVolumeClipboard();
		if (copies.empty()) return;
		const auto atoms = SelectedAtoms(windowState);
		for (SceneHideVolume &copy : copies)
		{
			if (copy.frame == HideVolumeFrame::Fractional) copy.anchorAtoms.clear();
			else if (!atoms.empty()) copy.anchorAtoms = atoms;
			else std::erase_if(copy.anchorAtoms, [&windowState](std::size_t index) {
				return index >= windowState.structure.atoms.size();
			});
		}
		InsertCopies(windowState, std::move(copies));
	}

	void AnchorSelectedSceneHideVolumesToSelectedAtoms(RendererWindowState &windowState)
	{
		if (windowState.selectedSceneHideVolumes.empty()) return;
		const auto atoms = SelectedAtoms(windowState);
		PushPinnedMeasurementUndoSnapshot(windowState);
		for (const SceneObjectId id : windowState.selectedSceneHideVolumes)
		{
			const std::size_t index = AnnotationIndex(windowState.sceneHideVolumes, id);
			if (index >= windowState.sceneHideVolumes.size()) continue;
			auto &volume = windowState.sceneHideVolumes[index];
			if (volume.frame != HideVolumeFrame::Anchored) continue;
			volume.center = HideVolumeWorldCenter(volume, windowState.structure);
			volume.anchorAtoms = atoms;
		}
		RefreshMask(windowState);
	}

	std::size_t CountAtomsHiddenBySceneHideVolume(const RendererWindowState &windowState, const SceneHideVolume &volume)
	{
		return AtomsCoveredByHideVolumes(windowState.structure, std::span<const SceneHideVolume>(&volume, 1)).size();
	}

	void DrawSelectedSceneHideVolumeSection(RendererWindowState &windowState)
	{
		if (windowState.selectedSceneHideVolumes.empty()) return;
		const std::size_t index = AnnotationIndex(windowState.sceneHideVolumes, windowState.selectedSceneHideVolumes.front());
		if (index >= windowState.sceneHideVolumes.size()) return;
		auto &volume = windowState.sceneHideVolumes[index];
		ImGui::PushID("##SceneHideVolume");
		ImGui::SeparatorText("Bryła ukrywająca");
		bool active = volume.active;
		if (ImGui::Checkbox("Aktywna (ukrywa)", &active)) SetSceneHideVolumeActive(windowState, volume.id, active);
		ImGui::SetItemTooltip("Włącza ukrywanie. Oko i kamera sterują tylko rysowaniem obrysu.");
		if (windowState.selectedSceneHideVolumes.size() > 1)
			ImGui::TextDisabled("Zaznaczono %zu - edytujesz pierwszą.", windowState.selectedSceneHideVolumes.size());
		const float labelWidth = PropertyGridLabelWidth({"Nazwa", "Kształt", "Układ", "Ukrywa", "Kotwica", "Środek",
			"Orientacja", "Promień", "Wymiary", "Wysokość", "Kolor", "Krycie", "Ukryte atomy"});
		constexpr auto open = ImGuiTreeNodeFlags_DefaultOpen;
		if (ImGui::CollapsingHeader("Bryła", open))
		{
			PropertyGrid grid("##HideVolumeShape", labelWidth);
			if (grid)
			{
				grid.Row("Nazwa");
				DrawUndoableValue(windowState, volume.name, [](std::string &name) {
					char text[256];
					std::snprintf(text, sizeof(text), "%s", name.c_str());
					if (!ImGui::InputText("##name", text, sizeof(text))) return false;
					name = text;
					return true;
				});
				grid.Row("Kształt");
				int kind = static_cast<int>(volume.kind);
				if (ImGui::Combo("##kind", &kind, "Kula\0Prostopadłościan\0Walec\0"))
				{
					PushPinnedMeasurementUndoSnapshot(windowState);
					volume.kind = static_cast<HideVolumeKind>(kind);
					RefreshMask(windowState);
				}
				grid.Row("Układ");
				const glm::mat3 &lattice = windowState.structure.lattice;
				const float meanLength = (glm::length(lattice[0]) + glm::length(lattice[1]) + glm::length(lattice[2])) / 3.0f;
				ImGui::BeginDisabled(!std::isfinite(meanLength) || meanLength <= 1e-6f || std::abs(glm::determinant(lattice)) <= 1e-6f);
				int frame = static_cast<int>(volume.frame);
				if (ImGui::Combo("##frame", &frame, "Zakotwiczony (Å)\0Ułamkowy (sieć)\0"))
				{
					PushPinnedMeasurementUndoSnapshot(windowState);
					const glm::vec3 worldCenter = HideVolumeWorldCenter(volume, windowState.structure);
					volume.frame = static_cast<HideVolumeFrame>(frame);
					const bool fractional = volume.frame == HideVolumeFrame::Fractional;
					volume.center = fractional ? glm::inverse(lattice) * worldCenter : worldCenter;
					// ponytail: mean length preserves dimensions exactly for cubic cells; edit dimensions for skewed cells.
					volume.halfExtents *= fractional ? 1.0f / meanLength : meanLength;
					if (fractional) volume.anchorAtoms.clear();
					RefreshMask(windowState);
				}
				ImGui::EndDisabled();
				grid.Row("Ukrywa");
				int invert = volume.invert ? 1 : 0;
				if (ImGui::Combo("##invert", &invert, "Wewnątrz\0Na zewnątrz\0"))
				{
					PushPinnedMeasurementUndoSnapshot(windowState);
					volume.invert = invert != 0;
					RefreshMask(windowState);
				}
			}
		}
		const bool fractional = volume.frame == HideVolumeFrame::Fractional;
		const char *format = fractional ? "%.3f" : "%.2f Å";
		const float minimum = fractional ? 0.01f : 0.1f;
		const float speed = fractional ? 0.001f : 0.05f;
		if (ImGui::CollapsingHeader("Położenie", open))
		{
			PropertyGrid grid("##HideVolumePosition", labelWidth);
			if (grid)
			{
				grid.Row("Kotwica");
				if (volume.anchorAtoms.empty()) ImGui::TextUnformatted("brak");
				else ImGui::Text("%zu atomów", volume.anchorAtoms.size());
				ImGui::BeginDisabled(fractional);
				if (ImGui::Button("Z zaznaczenia")) AnchorSelectedSceneHideVolumesToSelectedAtoms(windowState);
				ImGui::SameLine();
				ImGui::BeginDisabled(volume.anchorAtoms.empty());
				if (ImGui::Button("Odkotwicz"))
				{
					PushPinnedMeasurementUndoSnapshot(windowState);
					volume.center = HideVolumeWorldCenter(volume, windowState.structure);
					volume.anchorAtoms.clear();
					RefreshMask(windowState);
				}
				ImGui::EndDisabled();
				ImGui::EndDisabled();
				grid.Row("Środek");
				const auto anchor = ResolveHideVolumeCenter(volume, windowState.structure);
				if (anchor)
				{
					auto center = *anchor;
					ImGui::BeginDisabled();
					ImGui::DragFloat3("##center", &center.x, speed, 0, 0, format);
					ImGui::EndDisabled();
				}
				else DrawUndoableValue(windowState, volume.center, [&](glm::vec3 &value) {
					return ImGui::DragFloat3("##center", &value.x, speed, 0, 0, format);
				});
				if (volume.kind != HideVolumeKind::Sphere)
				{
					grid.Row("Orientacja");
					DrawUndoableValue(windowState, volume.orientation, [](glm::mat3 &orientation) {
						glm::vec3 euler = glm::degrees(glm::eulerAngles(glm::quat_cast(orientation)));
						if (!ImGui::DragFloat3("##orientation", &euler.x, 0.5f, 0, 0, "%.1f°")) return false;
						orientation = glm::mat3_cast(glm::quat(glm::radians(euler)));
						return true;
					});
					if (volume.kind == HideVolumeKind::Cylinder)
					{
						for (int axis = 0; axis < 3; ++axis)
						{
							if (axis != 0) ImGui::SameLine();
							if (ImGui::Button(axis == 0 ? "X" : axis == 1 ? "Y" : "Z"))
							{
								glm::vec3 direction(0);
								direction[axis] = 1;
								SetCylinderAxis(windowState, volume, direction);
							}
						}
						const auto atoms = SelectedAtoms(windowState);
						ImGui::SameLine();
						ImGui::BeginDisabled(atoms.size() != 2);
						if (ImGui::Button("Z 2 atomów") && atoms.size() == 2)
						{
							glm::vec3 direction = windowState.structure.atoms[atoms[1]].cartesianPosition -
								windowState.structure.atoms[atoms[0]].cartesianPosition;
							if (!fractional || std::abs(glm::determinant(windowState.structure.lattice)) > 1e-6f)
							{
								if (fractional) direction = glm::inverse(windowState.structure.lattice) * direction;
								SetCylinderAxis(windowState, volume, direction);
							}
						}
						ImGui::EndDisabled();
					}
				}
			}
		}
		if (ImGui::CollapsingHeader("Wymiary", open))
		{
			PropertyGrid grid("##HideVolumeDimensions", labelWidth);
			if (grid)
			{
				if (volume.kind == HideVolumeKind::Box)
				{
					grid.Row("Wymiary");
					DrawUndoableValue(windowState, volume.halfExtents, [&](glm::vec3 &value) {
						auto sides = 2.0f * value;
						if (!ImGui::DragFloat3("##sides", &sides.x, speed, minimum, std::numeric_limits<float>::max(), format,
							ImGuiSliderFlags_AlwaysClamp)) return false;
						value = 0.5f * glm::max(sides, glm::vec3(minimum));
						return true;
					});
				}
				else
				{
					grid.Row("Promień");
					DrawUndoableValue(windowState, volume.halfExtents.x, [&](float &value) {
						return ImGui::DragFloat("##radius", &value, speed, minimum, std::numeric_limits<float>::max(), format,
							ImGuiSliderFlags_AlwaysClamp);
					});
					if (volume.kind == HideVolumeKind::Cylinder)
					{
						grid.Row("Wysokość");
						DrawUndoableValue(windowState, volume.halfExtents.z, [&](float &value) {
							float height = 2.0f * value;
							if (!ImGui::DragFloat("##height", &height, speed, minimum, std::numeric_limits<float>::max(), format,
								ImGuiSliderFlags_AlwaysClamp)) return false;
							value = 0.5f * std::max(height, minimum);
							return true;
						});
					}
				}
			}
		}
		if (ImGui::CollapsingHeader("Wygląd", open))
		{
			PropertyGrid grid("##HideVolumeAppearance", labelWidth);
			if (grid)
			{
				grid.Row("Kolor");
				DrawUndoableValue(windowState, volume.color, [](glm::vec3 &value) { return ImGui::ColorEdit3("##color", &value.x); });
				grid.Row("Krycie");
				DrawUndoableValue(windowState, volume.alpha, [](float &value) {
					return ImGui::SliderFloat("##alpha", &value, 0, 1, "%.2f", ImGuiSliderFlags_AlwaysClamp);
				});
			}
		}
		if (ImGui::CollapsingHeader("Wynik", open))
		{
			PropertyGrid grid("##HideVolumeResult", labelWidth);
			if (grid)
			{
				char text[32];
				std::snprintf(text, sizeof(text), "%zu", CountAtomsHiddenBySceneHideVolume(windowState, volume));
				grid.Value("Ukryte atomy", text);
			}
		}
		ImGui::PopID();
	}
} // namespace DefectStudio
