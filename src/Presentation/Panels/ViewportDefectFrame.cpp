#include "Core/dspch.hpp"

#include "Presentation/Panels/ViewportDefectFrame.hpp"

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <imgui.h>

#include "Core/Commands/CommandRegistry.hpp"
#include "Core/Logging/Logger.hpp"
#include "Presentation/Panels/SceneOutlinerVisibilityColumns.hpp"
#include "Domain/Defects/DefectModel.hpp"
#include "Renderer/Commands/RendererVacancyCommands.hpp"
#include "Renderer/RendererLayer.hpp"
#include "Renderer/RendererViewCamera.hpp"
#include "Renderer/RendererWindowState.hpp"
#include "Renderer/Scene/SceneOrbitalAim.hpp"
#include "Renderer/Scene/SceneSystem.hpp"
#include "Renderer/Scene/SceneTransform.hpp"
#include "Renderer/Commands/SceneObjectsSnapshotCommand.hpp"

namespace DefectStudio
{
	namespace
	{

		[[nodiscard]] std::optional<glm::vec3> SelectionCentroid(const RendererWindowState &windowState)
		{
			glm::vec3 sum(0.0f);
			std::size_t count = 0;
			for (const std::size_t index : windowState.selectedAtomIndices)
				if (index < windowState.structure.atoms.size())
				{
					sum += windowState.structure.atoms[index].cartesianPosition;
					++count;
				}
			if (count == 0)
				return std::nullopt;
			return sum / static_cast<float>(count);
		}

		void SetFrame(RendererWindowState &windowState, CommandRegistry *registry, std::optional<DefectFrame> frame,
			const char *description)
		{
			if (registry == nullptr)
				return;
			CommandContext context;
			context.Set<SetDefectFramePayload>(
				kSetDefectFramePayloadKey, SetDefectFramePayload{windowState.windowId, frame, description});
			const auto result = registry->Execute(CommandID{kSetDefectFrameCommandId}, std::move(context));
			if (!result)
				DS_LOG_WARN("{} failed: {}", description, result.Error().technicalDetails);
		}

		void SetFrameFrom(RendererWindowState &windowState, CommandRegistry *registry, const Result<DefectFrame> &frame,
			const char *description)
		{
			if (!frame)
			{
				DS_LOG_WARN("{} failed: {}", description, frame.Error().technicalDetails);
				return;
			}
			SetFrame(windowState, registry, std::optional<DefectFrame>(*frame), description);
		}

		// Re-aim z / x at the selection, or reset to the world or cell axes; origin kept.
		void DrawDefectFrameAimItems(RendererWindowState &windowState, CommandRegistry *commandRegistry, bool asButtons);
	} // namespace

	namespace
	{
		struct ProjectedFrame
		{
			ImVec2 origin;
			std::optional<ImVec2> tips[3];
			std::optional<ImVec2> negativeTips[3]; // empty when the negative halves are not drawn
		};

		[[nodiscard]] std::optional<ProjectedFrame> ProjectFrame(
			const RendererWindowState &windowState, const ImVec2 &imageOrigin, const ImVec2 &imageSize)
		{
			const auto &frame = windowState.structure.defectFrame;
			if (!frame || !windowState.showDefectFrame || windowState.camera == nullptr || imageSize.x <= 0.0f ||
				imageSize.y <= 0.0f)
				return std::nullopt;
			const glm::mat4 viewProjection = windowState.camera->ProjectionMatrix() * windowState.camera->ViewMatrix();
			auto project = [&](const glm::vec3 &world) -> std::optional<ImVec2> {
				const glm::vec4 clip = viewProjection * glm::vec4(world, 1.0f);
				if (clip.w <= 0.0001f)
					return std::nullopt;
				return ImVec2(imageOrigin.x + (clip.x / clip.w * 0.5f + 0.5f) * imageSize.x,
					imageOrigin.y + (0.5f - clip.y / clip.w * 0.5f) * imageSize.y);
			};
			const auto origin = project(frame->origin);
			if (!origin)
				return std::nullopt;
			ProjectedFrame projected{*origin, {}};
			const glm::vec3 axes[3] = {frame->x, frame->y, frame->z};
			const float length = windowState.defectFrameAxisLength;
			for (int axis = 0; axis < 3; ++axis)
			{
				projected.tips[axis] = project(frame->origin + axes[axis] * length);
				if (windowState.defectFrameNegativeAxes)
					projected.negativeTips[axis] = project(frame->origin - axes[axis] * length);
			}
			return projected;
		}
	} // namespace

	bool IsDefectFrameUnderMouse(
		const RendererWindowState &windowState, const ImVec2 &imageOrigin, const ImVec2 &imageSize, const ImVec2 &mouse)
	{
		const auto projected = ProjectFrame(windowState, imageOrigin, imageSize);
		if (!projected)
			return false;
		const glm::vec2 point(mouse.x, mouse.y);
		const glm::vec2 origin(projected->origin.x, projected->origin.y);
		if (glm::length(point - origin) <= 10.0f)
			return true;
		for (const auto &tip : {projected->tips[0], projected->tips[1], projected->tips[2], projected->negativeTips[0],
				 projected->negativeTips[1], projected->negativeTips[2]})
		{
			if (!tip)
				continue;
			const glm::vec2 segment = glm::vec2(tip->x, tip->y) - origin;
			const float length2 = glm::dot(segment, segment);
			const float t =
				length2 > 0.0f ? glm::clamp(glm::dot(point - origin, segment) / length2, 0.0f, 1.0f) : 0.0f;
			if (glm::length(point - (origin + t * segment)) <= 6.0f)
				return true;
		}
		return false;
	}

	void DrawSelectedDefectFrameSection(RendererWindowState &windowState, CommandRegistry *commandRegistry)
	{
		const auto &frame = windowState.structure.defectFrame;
		if (!windowState.defectFrameSelected || !frame)
			return;
		ImGui::PushID("SelectedDefectFrame");
		ImGui::SeparatorText("Osie defektu");
		glm::vec3 origin = frame->origin;
		// Live preview on the renderer copy while the number is dragged; one undo step on release.
		if (ImGui::DragFloat3("Początek", &origin.x, 0.01f, 0.0f, 0.0f, "%.3f"))
			windowState.structure.defectFrame->origin = origin;
		if (ImGui::IsItemDeactivatedAfterEdit())
			SetFrame(windowState, commandRegistry, std::optional<DefectFrame>(*windowState.structure.defectFrame),
				"Move defect axes");
		// Rotation as XYZ Euler degrees of the frame. The typed angles live in a draft while the field is
		// dragged (a matrix -> Euler round trip can jump between equivalent triples); one undo on release.
		static std::optional<glm::vec3> rotationDraft;
		glm::vec3 degrees = rotationDraft.value_or(
			glm::degrees(glm::eulerAngles(glm::quat_cast(glm::mat3(frame->x, frame->y, frame->z)))));
		if (ImGui::DragFloat3("Obrót (°)", &degrees.x, 0.5f, -360.0f, 360.0f, "%.1f"))
		{
			const glm::mat3 axes = glm::mat3_cast(glm::quat(glm::radians(degrees)));
			windowState.structure.defectFrame->x = axes[0];
			windowState.structure.defectFrame->y = axes[1];
			windowState.structure.defectFrame->z = axes[2];
		}
		rotationDraft = ImGui::IsItemActive() ? std::optional<glm::vec3>(degrees) : std::nullopt;
		if (ImGui::IsItemDeactivatedAfterEdit())
			SetFrame(windowState, commandRegistry, std::optional<DefectFrame>(*windowState.structure.defectFrame),
				"Rotate defect axes");
		if (ImGui::TreeNode("Kierunki osi"))
		{
			ImGui::Text("x: (%.3f, %.3f, %.3f)", frame->x.x, frame->x.y, frame->x.z);
			ImGui::Text("y: (%.3f, %.3f, %.3f)", frame->y.x, frame->y.y, frame->y.z);
			ImGui::Text("z: (%.3f, %.3f, %.3f)", frame->z.x, frame->z.y, frame->z.z);
			ImGui::TreePop();
		}
		ImGui::SeparatorText("Wygląd");
		ImGui::DragFloat("Długość osi (A)", &windowState.defectFrameAxisLength, 0.02f, 0.1f, 20.0f, "%.2f");
		ImGui::DragFloat("Grubość (px)", &windowState.defectFrameAxisWidth, 0.1f, 0.5f, 12.0f, "%.1f");
		ImGui::Checkbox("Osie ujemne (-x, -y, -z)", &windowState.defectFrameNegativeAxes);
		ImGui::Checkbox("Pokaż (H ukrywa, Alt+H pokazuje; klawisze 1/2/3 w osiach defektu)", &windowState.showDefectFrame);
		if (ImGui::Button("Odwróć z"))
		{
			DefectFrame flipped = *frame;
			flipped.z = -flipped.z;
			flipped.x = -flipped.x;
			SetFrame(windowState, commandRegistry, std::optional<DefectFrame>(flipped), "Flip defect z axis");
		}
		ImGui::SameLine();
		if (ImGui::Button("Usuń (Del)"))
		{
			windowState.defectFrameSelected = false;
			SetFrame(windowState, commandRegistry, std::optional<DefectFrame>(), "Remove defect axes");
		}
		DrawDefectFrameAimItems(windowState, commandRegistry, true);
		ImGui::TextDisabled("G / R albo gizmo: przesuń / obróć osie (w ich własnych osiach).");
		ImGui::PopID();
	}

	void DrawViewportDefectFrameOverlay(
		const RendererWindowState &windowState, const ImVec2 &imageOrigin, const ImVec2 &imageSize)
	{
		const auto projected = ProjectFrame(windowState, imageOrigin, imageSize);
		if (!projected)
			return;
		ImDrawList &drawList = *ImGui::GetWindowDrawList();
		const float thickness = std::max(windowState.defectFrameAxisWidth, 0.5f);
		constexpr ImU32 kColors[3] = {IM_COL32(235, 80, 80, 255), IM_COL32(90, 200, 90, 255), IM_COL32(80, 140, 245, 255)};
		constexpr const char *kNames[3] = {"x", "y", "z"};
		for (int axis = 0; axis < 3; ++axis)
		{
			// The negative half in the same colour, dimmed, so +x and -x read apart.
			if (const auto &negative = projected->negativeTips[axis])
				drawList.AddLine(projected->origin, *negative, (kColors[axis] & IM_COL32(255, 255, 255, 0)) | IM_COL32(0, 0, 0, 140),
					thickness);
			if (const auto &tip = projected->tips[axis])
			{
				drawList.AddLine(projected->origin, *tip, kColors[axis], thickness);
				drawList.AddText(ImVec2(tip->x + 4.0f, tip->y - ImGui::GetFontSize() * 0.5f), kColors[axis], kNames[axis]);
			}
		}
		drawList.AddCircleFilled(projected->origin, thickness * 1.4f, IM_COL32(230, 230, 230, 255));
		if (windowState.defectFrameSelected)
			drawList.AddCircle(projected->origin, thickness * 4.0f, IM_COL32(255, 200, 60, 255), 32, 2.5f);
	}

	void DrawDefectFrameOutlinerRow(RendererWindowState &windowState)
	{
		if (!windowState.structure.defectFrame)
			return;
		ImGui::PushID("##defectFrameRow");
		ImGui::SetNextItemAllowOverlap();
		if (ImGui::Selectable("Osie defektu", windowState.defectFrameSelected))
		{
			const bool additive = ImGui::GetIO().KeyCtrl;
			windowState.defectFrameSelected = additive ? !windowState.defectFrameSelected : true;
			if (!additive)
			{
				windowState.selectedVacancies.clear();
				windowState.selectedFreeLabels.clear();
				windowState.selectedPinnedMeasurements.clear();
				windowState.selectedSceneOrbitals.clear();
				windowState.selectedScenePlanes.clear();
				windowState.selectedScenePaths.clear();
				SceneSystem::ClearStructureSelection(windowState.sceneRegistry, windowState);
			}
		}
		// One flag behind both columns, as for the vacancy group: the axes are never exported.
		const SceneVisibilityColumnEdit edit =
			DrawSceneVisibilityColumns({windowState.showDefectFrame, windowState.showDefectFrame});
		if (edit.visibleChanged)
			windowState.showDefectFrame = edit.visible;
		else if (edit.renderableChanged)
			windowState.showDefectFrame = edit.renderable;
		ImGui::PopID();
	}

	namespace
	{
		// Each selected arrow / plane / orbital / path on its own: its local basis and its origin.
		struct AlignTarget
		{
			SceneTransformSelectionSnapshot snapshot;
			glm::mat3 basis;
		};

		[[nodiscard]] std::vector<AlignTarget> AlignTargets(const RendererWindowState &windowState)
		{
			const SceneTransformSelectionSnapshot all = CaptureSceneTransformSelection(windowState);
			std::vector<AlignTarget> targets;
			auto addWithLocalBasis = [&](SceneTransformSelectionSnapshot one) {
				if (const auto basis = SceneTransformLocalBasis(one))
					targets.push_back({std::move(one), *basis});
			};
			for (const PlaneTransformStart &plane : all.planes)
				addWithLocalBasis(SceneTransformSelectionSnapshot{.planes = {plane}});
			for (const PathTransformStart &path : all.paths)
				addWithLocalBasis(SceneTransformSelectionSnapshot{.paths = {path}});
			for (const OrbitalTransformStart &orbital : all.orbitals)
				if (orbital.index < windowState.sceneOrbitals.size() &&
					windowState.sceneOrbitals[orbital.index].lcaoComponents.empty())
					targets.push_back({SceneTransformSelectionSnapshot{.orbitals = {orbital}},
						glm::mat3_cast(glm::quat(glm::radians(orbital.rotationEuler)))});
			return targets;
		}

		// Turns every selected object about its own origin so its local x/y/z become the defect
		// axes (an arrow or line then points along z, a plane's normal is z). One undo step.
		void AlignSelectionToDefectFrame(RendererWindowState &windowState)
		{
			const auto &frame = windowState.structure.defectFrame;
			const std::vector<AlignTarget> targets = AlignTargets(windowState);
			if (!frame || targets.empty())
				return;
			const glm::mat3 axes(frame->x, frame->y, frame->z);
			SceneObjectsSnapshot before = CaptureSceneObjectsSnapshot(windowState);
			for (const AlignTarget &target : targets)
			{
				SceneTransformDelta delta;
				delta.spatial.rotation = glm::normalize(glm::quat_cast(axes * glm::transpose(target.basis)));
				ApplySceneTransformSelection(windowState, target.snapshot, delta, ModalTransformOp::Rotate,
					TransformPivotMode::IndividualOrigins, glm::vec3(0.0f));
			}
			PushSceneObjectsUndoSnapshot(windowState, std::move(before));
		}

	} // namespace

	void ParentSelectionToDefectFrame(RendererWindowState &windowState)
	{
		auto &children = windowState.defectFrameChildren;
		auto add = [](auto &list, const auto &selected) {
			for (const auto &item : selected)
				if (std::find(list.begin(), list.end(), item) == list.end())
					list.push_back(item);
		};
		add(children.freeLabels, windowState.selectedFreeLabels);
		add(children.paths, windowState.selectedScenePaths);
		add(children.orbitals, windowState.selectedSceneOrbitals);
		add(children.planes, windowState.selectedScenePlanes);
		add(children.vacancies, windowState.selectedVacancies);
	}

	namespace
	{
		[[nodiscard]] bool AnyParentableSelected(const RendererWindowState &windowState)
		{
			return !windowState.selectedFreeLabels.empty() || !windowState.selectedScenePaths.empty() ||
				!windowState.selectedSceneOrbitals.empty() || !windowState.selectedScenePlanes.empty() ||
				!windowState.selectedVacancies.empty();
		}

		void SelectDefectFrameOnly(RendererWindowState &windowState)
		{
			windowState.selectedVacancies.clear();
			windowState.selectedFreeLabels.clear();
			windowState.selectedPinnedMeasurements.clear();
			windowState.selectedSceneOrbitals.clear();
			windowState.selectedScenePlanes.clear();
			windowState.selectedScenePaths.clear();
			SceneSystem::ClearStructureSelection(windowState.sceneRegistry, windowState);
			windowState.defectFrameSelected = true;
		}
	} // namespace

	namespace
	{
		void DrawDefectFrameAimItems(RendererWindowState &windowState, CommandRegistry *commandRegistry, bool asButtons)
		{
			const auto &frame = windowState.structure.defectFrame;
			if (!frame)
				return;
			const auto centroid = SelectionCentroid(windowState);
			auto item = [&](const char *label, bool enabled) {
				if (!asButtons)
					return ImGui::MenuItem(label, nullptr, false, enabled);
				ImGui::BeginDisabled(!enabled);
				const bool pressed = ImGui::Button(label);
				ImGui::EndDisabled();
				return pressed;
			};
			if (item("z w stronę zaznaczenia", centroid.has_value()))
				SetFrameFrom(windowState, commandRegistry, MakeDefectFrame(frame->origin, *centroid, frame->origin + frame->x),
					"Aim defect z axis");
			if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
				ImGui::SetTooltip("Początek zostaje, z celuje w środek zaznaczonych atomów (NV: zaznacz N).");
			if (asButtons)
				ImGui::SameLine();
			if (item("x w stronę zaznaczenia", centroid.has_value()))
				SetFrameFrom(windowState, commandRegistry, MakeDefectFrame(frame->origin, frame->origin + frame->z, *centroid),
					"Turn defect x axis");
			if (item("Osie globalne", true))
				SetFrame(windowState, commandRegistry,
					std::optional<DefectFrame>(
						DefectFrame{frame->origin, glm::vec3(1, 0, 0), glm::vec3(0, 1, 0), glm::vec3(0, 0, 1)}),
					"Reset defect axes");
			if (asButtons)
				ImGui::SameLine();
			// z along c, x toward a (orthogonalised): the crystal's own axes for a hexagonal cell.
			const glm::mat3 &lattice = windowState.structure.lattice;
			if (item("Osie kryształu (z = c)", glm::length(lattice[2]) > 1.0e-4f))
				SetFrameFrom(windowState, commandRegistry,
					MakeDefectFrame(frame->origin, frame->origin + lattice[2], frame->origin + lattice[0]),
					"Align defect axes to the cell");
		}
	} // namespace

	void DrawDefectFrameAddMenu(RendererWindowState &windowState, CommandRegistry *commandRegistry, const glm::vec3 &position)
	{
		if (!ImGui::BeginMenu("Defect axes (empty)", !windowState.structure.domainStructureId.empty()))
			return;
		const auto centroid = SelectionCentroid(windowState);
		const bool hasVacancy = !windowState.structure.vacancies.empty();
		if (windowState.structure.defectFrame)
			ImGui::TextDisabled("Zastępuje obecne osie (Ctrl+Z cofa).");

		if (ImGui::MenuItem("Tutaj, osie globalne"))
			SetFrame(windowState, commandRegistry,
				std::optional<DefectFrame>(DefectFrame{position, glm::vec3(1, 0, 0), glm::vec3(0, 1, 0), glm::vec3(0, 0, 1)}),
				"Add defect axes");
		if (ImGui::MenuItem("z: wakans → zaznaczenie", nullptr, false, centroid && hasVacancy))
		{
			const auto vacancy = ResolveDanglingBondTarget(windowState, windowState.selectedAtomIndices);
			if (vacancy)
				SetFrameFrom(windowState, commandRegistry, MakeDefectFrame(*vacancy, *centroid), "Set defect axes");
		}
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
			ImGui::SetTooltip(hasVacancy ? "Początek w najbliższym wakansie, z w stronę zaznaczonych atomów (NV: zaznacz N)."
										 : "Struktura nie ma wakansu.");
		const auto &selection = windowState.selectedAtomIndices;
		const bool twoAtoms = selection.size() == 2 && selection[0] < windowState.structure.atoms.size() &&
			selection[1] < windowState.structure.atoms.size();
		if (ImGui::MenuItem("z: atom 1 → atom 2", nullptr, false, twoAtoms))
			SetFrameFrom(windowState, commandRegistry,
				MakeDefectFrame(windowState.structure.atoms[selection[0]].cartesianPosition,
					windowState.structure.atoms[selection[1]].cartesianPosition),
				"Set defect axes");
		if (ImGui::MenuItem("z: kursor 3D → zaznaczenie", nullptr, false, centroid.has_value()))
			SetFrameFrom(windowState, commandRegistry, MakeDefectFrame(windowState.cursor3DPosition, *centroid),
				"Set defect axes");
		// The object's own axes: an arrow/line gives z along it, a plane z along its normal.
		const std::vector<AlignTarget> objects = AlignTargets(windowState);
		if (ImGui::MenuItem("Z osi zaznaczonego obiektu", nullptr, false, !objects.empty()))
		{
			const AlignTarget &object = objects.front();
			const glm::vec3 origin = SceneTransformPivotPositions(object.snapshot).front();
			SetFrameFrom(windowState, commandRegistry,
				MakeDefectFrame(origin, origin + object.basis[2], origin + object.basis[0]), "Set defect axes");
		}
		ImGui::EndMenu();
	}

	void DrawDefectFrameMenu(RendererWindowState &windowState, CommandRegistry *commandRegistry)
	{
		const auto &frame = windowState.structure.defectFrame;
		// No axes yet: offer to create them here instead of a greyed-out menu.
		if (!frame)
		{
			DrawDefectFrameAddMenu(windowState, commandRegistry,
				SelectionCentroid(windowState).value_or(windowState.cursor3DPosition));
			return;
		}
		if (!ImGui::BeginMenu("Osie defektu"))
			return;

		if (ImGui::MenuItem("Zaznacz osie"))
			SelectDefectFrameOnly(windowState);
		const bool hasObjects = !AlignTargets(windowState).empty();
		const bool hasPoints = !windowState.selectedAtomIndices.empty() || !windowState.selectedVacancies.empty();
		if (ImGui::MenuItem("Wyrównaj zaznaczone do osi", nullptr, false, hasObjects || hasPoints))
		{
			AlignSelectionToDefectFrame(windowState);
			// Points cannot turn: aligning them means moving them along the defect axes.
			windowState.transformOrientation = TransformOrientation::Defect;
		}
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
			ImGui::SetTooltip("Strzałki, linie, płaszczyzny, orbitale, ścieżki: lokalne x/y/z = osie defektu\n"
							  "(strzałka wzdłuż z, normalna płaszczyzny = z). Obrót wokół własnego środka.\n"
							  "Atomy i wakanse: gizmo i G/R/S + X/Y/Z przechodzą na osie defektu.");
		bool defectOrientation = windowState.transformOrientation == TransformOrientation::Defect;
		if (ImGui::MenuItem("Gizmo i G/R/S w osiach defektu", nullptr, &defectOrientation))
			windowState.transformOrientation = defectOrientation ? TransformOrientation::Defect : TransformOrientation::Global;

		ImGui::Separator();
		const std::size_t childCount = windowState.defectFrameChildren.Count();
		if (ImGui::MenuItem("Przypnij zaznaczone do osi", nullptr, false, AnyParentableSelected(windowState)))
		{
			ParentSelectionToDefectFrame(windowState);
			SelectDefectFrameOnly(windowState);
		}
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
			ImGui::SetTooltip("Tymczasowe rodzicielstwo: G/R/S osi niesie przypięte obiekty wokół początku osi.\n"
							  "Tylko w tej sesji. Bez przypinania: zaznacz obiekty, Ctrl+klik w osie, G/R.");
		const std::string unparent = "Odepnij wszystkie (" + std::to_string(childCount) + ")";
		if (ImGui::MenuItem(unparent.c_str(), nullptr, false, childCount > 0))
			windowState.defectFrameChildren = {};

		ImGui::Separator();
		DrawDefectFrameAimItems(windowState, commandRegistry, false);
		if (ImGui::MenuItem("Odwróć z"))
		{
			DefectFrame flipped = *frame;
			flipped.z = -flipped.z;
			flipped.x = -flipped.x;
			SetFrame(windowState, commandRegistry, std::optional<DefectFrame>(flipped), "Flip defect z axis");
		}
		ImGui::MenuItem("Pokaż osie (klawisze 1/2/3)", nullptr, &windowState.showDefectFrame);
		if (ImGui::MenuItem("Usuń osie defektu"))
		{
			windowState.defectFrameSelected = false;
			windowState.defectFrameChildren = {};
			SetFrame(windowState, commandRegistry, std::optional<DefectFrame>(), "Remove defect axes");
		}
		ImGui::EndMenu();
	}
} // namespace DefectStudio
