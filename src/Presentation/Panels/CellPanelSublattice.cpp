// task/112d: "Dodaj podsieć" (a species on a visual centering pattern plus an offset) and a new empty cell -
// building a cell such as chalcopyrite without space groups or Wyckoff letters. Split out of CellPanel.cpp.
#include "Core/dspch.hpp"

#include "Presentation/Panels/CellPanel.hpp"

#include <algorithm>
#include <format>

#include <imgui.h>

#include "Domain/Crystal/LatticeBasisExpansion.hpp"
#include "Domain/DomainLayer.hpp"
#include "Presentation/Panels/PeriodicTableGrid.hpp"
#include "Renderer/OpenCrystalStructureAsWindow.hpp"
#include "Renderer/Scene/ModalTransform.hpp"

namespace DefectStudio
{
	namespace
	{
		struct PatternChoice
		{
			BravaisCenteringPreset preset;
			const char *caption;
			const char *tooltip;
		};

		constexpr std::array<PatternChoice, 4> kPatterns = {{
			{BravaisCenteringPreset::Primitive, "narożniki", "Tylko narożniki komórki: 1 atom na komórkę."},
			{BravaisCenteringPreset::BodyCentered, "+ środek", "Narożniki i środek komórki: 2 atomy na komórkę."},
			{BravaisCenteringPreset::FaceCentered, "+ środki ścian", "Narożniki i środki wszystkich ścian: 4 atomy na komórkę."},
			{BravaisCenteringPreset::BaseCentered, "+ środki podstaw", "Narożniki i środki ścian ab (góra i dół): 2 atomy na komórkę."},
		}};

		// A small oblique cube with the lattice points of `preset` as dots; true when clicked.
		bool PatternButton(const PatternChoice &choice, bool selected)
		{
			const float size = ImGui::GetFrameHeight() * 2.2f;
			ImGui::PushID(choice.caption);
			const bool clicked = ImGui::InvisibleButton("##pattern", ImVec2(size, size));
			const ImVec2 min = ImGui::GetItemRectMin();
			ImDrawList *draw = ImGui::GetWindowDrawList();
			const ImU32 frame = ImGui::GetColorU32(selected ? ImGuiCol_CheckMark : ImGui::IsItemHovered() ? ImGuiCol_ButtonHovered : ImGuiCol_Border);
			draw->AddRect(min, ImVec2(min.x + size, min.y + size), frame, 3.0f, 0, selected ? 2.0f : 1.0f);
			const float side = size * 0.55f, depth = size * 0.22f, left = min.x + size * 0.12f, bottom = min.y + size * 0.85f;
			const auto project = [&](const glm::vec3 &p) { return ImVec2(left + p.x * side + p.z * depth, bottom - p.y * side - p.z * depth); };
			const ImU32 edge = ImGui::GetColorU32(ImGuiCol_TextDisabled);
			for (int axis = 0; axis < 3; ++axis)
				for (int a = 0; a < 2; ++a)
					for (int b = 0; b < 2; ++b)
					{
						glm::vec3 from(0.0f), to(0.0f);
						from[(axis + 1) % 3] = to[(axis + 1) % 3] = static_cast<float>(a);
						from[(axis + 2) % 3] = to[(axis + 2) % 3] = static_cast<float>(b);
						to[axis] = 1.0f;
						draw->AddLine(project(from), project(to), edge);
					}
			const ImU32 dot = ImGui::GetColorU32(ImGuiCol_Text);
			for (const glm::vec3 &translation : GetCenteringTranslations(choice.preset))
				for (int corner = 0; corner < 8; ++corner)
				{
					// Every periodic image inside the drawn cube: (0,0,0) shows on all 8 corners, a face centre on both faces.
					const glm::vec3 point = translation + glm::vec3(corner & 1, (corner >> 1) & 1, (corner >> 2) & 1);
					if (point.x <= 1.0f && point.y <= 1.0f && point.z <= 1.0f)
						draw->AddCircleFilled(project(point), size * 0.045f, dot);
				}
			ImGui::SetItemTooltip("%s", choice.tooltip);
			ImGui::PopID();
			return clicked;
		}
	} // namespace

	void CellPanel::drawSublattice(const CrystalStructure &structure, const int revision)
	{
		ImGui::SeparatorText("Dodaj podsieć");
		ImGui::TextUnformatted("Wzór");
		for (const auto &choice : kPatterns)
		{
			ImGui::BeginGroup();
			if (PatternButton(choice, m_Pattern == choice.preset))
				m_Pattern = choice.preset;
			ImGui::TextDisabled("%s", choice.caption);
			ImGui::EndGroup();
			ImGui::SameLine();
		}
		ImGui::NewLine();

		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted("Pierwiastek");
		ImGui::SameLine();
		if (ImGui::Button((m_Species.empty() ? std::string("(wybierz)") : m_Species).append("###species").c_str()))
			ImGui::OpenPopup("##sublatticeElement");
		ImGui::SetItemTooltip("Pierwiastek nowej podsieci (układ okresowy).");
		if (ImGui::BeginPopup("##sublatticeElement"))
		{
			const std::string clicked = DrawPeriodicTableGrid(m_Layer,
				[&](const std::string &symbol) { return CategoryColor(ClassifyElement(AtomicNumberForSymbol(m_Layer, symbol))); },
				m_Species);
			if (!clicked.empty())
			{
				m_Species = clicked;
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}

		ImGui::TextUnformatted("Przesunięcie (ułamki a, b, c)");
		glm::vec3 offset(0.0f);
		bool offsetValid = true;
		if (ImGui::BeginTable("##sublatticeOffset", 3, ImGuiTableFlags_SizingStretchSame))
		{
			ImGui::TableNextRow();
			for (int axis = 0; axis < 3; ++axis)
			{
				ImGui::TableSetColumnIndex(axis);
				ImGui::PushID(axis);
				ImGui::SetNextItemWidth(-1.0f);
				ImGui::InputText("##offset", m_Offset[axis].data(), m_Offset[axis].size());
				ImGui::SetItemTooltip("Przesunięcie wzdłuż %c jako ułamek wektora: 0, 1/4, -1/8, 0.37 ...", "abc"[axis]);
				const auto value = ParseNumericText(m_Offset[axis].data());
				if (value)
					offset[axis] = *value;
				else
				{
					offsetValid = false;
					ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "nieprawidłowe");
				}
				ImGui::PopID();
			}
			ImGui::EndTable();
		}

		const std::string key = std::format("{}|{}|{}|{}|{}|{}|{}", revision, static_cast<int>(m_Pattern), m_Species,
			offset.x, offset.y, offset.z, structure.atoms.size());
		if (offsetValid && !m_Species.empty() && key != m_PreviewKey)
		{
			std::vector<std::size_t> coincident;
			const CrystalStructure preview = AddSublattice(structure, m_Species, m_Pattern, offset, coincident);
			m_PreviewNew = preview.atoms.size() - structure.atoms.size();
			m_PreviewCoincident = static_cast<std::size_t>(std::count_if(coincident.begin(), coincident.end(),
				[&](std::size_t index) { return index >= structure.atoms.size(); }));
			m_PreviewKey = key;
		}
		const bool ready = offsetValid && !m_Species.empty();
		if (ready)
		{
			ImGui::Text("Doda %zu atomów (%s)", m_PreviewNew, m_Species.c_str());
			if (m_PreviewCoincident > 0)
				ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "Nakładające się z istniejącymi: %zu", m_PreviewCoincident);
		}
		ImGui::BeginDisabled(!ready);
		if (ImGui::Button("Dodaj"))
		{
			std::vector<std::size_t> coincident;
			(void)replace(AddSublattice(structure, m_Species, m_Pattern, offset, coincident), "Dodaj podsieć");
		}
		ImGui::EndDisabled();
		ImGui::SetItemTooltip(ready ? "Dodaj atomy podsieci do tej komórki (jedno cofnięcie). Nakładające się atomy nie są usuwane."
			: m_Species.empty() ? "Wybierz pierwiastek." : "Popraw przesunięcie (liczba lub ułamek, np. 1/4).");
	}

	void CellPanel::drawNewCell()
	{
		if (ImGui::Button("Nowa pusta komórka..."))
			ImGui::OpenPopup("##newEmptyCell");
		ImGui::SetItemTooltip("Otwórz nowe okno z pustą komórką o zadanych parametrach; atomy dodasz podsieciami.");
		if (!ImGui::BeginPopup("##newEmptyCell"))
			return;
		constexpr std::array<std::pair<CrystalSystem, const char *>, 7> systems = {{
			{CrystalSystem::Cubic, "regularny"}, {CrystalSystem::Tetragonal, "tetragonalny"},
			{CrystalSystem::Orthorhombic, "rombowy"}, {CrystalSystem::Hexagonal, "heksagonalny"},
			{CrystalSystem::Trigonal, "trygonalny (romboedryczny)"}, {CrystalSystem::Monoclinic, "jednoskośny"},
			{CrystalSystem::Triclinic, "trójskośny"}}};
		const auto current = std::find_if(systems.begin(), systems.end(), [&](const auto &entry) { return entry.first == m_NewSystem; });
		ImGui::TextUnformatted("Układ krystalograficzny");
		if (ImGui::BeginCombo("##newSystem", current->second))
		{
			for (const auto &[system, name] : systems)
				if (ImGui::Selectable(name, system == m_NewSystem))
					m_NewSystem = system;
			ImGui::EndCombo();
		}
		const LatticeFieldConstraints locks = GetFieldConstraints(m_NewSystem);
		const std::array<std::pair<const char *, float *>, 6> fields = {{{"a [Å]", &m_NewParameters.a},
			{"b [Å]", &m_NewParameters.b}, {"c [Å]", &m_NewParameters.c}, {"α [deg]", &m_NewParameters.alphaDegrees},
			{"β [deg]", &m_NewParameters.betaDegrees}, {"γ [deg]", &m_NewParameters.gammaDegrees}}};
		const std::array<bool, 6> locked = {false, locks.bLocked, locks.cLocked, locks.alphaLocked, locks.betaLocked, locks.gammaLocked};
		if (ImGui::BeginTable("##newCellParameters", 3, ImGuiTableFlags_SizingStretchSame))
		{
			for (int index = 0; index < 6; ++index)
			{
				if (index % 3 == 0)
					ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(index % 3);
				ImGui::TextUnformatted(fields[index].first);
				ImGui::PushID(index);
				ImGui::SetNextItemWidth(-1.0f);
				ImGui::BeginDisabled(locked[index]);
				ImGui::DragFloat("##value", fields[index].second, index < 3 ? 0.01f : 0.1f, index < 3 ? 0.1f : 1.0f,
					index < 3 ? 100.0f : 179.0f, "%.4f");
				ImGui::EndDisabled();
				ImGui::SetItemTooltip(locked[index] ? "Wynika z układu krystalograficznego." : "Parametr komórki nowej struktury.");
				ImGui::PopID();
			}
			ImGui::EndTable();
		}
		const auto domain = m_Domain.lock();
		ImGui::BeginDisabled(!domain);
		if (ImGui::Button("Utwórz"))
		{
			OpenCrystalStructureAsWindow(EmptyCell(BuildLatticeCell(m_NewSystem, m_NewParameters)), "Nowa komórka",
				*domain, m_Layer, m_Elements, m_Styles);
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndDisabled();
		ImGui::SetItemTooltip(domain ? "Otwiera nowe okno z pustą komórką (bez atomów)." : "Brak warstwy domeny: otwórz projekt.");
		ImGui::EndPopup();
	}
} // namespace DefectStudio
