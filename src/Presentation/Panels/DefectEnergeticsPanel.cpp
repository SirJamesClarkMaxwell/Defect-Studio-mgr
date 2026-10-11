#include "Core/dspch.hpp"

#include "Presentation/Panels/DefectEnergeticsPanel.hpp"

#include <algorithm>
#include <format>

#include <imgui.h>

#include "Core/JobSystem/JobSystem.hpp"
#include "Core/Platform/FileDialog.hpp"
#include "IO/TextFileIO.hpp"
#include "IO/VaspRunRecordIO.hpp"
#include "Presentation/Panels/PropertyGrid.hpp"

namespace DefectStudio
{
	namespace
	{
		const ImVec4 kWarningColor(0.95f, 0.75f, 0.30f, 1.0f);
	}
	DefectEnergeticsPanel::DefectEnergeticsPanel(Ref<DefectEnergeticsSession> session, View view,
		std::string title, bool visibleByDefault)
		: IPanel(std::move(title), visibleByDefault), m_Session(std::move(session)), m_View(view) {}

	Ref<IPanel> DefectEnergeticsPanel::Clone() const
	{
		return CreateRef<DefectEnergeticsPanel>(*this);
	}

	void DefectEnergeticsPanel::pickReference(const std::string &species)
	{
		const auto picked = Platform::PickFolder(m_Session->m_File->root);
		if (!picked || !picked->has_value())
			return;
		// ponytail: read on the UI thread - one small OUTCAR head + tail, about a second over SSHFS.
		VaspRunRecord record = VaspRunRecordIO::Read(**picked);
		const SpeciesCounts composition = CalculationComposition(record);
		if (!record.error.empty() || composition.size() != 1 || !composition.contains(species))
		{
			m_Session->m_Message = record.error.empty() ? std::format("{}: to nie jest obliczenie samego {}.", (*picked)->Utf8(), species)
				: record.error;
			return;
		}
		const auto existing = std::find_if(m_Session->m_File->runs.begin(), m_Session->m_File->runs.end(),
			[&](const VaspRunRecord &run) { return run.directory == record.directory; });
		if (existing != m_Session->m_File->runs.end())
			*existing = std::move(record);
		else
			m_Session->m_File->runs.push_back(std::move(record));
		m_Session->m_File->references[species] = **picked;
		m_Session->Changed();
	}

	void DefectEnergeticsPanel::drawSource()
	{
		const float labelWidth = PropertyGridLabelWidth({"Folder obliczeń", "Bulk", "VBM / CBM", "Przerwa E_g", "Źródło ε"});
		PropertyGrid grid("##EnergeticsSource", labelWidth);
		if (!grid)
			return;
		grid.Row("Folder obliczeń", "Folder z obliczeniami defektów (np. O:/ albo O:/GeV). Skan czyta tylko "
			"początek i koniec każdego OUTCAR; wynik jest zapisany w projekcie.");
		{
			const std::string shown = m_Session->m_File->root.Empty() ? "brak" : m_Session->m_File->root.Utf8();
			const float button = ImGui::GetFrameHeight();
			ImGui::SetNextItemWidth(std::max(1.0f, ImGui::GetContentRegionAvail().x - button - ImGui::GetStyle().ItemInnerSpacing.x));
			ImGui::InputText("##root", const_cast<char *>(shown.c_str()), shown.size() + 1, ImGuiInputTextFlags_ReadOnly);
			ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
			ImGui::BeginDisabled(m_Session->m_ScanJob != nullptr);
			if (ImGui::Button("...##pickRoot", ImVec2(button, 0.0f)))
			{
				const auto picked = Platform::PickFolder(m_Session->m_File->root);
				if (picked && picked->has_value() && **picked != m_Session->m_File->root)
				{
					m_Session->cancelJobs();
					m_Session->m_File->root = **picked;
					m_Session->Changed();
					m_Session->startScan();
				}
			}
			ImGui::EndDisabled();
		}
		grid.Row("");
		if (m_Session->m_ScanJob)
		{
			const auto jobSystem = m_Session->m_JobSystem.lock();
			const auto snapshot = jobSystem ? jobSystem->GetJob(m_Session->m_ScanJobId) : std::nullopt;
			const float fraction = snapshot && snapshot->totalWork > 0.0f ? snapshot->completedWork / snapshot->totalWork : 0.0f;
			ImGui::ProgressBar(fraction, ImVec2(-ImGui::CalcTextSize("Anuluj").x - 3.0f * ImGui::GetStyle().FramePadding.x, 0.0f),
				snapshot && snapshot->totalWork > 0.0f ? nullptr : "szukanie OUTCAR...");
			if (snapshot)
				ImGui::SetItemTooltip("%s", snapshot->currentMessage.c_str());
			ImGui::SameLine();
			if (ImGui::Button("Anuluj##scan") && jobSystem)
				(void)jobSystem->RequestCancel(m_Session->m_ScanJobId);
		}
		else
		{
			ImGui::BeginDisabled(m_Session->m_File->root.Empty());
			if (ImGui::Button(m_Session->m_File->runs.empty() ? "Skanuj" : "Odśwież"))
				m_Session->startScan();
			ImGui::EndDisabled();
			ImGui::SetItemTooltip("Szuka OUTCAR na nowo; nie zmienione obliczenia nie są czytane ponownie.");
			if (!m_Session->m_Message.empty())
			{
				ImGui::SameLine();
				ImGui::TextDisabled("%s", m_Session->m_Message.c_str());
			}
		}

		grid.Row("Bulk", "Obliczenie czystego materiału: E_bulk, VBM i przerwa. Propozycja: największa komórka "
			"bez obcych pierwiastków. Gdy kandydatów jest kilku, wybierz.");
		{
			const std::string preview = m_Session->m_Model.bulk ? m_Session->runLabel(*m_Session->m_Model.bulk) + (m_Session->m_File->bulkRun ? "" : "  (auto)")
				: m_Session->m_Model.bulkCandidates.empty() ? "brak kandydata" : "wybierz";
			ImGui::SetNextItemWidth(-1.0f);
			if (ImGui::BeginCombo("##bulk", preview.c_str()))
			{
				for (const std::size_t candidate : m_Session->m_Model.bulkCandidates)
				{
					const bool selected = m_Session->m_Model.bulk == candidate;
					if (ImGui::Selectable(m_Session->runLabel(candidate).c_str(), selected))
					{
						m_Session->m_File->bulkRun = m_Session->m_File->runs[candidate].directory;
						m_Session->Changed();
					}
					if (m_Session->m_File->runs[candidate].totalEnergy)
						ImGui::SetItemTooltip("TOTEN %.8f eV", *m_Session->m_File->runs[candidate].totalEnergy);
				}
				ImGui::EndCombo();
			}
		}
		grid.Row("VBM / CBM", "Z EIGENVAL bulku: najwyższy obsadzony i najniższy pusty stan.");
		if (m_Session->m_Edges)
			ImGui::Text("%.6f / %.6f eV", m_Session->m_Edges->vbm, m_Session->m_Edges->cbm);
		else
			ImGui::TextColored(kWarningColor, "brak (EIGENVAL bulku)");
		grid.Row("Przerwa E_g", "Zakres E_F na wykresie i w eksporcie. Domyślnie CBM - VBM bulku.");
		{
			bool custom = m_Session->m_File->gap.has_value();
			if (ImGui::Checkbox("własna##gap", &custom))
			{
				m_Session->m_File->gap = custom ? std::optional<double>(m_Session->m_Model.reference.gap) : std::nullopt;
				m_Session->Changed();
			}
			ImGui::SameLine();
			double gap = m_Session->m_Model.reference.gap;
			ImGui::BeginDisabled(!custom);
			ImGui::SetNextItemWidth(100.0f);
			if (ImGui::InputDouble("##gapValue", &gap, 0.0, 0.0, "%.4f") && custom)
			{
				m_Session->m_File->gap = gap;
				m_Session->Changed();
			}
			ImGui::EndDisabled();
			if (custom)
			{
				ImGui::SameLine();
				std::array<char, 128> source{};
				std::copy_n(m_Session->m_File->gapSource.begin(), std::min(m_Session->m_File->gapSource.size(), source.size() - 1), source.begin());
				ImGui::SetNextItemWidth(-1.0f);
				if (ImGui::InputTextWithHint("##gapSource", "źródło", source.data(), source.size()))
				{
					m_Session->m_File->gapSource = source.data();
					m_Session->Changed();
				}
			}
		}
		grid.Row("ε", "Stała dielektryczna dla nowych poprawek FNV (policzone wcześniej mają własną).");
		ImGui::SetNextItemWidth(100.0f);
		if (ImGui::InputDouble("##epsilon", &m_Session->m_File->dielectric, 0.0, 0.0, "%.3f"))
			m_Session->Changed();
		grid.Row("Źródło ε");
		{
			std::array<char, 128> source{};
			std::copy_n(m_Session->m_File->dielectricSource.begin(), std::min(m_Session->m_File->dielectricSource.size(), source.size() - 1), source.begin());
			ImGui::SetNextItemWidth(-1.0f);
			if (ImGui::InputText("##epsilonSource", source.data(), source.size()))
			{
				m_Session->m_File->dielectricSource = source.data();
				m_Session->Changed();
			}
		}
	}

	void DefectEnergeticsPanel::drawExport()
	{
		if (ImGui::Button("Eksport TSV..."))
		{
			const auto folder = Platform::PickFolder();
			if (folder && folder->has_value())
			{
				std::size_t written = 0;
				std::string error;
				for (const auto &[name, text] : ExportEnergeticsTsv(m_Session->m_Model, *m_Session->m_File, m_Session->fnvFile()))
				{
					if (!TextFileIO::Save(**folder / Path::FromUtf8(name), text, error))
						break;
					++written;
				}
				m_Session->m_Message = error.empty() ? std::format("Zapisano {} plików TSV w {}.", written, (*folder)->Utf8())
					: "Eksport: " + error;
			}
		}
		ImGui::SetItemTooltip("states.tsv (środek, E_corr, C dla a1/a2/a3, E_f), transitions.tsv, mu_sets.tsv "
			"i formation_<zestaw>.tsv dla każdego zestawu μ - do Origin.");
	}

	void DefectEnergeticsPanel::Render()
	{
		if (IsVisible())
			renderWindow();
	}

	void DefectEnergeticsPanel::renderWindow()
	{
		ImGui::SetNextWindowSize(ImVec2(900.0f, 650.0f), ImGuiCond_FirstUseEver);
		bool stillOpen = true;
		const bool draw = ImGui::Begin(GetTitle().c_str(), &stillOpen);
		if (!stillOpen)
			SetVisible(false);
		if (draw)
		{
			switch (m_View)
			{
			case View::Conditions: drawChemicalPotentials(); break;
			case View::Calculations:
				drawExport();
				if (!m_Session->m_Message.empty())
					ImGui::TextWrapped("%s", m_Session->m_Message.c_str());
				drawRows();
				break;
			case View::Formation:
			case View::Transitions: drawFormation(); break;
			case View::Concentrations: drawConcentrations(); break;
			}
		}
		ImGui::End();
	}
} // namespace DefectStudio
