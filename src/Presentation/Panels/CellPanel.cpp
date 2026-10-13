#include "Core/dspch.hpp"

#include "Presentation/Panels/CellPanel.hpp"

#include <algorithm>
#include <cmath>

#include <imgui.h>
#include <imgui_internal.h>

#include "Core/Commands/CommandRegistry.hpp"
#include "Domain/Crystal/Supercell.hpp"
#include "Domain/DomainLayer.hpp"
#include "Renderer/Commands/RendererAtomEditCommands.hpp"
#include "Renderer/Commands/RendererCellEditCommand.hpp"
#include "Renderer/RendererLayer.hpp"

namespace DefectStudio
{
	CellPanel::CellPanel(RendererLayer &layer, WeakRef<CommandRegistry> commands, WeakRef<DomainLayer> domain,
		ElementPropertiesTable elements, AtomStyleTable styles, std::string title, bool visibleByDefault)
		: IPanel(std::move(title), visibleByDefault), m_Layer(layer),
		  m_Commands(std::move(commands)), m_Domain(std::move(domain)),
		  m_Elements(std::move(elements)), m_Styles(std::move(styles))
	{
	}

	Ref<IPanel> CellPanel::Clone() const
	{
		return CreateRef<CellPanel>(*this);
	}

	bool CellPanel::replace(Result<CrystalStructure> structure, std::string description, bool changesSiteIndices)
	{
		if (!structure)
		{
			m_Status = structure.Error().userMessage;
			return false;
		}
		const auto commands = m_Commands.lock();
		if (!commands)
		{
			m_Status = "Komendy edycji są niedostępne.";
			return false;
		}
		ReplaceStructurePayload payload{m_WindowId, std::move(*structure), std::move(description), changesSiteIndices};
		CommandContext context;
		context.Set<ReplaceStructurePayload>(kReplaceStructurePayloadKey, std::move(payload));
		const auto result = commands->Execute(CommandID{kReplaceStructureCommandId}, std::move(context));
		m_Status = result ? "" : result.Error().userMessage;
		if (result)
			m_Coincident.reset();
		return result.HasValue();
	}

	void CellPanel::drawEditor(const CrystalStructure &structure)
	{
		bool commit = false;
		bool editing = false;
		const std::array<const char *, 6> captions = {"a [Å]", "b [Å]", "c [Å]", "α [deg]", "β [deg]", "γ [deg]"};
		if (ImGui::BeginTable("cell_parameters", 3, ImGuiTableFlags_SizingStretchSame))
		{
			for (int row = 0; row < 2; ++row)
			{
				ImGui::TableNextRow();
				for (int column = 0; column < 3; ++column)
				{
					const int index = row * 3 + column;
					ImGui::TableSetColumnIndex(column);
					ImGui::Text("%s: %.4f", captions[index], m_Parameters[index]);
					ImGui::PushID(index);
					ImGui::SetNextItemWidth(-1.0f);
					const bool changed = ImGui::DragFloat("##parameter", &m_DragParameters[index], row == 0 ? 0.01f : 0.1f,
						row == 0 ? 0.001f : 0.01f, row == 0 ? 0.0f : 179.99f, "%.4f");
					const bool dragging = ImGui::IsItemActive() && !ImGui::TempInputIsActive(ImGui::GetItemID());
					if (changed || dragging)
						m_Parameters[index] = m_DragParameters[index];
					// Ctrl also starts text input: only the actual mouse drag may snap an angle.
					if (row == 1 && ImGui::GetIO().KeyCtrl && dragging)
					{
						const std::array<float, 3> angles = {60.0f, 90.0f, 120.0f};
						m_Parameters[index] = *std::min_element(angles.begin(), angles.end(), [&](float a, float b) {
							return std::abs(a - m_Parameters[index]) < std::abs(b - m_Parameters[index]);
						});
					}
					commit = ImGui::IsItemDeactivatedAfterEdit() || commit;
					editing = ImGui::IsItemActive() || editing;
					ImGui::SetItemTooltip("Zastosuj po zakończeniu edycji (jedno cofnięcie).\n"
						"Ctrl podczas przeciągania kąta: 60/90/120°. Wpisane wartości bez przyciągania.");
					ImGui::PopID();
				}
			}
			ImGui::EndTable();
		}
		m_Editing = editing;
		ImGui::Text("c/a: %.4f", m_Parameters[2] / m_Parameters[0]);
		ImGui::Text("Objętość [Å³]: %.4f", std::abs(glm::determinant(structure.cell.ToMatrix())));
		ImGui::TextUnformatted("Pozycje przy zmianie komórki:");
		if (ImGui::RadioButton("ułamkowe", m_Positions == CellEditPositions::KeepFractional))
			m_Positions = CellEditPositions::KeepFractional;
		ImGui::SetItemTooltip("Zachowaj współrzędne ułamkowe; atomy, wakancje i początek układu defektu podążają za komórką.");
		ImGui::SameLine();
		if (ImGui::RadioButton("kartezjańskie", m_Positions == CellEditPositions::KeepCartesian))
			m_Positions = CellEditPositions::KeepCartesian;
		ImGui::SetItemTooltip("Zachowaj pozycje w Å; współrzędne ułamkowe zmieniają się wraz z komórką.");
		if (commit)
		{
			const LatticeParameters parameters{m_Parameters[0], m_Parameters[1], m_Parameters[2],
				m_Parameters[3], m_Parameters[4], m_Parameters[5]};
			(void)replace(WithCell(structure, CellFromParameters(structure.cell, parameters), m_Positions), "Zmień komórkę");
		}

		ImGui::Separator();
		ImGui::TextUnformatted("Powtórz komórkę");
		const std::array<const char *, 3> repeatCaptions = {"n", "m", "k"};
		if (ImGui::BeginTable("cell_repeat", 3, ImGuiTableFlags_SizingStretchSame))
		{
			ImGui::TableNextRow();
			for (int axis = 0; axis < 3; ++axis)
			{
				ImGui::TableSetColumnIndex(axis);
				ImGui::TextUnformatted(repeatCaptions[axis]);
				ImGui::PushID(axis);
				ImGui::SetNextItemWidth(-1.0f);
				ImGui::InputInt("##repeat", &m_Repeat[axis]);
				m_Repeat[axis] = std::clamp(m_Repeat[axis], 1, 10);
				ImGui::SetItemTooltip("Liczba powtórzeń wzdłuż wektora %s (1..10).", axis == 0 ? "a" : axis == 1 ? "b" : "c");
				ImGui::PopID();
			}
			ImGui::EndTable();
		}
		if (ImGui::Button("Powtórz komórkę"))
			(void)replace(BuildSupercell(structure, SupercellMatrix::Diagonal(m_Repeat[0], m_Repeat[1], m_Repeat[2])),
				"Powtórz komórkę", true);
		ImGui::SetItemTooltip("W tym samym oknie; atomy × n*m*k. Jedno cofnięcie.\n"
			"Powtórzenie usuwa wakancje i układ defektu; cofnięcie je przywraca.");
		if (ImGui::Button("Zawiń atomy do komórki"))
		{
			std::vector<std::size_t> coincident;
			auto wrapped = WrapIntoCell(structure, coincident);
			if (replace(std::move(wrapped), "Zawiń atomy do komórki"))
				m_Coincident = coincident.size();
		}
		ImGui::SetItemTooltip("Przenieś atomy i wakancje do [0,1). Nakładające się atomy pozostają; jedno cofnięcie.");
		if (m_Coincident && *m_Coincident > 0)
			ImGui::Text("Nakładające się atomy: %zu", *m_Coincident);
	}

	void CellPanel::Render()
	{
		if (!IsVisible())
			return;
		bool open = true;
		if (ImGui::Begin(GetTitle().c_str(), &open))
		{
			drawNewCell();
			const auto domain = m_Domain.lock();
			const auto commands = m_Commands.lock();
			const auto &windowId = m_Layer.GetLastFocusedViewportWindowId();
			if (!domain || windowId.empty())
				ImGui::TextDisabled("Brak aktywnego okna ze strukturą.");
			else
			{
				const auto target = ResolveAtomEditTarget(m_Layer, *domain, windowId);
				if (!target)
					ImGui::TextDisabled("To okno nie zawiera edytowalnej struktury.");
				else if (!target->record->structure.isPeriodic)
					ImGui::TextDisabled("Struktura nieokresowa: edycja, powtarzanie i zawijanie komórki są niedostępne.");
				else
				{
					const auto &record = *target->record;
					const auto &structureId = target->windowState->structure.domainStructureId;
					const bool newTarget = m_WindowId != windowId || m_StructureId != structureId;
					if (newTarget || (!m_Editing && m_Revision != record.revision))
					{
						const auto parameters = ParametersOf(record.structure.cell);
						m_Parameters = {parameters.a, parameters.b, parameters.c,
							parameters.alphaDegrees, parameters.betaDegrees, parameters.gammaDegrees};
						m_DragParameters = m_Parameters;
						m_WindowId = windowId;
						m_StructureId = structureId;
						m_Revision = record.revision;
						m_Coincident.reset();
						if (newTarget)
						{
							m_Repeat = {1, 1, 1};
							m_Editing = false;
							m_Status.clear();
						}
					}
					const bool transforming = target->windowState->modalTransform.has_value();
					if (!commands)
						ImGui::TextDisabled("Komendy edycji są niedostępne.");
					else if (transforming)
						ImGui::TextDisabled("Zakończ transformację atomów przed edycją komórki.");
					ImGui::BeginDisabled(!commands || transforming);
					drawEditor(record.structure);
					drawSublattice(record.structure, record.revision);
					ImGui::EndDisabled();
					// Keep the wrap result visible across our own commit; external edits/undo reseed next frame.
					if (m_Coincident)
						m_Revision = record.revision;
					if (!m_Status.empty())
						ImGui::TextWrapped("%s", m_Status.c_str());
				}
			}
		}
		ImGui::End();
		SetVisible(open);
	}
} // namespace DefectStudio
