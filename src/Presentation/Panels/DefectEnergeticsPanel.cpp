#include "Core/dspch.hpp"

#include "Presentation/Panels/DefectEnergeticsPanel.hpp"

#include <algorithm>
#include <format>

#include <imgui.h>

#include "Core/JobSystem/JobContext.hpp"
#include "Core/JobSystem/JobSystem.hpp"
#include "Core/Platform/FileDialog.hpp"
#include "IO/TextFileIO.hpp"
#include "IO/VaspRunRecordIO.hpp"
#include "Presentation/Panels/FnvCorrectionInputs.hpp"
#include "Presentation/Panels/FnvCorrectionPanel.hpp"
#include "Presentation/Panels/PropertyGrid.hpp"
#include "ScientificRuntime/Python/FnvCorrectionBridge.hpp"

namespace DefectStudio
{
	// Walks the folder and reads every run's OUTCAR head and tail, reusing the cached record of a
	// run whose OUTCAR did not change. The walk alone takes minutes on the Okeanos mount.
	class VaspRunScanJob final : public IJob
	{
	public:
		VaspRunScanJob(Path root, std::vector<VaspRunRecord> cached)
			: m_Root(std::move(root)), m_Cached(std::move(cached)) {}

		[[nodiscard]] std::string GetName() const override { return "Skan obliczeń VASP"; }
		[[nodiscard]] std::string GetType() const override { return "energetics.scan"; }
		void Execute(JobContext &context) override
		{
			context.SetStage("Szukanie OUTCAR");
			const auto runs = VaspRunRecordIO::FindRuns(m_Root, [&] { return context.IsCancellationRequested(); });
			context.ThrowIfCancellationRequested();
			std::vector<VaspRunRecord> records;
			for (std::size_t i = 0; i < runs.size(); ++i)
			{
				context.ThrowIfCancellationRequested();
				context.SetProgress(static_cast<float>(i), static_cast<float>(runs.size()));
				context.SetMessage(runs[i].Utf8());
				const auto cached = std::find_if(m_Cached.begin(), m_Cached.end(),
					[&](const VaspRunRecord &record) { return record.directory == runs[i]; });
				records.push_back(cached != m_Cached.end() && VaspRunRecordIO::IsUpToDate(*cached)
					? *cached : VaspRunRecordIO::Read(runs[i]));
			}
			m_Result = std::move(records);
		}
		[[nodiscard]] std::optional<std::vector<VaspRunRecord>> &Result() noexcept { return m_Result; }

	private:
		Path m_Root;
		std::vector<VaspRunRecord> m_Cached;
		std::optional<std::vector<VaspRunRecord>> m_Result;
	};

	namespace
	{
		constexpr ImVec4 kWarningColor(0.95f, 0.75f, 0.30f, 1.0f);

		std::string Relative(const Path &path, const Path &root)
		{
			if (root.Empty())
				return path.Utf8();
			const auto relative = path.Native().lexically_relative(root.Native());
			return relative.empty() || relative.native().starts_with(L"..") ? path.Utf8()
				: Path(relative).Utf8();
		}
	} // namespace

	DefectEnergeticsPanel::DefectEnergeticsPanel(WeakRef<JobSystem> jobSystem, std::string title, bool visibleByDefault)
		: IPanel(std::move(title), visibleByDefault), m_JobSystem(std::move(jobSystem))
	{
	}

	Ref<IPanel> DefectEnergeticsPanel::Clone() const
	{
		auto clone = CreateRef<DefectEnergeticsPanel>(m_JobSystem, GetTitle(), IsVisible());
		clone->m_File = m_File;
		clone->m_SaveHandler = m_SaveHandler;
		clone->m_FnvPanel = m_FnvPanel;
		clone->m_Edges = m_Edges;
		clone->m_MuSet = m_MuSet;
		return clone;
	}

	void DefectEnergeticsPanel::SetPersistedState(EnergeticsFile file)
	{
		*m_File = std::move(file);
		m_Message.clear();
		rebuild();
		readBandEdges();
		rebuild();
	}

	const FnvCorrectionsFile &DefectEnergeticsPanel::fnvFile() const
	{
		static const FnvCorrectionsFile none;
		const auto panel = m_FnvPanel.lock(); // owned by EditorLayer for the whole session
		return panel ? panel->SavedCorrections() : none;
	}

	void DefectEnergeticsPanel::rebuild()
	{
		m_Model = BuildEnergeticsModel(*m_File, fnvFile(), m_Edges);
		m_MuSet = std::clamp(m_MuSet, 0, std::max(0, static_cast<int>(m_Model.muSets.size()) - 1));
	}

	void DefectEnergeticsPanel::changed()
	{
		rebuild();
		if (m_SaveHandler)
			m_SaveHandler(*m_File);
	}

	void DefectEnergeticsPanel::readBandEdges()
	{
		m_Edges.reset();
		if (m_Model.bulk)
			m_Edges = VaspRunRecordIO::ReadBandEdges(m_File->runs[*m_Model.bulk].directory);
	}

	EnergeticsChoice &DefectEnergeticsPanel::choiceFor(const EnergeticsRow &row)
	{
		auto &choices = m_File->choices;
		const auto found = std::find_if(choices.begin(), choices.end(),
			[&](const EnergeticsChoice &choice) { return choice.defect == row.defect && choice.charge == row.charge; });
		if (found != choices.end())
			return *found;
		EnergeticsChoice choice;
		choice.defect = row.defect;
		choice.charge = row.charge;
		return choices.emplace_back(std::move(choice));
	}

	std::string DefectEnergeticsPanel::runLabel(const std::size_t run) const
	{
		if (run >= m_File->runs.size())
			return "?";
		const VaspRunRecord &record = m_File->runs[run];
		std::string label = Relative(record.directory, m_File->root);
		if (!CalculationUsable(record))
			label += record.finished ? "  [niezbieżne]" : "  [przerwane]";
		return label;
	}

	void DefectEnergeticsPanel::startScan()
	{
		const auto jobSystem = m_JobSystem.lock();
		if (!jobSystem || m_File->root.Empty())
			return;
		m_ScanJob = CreateRef<VaspRunScanJob>(m_File->root, m_File->runs);
		m_ScanJobId = jobSystem->Submit(m_ScanJob, JobPriority::Normal);
		m_Message.clear();
	}

	void DefectEnergeticsPanel::pollScan()
	{
		if (!m_ScanJob)
			return;
		const auto jobSystem = m_JobSystem.lock();
		const auto snapshot = jobSystem ? jobSystem->GetJob(m_ScanJobId) : std::nullopt;
		if (snapshot && (snapshot->status == JobStatus::Queued || snapshot->status == JobStatus::Running))
			return;
		if (snapshot && snapshot->status == JobStatus::Completed && m_ScanJob->Result())
		{
			m_File->runs = std::move(*m_ScanJob->Result());
			std::size_t broken = 0;
			for (const auto &run : m_File->runs)
				broken += run.error.empty() ? 0 : 1;
			m_Message = std::format("Znaleziono {} obliczeń{}.", m_File->runs.size(),
				broken ? std::format(" ({} bez czytelnego OUTCAR)", broken) : "");
			rebuild();
			readBandEdges();
			changed();
		}
		else
			m_Message = snapshot && snapshot->status == JobStatus::Cancelled ? "Skan przerwany."
				: snapshot && !snapshot->errorMessage.empty() ? snapshot->errorMessage : "Skan nie powiódł się.";
		m_ScanJob.reset();
		m_ScanJobId = 0;
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
			const std::string shown = m_File->root.Empty() ? "brak" : m_File->root.Utf8();
			const float button = ImGui::GetFrameHeight();
			ImGui::SetNextItemWidth(std::max(1.0f, ImGui::GetContentRegionAvail().x - button - ImGui::GetStyle().ItemInnerSpacing.x));
			ImGui::InputText("##root", const_cast<char *>(shown.c_str()), shown.size() + 1, ImGuiInputTextFlags_ReadOnly);
			ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
			ImGui::BeginDisabled(m_ScanJob != nullptr);
			if (ImGui::Button("...##pickRoot", ImVec2(button, 0.0f)))
			{
				const auto picked = Platform::PickFolder(m_File->root);
				if (picked && picked->has_value() && **picked != m_File->root)
				{
					m_File->root = **picked;
					changed();
					startScan();
				}
			}
			ImGui::EndDisabled();
		}
		grid.Row("");
		if (m_ScanJob)
		{
			const auto jobSystem = m_JobSystem.lock();
			const auto snapshot = jobSystem ? jobSystem->GetJob(m_ScanJobId) : std::nullopt;
			const float fraction = snapshot && snapshot->totalWork > 0.0f ? snapshot->completedWork / snapshot->totalWork : 0.0f;
			ImGui::ProgressBar(fraction, ImVec2(-ImGui::CalcTextSize("Anuluj").x - 3.0f * ImGui::GetStyle().FramePadding.x, 0.0f),
				snapshot && snapshot->totalWork > 0.0f ? nullptr : "szukanie OUTCAR...");
			if (snapshot)
				ImGui::SetItemTooltip("%s", snapshot->currentMessage.c_str());
			ImGui::SameLine();
			if (ImGui::Button("Anuluj##scan") && jobSystem)
				(void)jobSystem->RequestCancel(m_ScanJobId);
		}
		else
		{
			ImGui::BeginDisabled(m_File->root.Empty());
			if (ImGui::Button(m_File->runs.empty() ? "Skanuj" : "Odśwież"))
				startScan();
			ImGui::EndDisabled();
			ImGui::SetItemTooltip("Szuka OUTCAR na nowo; nie zmienione obliczenia nie są czytane ponownie.");
			if (!m_Message.empty())
			{
				ImGui::SameLine();
				ImGui::TextDisabled("%s", m_Message.c_str());
			}
		}

		grid.Row("Bulk", "Obliczenie czystego materiału: E_bulk, VBM i przerwa. Propozycja: największa komórka "
			"bez obcych pierwiastków. Gdy kandydatów jest kilku, wybierz.");
		{
			const std::string preview = m_Model.bulk ? runLabel(*m_Model.bulk) + (m_File->bulkRun ? "" : "  (auto)")
				: m_Model.bulkCandidates.empty() ? "brak kandydata" : "wybierz";
			ImGui::SetNextItemWidth(-1.0f);
			if (ImGui::BeginCombo("##bulk", preview.c_str()))
			{
				for (const std::size_t candidate : m_Model.bulkCandidates)
				{
					const bool selected = m_Model.bulk == candidate;
					if (ImGui::Selectable(runLabel(candidate).c_str(), selected))
					{
						m_File->bulkRun = m_File->runs[candidate].directory;
						rebuild();
						readBandEdges();
						changed();
					}
					if (m_File->runs[candidate].totalEnergy)
						ImGui::SetItemTooltip("TOTEN %.8f eV", *m_File->runs[candidate].totalEnergy);
				}
				ImGui::EndCombo();
			}
		}
		grid.Row("VBM / CBM", "Z EIGENVAL bulku: najwyższy obsadzony i najniższy pusty stan.");
		if (m_Edges)
			ImGui::Text("%.6f / %.6f eV", m_Edges->vbm, m_Edges->cbm);
		else
			ImGui::TextColored(kWarningColor, "brak (EIGENVAL bulku)");
		grid.Row("Przerwa E_g", "Zakres E_F na wykresie i w eksporcie. Domyślnie CBM - VBM bulku.");
		{
			bool custom = m_File->gap.has_value();
			if (ImGui::Checkbox("własna##gap", &custom))
			{
				m_File->gap = custom ? std::optional<double>(m_Model.reference.gap) : std::nullopt;
				changed();
			}
			ImGui::SameLine();
			double gap = m_Model.reference.gap;
			ImGui::BeginDisabled(!custom);
			ImGui::SetNextItemWidth(100.0f);
			if (ImGui::InputDouble("##gapValue", &gap, 0.0, 0.0, "%.4f") && custom)
			{
				m_File->gap = gap;
				changed();
			}
			ImGui::EndDisabled();
			if (custom)
			{
				ImGui::SameLine();
				std::array<char, 128> source{};
				std::copy_n(m_File->gapSource.begin(), std::min(m_File->gapSource.size(), source.size() - 1), source.begin());
				ImGui::SetNextItemWidth(-1.0f);
				if (ImGui::InputTextWithHint("##gapSource", "źródło", source.data(), source.size()))
				{
					m_File->gapSource = source.data();
					changed();
				}
			}
		}
		grid.Row("ε", "Stała dielektryczna dla nowych poprawek FNV (policzone wcześniej mają własną).");
		ImGui::SetNextItemWidth(100.0f);
		if (ImGui::InputDouble("##epsilon", &m_File->dielectric, 0.0, 0.0, "%.3f"))
			changed();
		grid.Row("Źródło ε");
		{
			std::array<char, 128> source{};
			std::copy_n(m_File->dielectricSource.begin(), std::min(m_File->dielectricSource.size(), source.size() - 1), source.begin());
			ImGui::SetNextItemWidth(-1.0f);
			if (ImGui::InputText("##epsilonSource", source.data(), source.size()))
			{
				m_File->dielectricSource = source.data();
				changed();
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
				for (const auto &[name, text] : ExportEnergeticsTsv(m_Model, *m_File, fnvFile()))
				{
					if (!TextFileIO::Save(**folder / Path::FromUtf8(name), text, error))
						break;
					++written;
				}
				m_Message = error.empty() ? std::format("Zapisano {} plików TSV w {}.", written, (*folder)->Utf8())
					: "Eksport: " + error;
			}
		}
		ImGui::SetItemTooltip("states.tsv (środek, E_corr, C dla a1/a2/a3, E_f), transitions.tsv, mu_sets.tsv "
			"i formation_<zestaw>.tsv dla każdego zestawu μ - do Origin.");
	}

	void DefectEnergeticsPanel::Render()
	{
		pollScan();
		pollFnvBatch();
		if (!IsVisible())
			return;
		ImGui::SetNextWindowSize(ImVec2(900.0f, 820.0f), ImGuiCond_FirstUseEver);
		bool stillOpen = true;
		const bool draw = ImGui::Begin(GetTitle().c_str(), &stillOpen);
		if (!stillOpen)
			SetVisible(false);
		if (draw)
		{
			rebuild(); // cheap (arithmetic over a few dozen rows) and follows FNV edits made elsewhere
			drawSource();
			drawRows();
			drawChemicalPotentials();
			drawFormation();
			drawExport();
		}
		ImGui::End();
	}
} // namespace DefectStudio
