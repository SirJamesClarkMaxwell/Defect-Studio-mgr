#include "Core/dspch.hpp"

#include "Presentation/Panels/CellPanelSelectionOps.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <limits>
#include <unordered_map>

#include <imgui.h>

#include "Core/Commands/CommandRegistry.hpp"
#include "Domain/Crystal/CellEditing.hpp"
#include "Domain/DomainLayer.hpp"
#include "Renderer/Commands/RendererAtomEditCommands.hpp"
#include "Renderer/Commands/RendererCellEditCommand.hpp"
#include "Renderer/RendererLayer.hpp"
#include "Renderer/Scene/SceneTransform.hpp"

namespace DefectStudio
{
	namespace
	{
		struct SelectionPopup
		{
			CellSelectionOperation operation = CellSelectionOperation::Repeat;
			int vectorMode = 0;
			int mirrorMode = 0;
			bool copy = true;
			std::array<char, 24> count = {"3"};
			std::array<std::array<char, 24>, 3> shift = {{{"1/4"}, {"0"}, {"0"}}};
			std::string key;
			std::optional<CrystalStructure> preview;
			std::size_t added = 0;
			std::size_t coincident = 0;
			std::string reason;
			std::string status;
		};

		// Both entry points use the same per-viewport inputs and cached domain preview.
		std::unordered_map<std::string, SelectionPopup> s_Popups;

		void DrawInputs(SelectionPopup &popup)
		{
			if (popup.operation == CellSelectionOperation::Repeat)
			{
				ImGui::TextUnformatted("Liczba kopii");
				ImGui::InputText("##count", popup.count.data(), popup.count.size());
				ImGui::SetItemTooltip("Dodatnia liczba całkowita kopii; można wpisać ułamek, np. 6/2.");
				ImGui::TextUnformatted("Wektor przesunięcia");
				ImGui::Combo("##vector_mode", &popup.vectorMode, "Ułamki a/b/c\0Kartezjański [Å]\0Pierwszy → ostatni atom\0");
				ImGui::SetItemTooltip("Kopia k jest przesunięta o k razy ten wektor. Wektor atomów wymaga dwóch zaznaczonych atomów.");
				if (popup.vectorMode != 2 && ImGui::BeginTable("shift", 3, ImGuiTableFlags_SizingStretchSame))
				{
					ImGui::TableNextRow();
					for (int axis = 0; axis < 3; ++axis)
					{
						ImGui::TableSetColumnIndex(axis);
						ImGui::TextUnformatted(popup.vectorMode == 0 ? (axis == 0 ? "a" : axis == 1 ? "b" : "c") :
							(axis == 0 ? "x [Å]" : axis == 1 ? "y [Å]" : "z [Å]"));
						ImGui::PushID(axis);
						ImGui::SetNextItemWidth(-1.0f);
						ImGui::InputText("##shift", popup.shift[axis].data(), popup.shift[axis].size());
						ImGui::SetItemTooltip("Składowa wektora; liczba lub ułamek, np. -1/8. Bez zawijania do komórki.");
						ImGui::PopID();
					}
					ImGui::EndTable();
				}
			}
			else
			{
				ImGui::TextUnformatted("Płaszczyzna przez kursor 3D");
				ImGui::Combo("##mirror_mode", &popup.mirrorMode,
					"Normalna x (bieżąca orientacja)\0Normalna y (bieżąca orientacja)\0Normalna z (bieżąca orientacja)\0Płaszczyzna ab\0Płaszczyzna ac\0Płaszczyzna bc\0Inwersja przez kursor 3D\0");
				ImGui::SetItemTooltip("Punkt płaszczyzny lub inwersji: kursor 3D (początek układu, gdy nie ustawiono). ab/ac/bc używają pełnych wektorów komórki.");
				ImGui::Checkbox("Kopiuj", &popup.copy);
				ImGui::SetItemTooltip("Włączone: zachowaj oryginały i dodaj obrazy. Wyłączone: przesuń zaznaczone atomy. Jedno cofnięcie.");
			}
		}

		void RefreshPreview(SelectionPopup &popup, const CrystalStructure &structure, const RendererWindowState &window,
			int revision, bool hasCommands)
		{
			const auto snapshot = CaptureSceneTransformSelection(window);
			const auto orientation = SceneTransformOrientation(window.transformOrientation, snapshot);
			const auto basis = ResolveBasis(orientation, SceneTransformBases(window, snapshot));
			const auto point = window.cursor3DPlaced ? window.cursor3DPosition : glm::vec3(0.0f);
			std::string key = std::format("{}:{}:{}:{}:{}:{}:{}:{}:{}:{}:{}", window.structure.domainStructureId,
				revision, static_cast<int>(popup.operation), popup.vectorMode, popup.mirrorMode, popup.copy,
				popup.count.data(), hasCommands, window.modalTransform.has_value(), point.x, point.y);
			key += std::format(":{}:{}:{}:{}", point.z, popup.shift[0].data(), popup.shift[1].data(), popup.shift[2].data());
			for (int axis = 0; axis < 3; ++axis)
				key += std::format(":{}:{}:{}", basis[axis].x, basis[axis].y, basis[axis].z);
			for (const auto index : window.selectedAtomIndices)
				key += std::format(":{}", index);
			if (key == popup.key)
				return;
			popup.key = std::move(key);
			popup.preview.reset();
			popup.reason.clear();
			popup.status.clear();
			popup.added = popup.coincident = 0;
			if (!hasCommands)
				popup.reason = "Komendy edycji są niedostępne.";
			else if (window.modalTransform)
				popup.reason = "Zakończ transformację przed edycją atomów.";
			else if (!structure.isPeriodic)
				popup.reason = "Komenda zastąpienia wymaga struktury okresowej.";
			else if (window.selectedAtomIndices.empty())
				popup.reason = "Zaznacz atomy.";
			else if (std::any_of(window.selectedAtomIndices.begin(), window.selectedAtomIndices.end(),
				[&](std::size_t index) { return index >= structure.atoms.size(); }))
				popup.reason = "Zaznaczenie zawiera nieaktualne indeksy.";
			if (!popup.reason.empty())
				return;
			const auto validStructure = WithCell(structure, structure.cell, CellEditPositions::KeepCartesian);
			if (!validStructure)
			{
				popup.reason = validStructure.Error().userMessage;
				return;
			}
			std::vector<std::size_t> indices;
			for (const auto index : window.selectedAtomIndices)
				if (std::find(indices.begin(), indices.end(), index) == indices.end())
					indices.push_back(index);
			std::vector<std::size_t> coincident;
			if (popup.operation == CellSelectionOperation::Repeat)
			{
				const auto count = ParseNumericText(popup.count.data());
				const auto maximum = static_cast<std::size_t>(std::numeric_limits<int>::max());
				const auto available = structure.atoms.size() <= maximum ? maximum - structure.atoms.size() : 0;
				if (!count || *count < 1.0f || std::floor(*count) != *count ||
					static_cast<double>(*count) > available / indices.size())
				{
					popup.reason = "Podaj dodatnią, całkowitą liczbę kopii mieszczącą się w limicie indeksów atomów.";
					return;
				}
				glm::vec3 shift(0.0f);
				if (popup.vectorMode == 2)
				{
					if (indices.size() != 2)
					{
						popup.reason = "Ten wektor wymaga dokładnie dwóch zaznaczonych atomów (pierwszy → ostatni).";
						return;
					}
					shift = structure.atoms[indices.back()].position - structure.atoms[indices.front()].position;
				}
				else
				{
					for (int axis = 0; axis < 3; ++axis)
					{
						const auto value = ParseNumericText(popup.shift[axis].data());
						if (!value)
						{
							popup.reason = "Nieprawidłowa składowa wektora; wpisz liczbę lub ułamek.";
							return;
						}
						shift[axis] = *value;
					}
					if (popup.vectorMode == 0)
						shift = structure.FractionalToCartesian(shift);
				}
				popup.preview = RepeatAtoms(structure, indices, shift, static_cast<int>(*count), coincident);
				if (popup.preview->atoms.size() != structure.atoms.size() + indices.size() * static_cast<std::size_t>(*count))
				{
					popup.preview.reset();
					popup.reason = "Wektor tworzy nieprawidłowe lub nieskończone pozycje.";
					return;
				}
			}
			else
			{
				if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z))
				{
					popup.reason = "Kursor 3D ma nieprawidłową pozycję.";
					return;
				}
				glm::vec3 normal(0.0f);
				if (popup.mirrorMode < 3)
					normal = basis[popup.mirrorMode];
				else if (popup.mirrorMode < 6)
				{
					const auto &vectors = structure.cell.vectors;
					normal = popup.mirrorMode == 3 ? glm::cross(vectors[0], vectors[1]) :
						popup.mirrorMode == 4 ? glm::cross(vectors[0], vectors[2]) : glm::cross(vectors[1], vectors[2]);
				}
				if (popup.mirrorMode != 6 && (!std::isfinite(glm::length(normal)) || glm::length(normal) <= 1e-7f))
				{
					popup.reason = "Ta orientacja nie określa poprawnej płaszczyzny.";
					return;
				}
				popup.preview = MirrorAtoms(structure, indices, point, normal, popup.copy, coincident);
				if (popup.copy && popup.preview->atoms.size() != structure.atoms.size() + indices.size())
				{
					popup.preview.reset();
					popup.reason = "Odbicie tworzy nieprawidłowe lub nieskończone pozycje.";
					return;
				}
			}
			popup.added = popup.preview->atoms.size() - structure.atoms.size();
			for (const auto index : coincident)
				if (index >= structure.atoms.size() || (popup.operation == CellSelectionOperation::Mirror && !popup.copy &&
					std::find(indices.begin(), indices.end(), index) != indices.end()))
					++popup.coincident;
		}
	} // namespace

	std::optional<CellSelectionOperation> DrawCellSelectionOperationButtons(bool menu)
	{
		std::optional<CellSelectionOperation> requested;
		if (menu ? ImGui::MenuItem("Powtórz…") : ImGui::Button("Powtórz…"))
			requested = CellSelectionOperation::Repeat;
		ImGui::SetItemTooltip("Dodaj szereg kopii zaznaczonych atomów wzdłuż wektora. Bez zawijania; jedno cofnięcie.");
		if (!menu)
			ImGui::SameLine();
		if (menu ? ImGui::MenuItem("Odbij…") : ImGui::Button("Odbij…"))
			requested = CellSelectionOperation::Mirror;
		ImGui::SetItemTooltip("Odbij zaznaczone atomy przez płaszczyznę lub kursor 3D; kopiuj albo przenieś.");
		return requested;
	}

	void DrawCellSelectionOperationPopups(const std::string &windowId, RendererLayer &layer,
		const WeakRef<CommandRegistry> &commandsRef, const WeakRef<DomainLayer> &domainRef,
		std::optional<CellSelectionOperation> requested)
	{
		auto &popup = s_Popups[windowId];
		ImGui::PushID(windowId.c_str());
		if (requested)
		{
			popup.operation = *requested;
			popup.key.clear();
			popup.status.clear();
			ImGui::OpenPopup(*requested == CellSelectionOperation::Repeat ? "Powtórz atomy" : "Odbij atomy");
		}
		for (const auto operation : {CellSelectionOperation::Repeat, CellSelectionOperation::Mirror})
		{
			bool open = true;
			if (!ImGui::BeginPopupModal(operation == CellSelectionOperation::Repeat ? "Powtórz atomy" : "Odbij atomy",
				&open, ImGuiWindowFlags_AlwaysAutoResize))
				continue;
			DrawInputs(popup);
			const auto domain = domainRef.lock();
			const auto commands = commandsRef.lock();
			if (domain)
			{
				const auto target = ResolveAtomEditTarget(layer, *domain, windowId);
				if (target)
					RefreshPreview(popup, target->record->structure, *target->windowState, target->record->revision, commands != nullptr);
				else
				{
					popup.preview.reset();
					popup.reason = "To okno nie zawiera edytowalnej struktury.";
				}
			}
			else
			{
				popup.preview.reset();
				popup.reason = "Warstwa domeny jest niedostępna.";
			}
			if (popup.preview)
				ImGui::Text("doda %zu atomów, %zu nakładają się", popup.added, popup.coincident);
			if (!popup.reason.empty())
				ImGui::TextWrapped("%s", popup.reason.c_str());
			ImGui::BeginDisabled(!popup.preview || !commands);
			if (ImGui::Button("Wykonaj"))
			{
				CommandContext context;
				context.Set<ReplaceStructurePayload>(kReplaceStructurePayloadKey, ReplaceStructurePayload{windowId,
					*popup.preview, operation == CellSelectionOperation::Repeat ? "Powtórz atomy" : "Odbij atomy", false});
				const auto result = commands->Execute(CommandID{kReplaceStructureCommandId}, std::move(context));
				if (result)
					ImGui::CloseCurrentPopup();
				else
					popup.status = result.Error().userMessage;
			}
			ImGui::SetItemTooltip("Zastosuj podgląd przez komendę zastąpienia struktury. Jedno cofnięcie; kolizje są zachowane.");
			ImGui::EndDisabled();
			ImGui::SameLine();
			if (ImGui::Button("Anuluj"))
				ImGui::CloseCurrentPopup();
			ImGui::SetItemTooltip("Zamknij bez zmiany struktury.");
			if (!popup.status.empty())
				ImGui::TextWrapped("%s", popup.status.c_str());
			ImGui::EndPopup();
		}
		ImGui::PopID();
	}
} // namespace DefectStudio
