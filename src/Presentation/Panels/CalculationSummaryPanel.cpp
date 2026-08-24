#include "Core/dspch.hpp"

#include "Presentation/Panels/CalculationSummaryPanel.hpp"

#include <implot.h>

#include "Core/JobSystem/JobSystem.hpp"
#include "ScientificRuntime/Python/VaspOutputJob.hpp"

namespace DefectStudio
{
	namespace
	{
		void RenderVec3(const char *label, const std::array<double, 3> &v)
		{
			ImGui::Text("%s: %.4g, %.4g, %.4g", label, v[0], v[1], v[2]);
		}
	} // namespace

	CalculationSummaryPanel::CalculationSummaryPanel(WeakRef<JobSystem> jobSystem, std::string title, bool visibleByDefault)
		: IPanel(std::move(title), visibleByDefault), m_JobSystem(std::move(jobSystem))
	{
	}

	Ref<IPanel> CalculationSummaryPanel::Clone() const
	{
		return CreateRef<CalculationSummaryPanel>(*this);
	}

	void CalculationSummaryPanel::OpenDirectory(Path directory)
	{
		m_Directory = std::move(directory);
		m_Data.reset();
		m_Error.clear();
		dispatchLoad();
		SetVisible(true);
	}

	void CalculationSummaryPanel::dispatchLoad()
	{
		Ref<JobSystem> jobSystem = m_JobSystem.lock();
		if (jobSystem == nullptr)
		{
			m_Error = "JobSystem unavailable";
			return;
		}
		// includeOrbitals=false - this panel never shows orbital data, no reason to pay for the
		// WAVECAR read/per-band diagonalization ElectronicStructurePanel's dispatch needs.
		m_PendingJob = CreateRef<VaspOutputJob>(m_Directory, 0, 0, false);
		m_PendingJobId = jobSystem->Submit(m_PendingJob, JobPriority::Normal);
		m_Error.clear();
	}

	void CalculationSummaryPanel::pollJob()
	{
		if (m_PendingJob == nullptr)
			return;

		Ref<JobSystem> jobSystem = m_JobSystem.lock();
		if (jobSystem == nullptr)
			return;

		const std::optional<JobSnapshot> snapshot = jobSystem->GetJob(m_PendingJobId);
		if (!snapshot.has_value() || snapshot->status == JobStatus::Queued || snapshot->status == JobStatus::Running)
			return;

		if (snapshot->status == JobStatus::Completed)
		{
			const std::optional<VaspOutputData> &result = m_PendingJob->GetResult();
			if (result.has_value())
			{
				m_Data = *result;
				m_Error.clear();
			}
			else
			{
				m_Error = "Load completed with no result";
			}
		}
		else
		{
			m_Error = snapshot->errorMessage.empty() ? "Load failed" : snapshot->errorMessage;
		}
		m_PendingJob.reset();
		m_PendingJobId = 0;
	}

	void CalculationSummaryPanel::renderSummary(const VaspOutputData &data)
	{
		const VaspOutputSummaryData &summary = data.summary;

		if (summary.finalEnergy.has_value())
			ImGui::Text("Final energy: %.6f eV", *summary.finalEnergy);
		else
			ImGui::TextDisabled("Final energy unavailable");

		if (data.gap.has_value())
		{
			ImGui::Text(
				"HOMO/LUMO %.3f / %.3f eV   Gap %.3f eV", data.gap->homo, data.gap->lumo, data.gap->bandgap);
		}
		else
		{
			ImGui::TextDisabled("Band gap unavailable (no vasprun.xml)");
		}

		ImGui::Separator();
		ImGui::TextUnformatted("Convergence");
		if (summary.energyTrend.has_value() && summary.energyTrend->size() > 1)
		{
			const std::vector<double> &trend = *summary.energyTrend;
			std::vector<double> steps(trend.size());
			for (std::size_t i = 0; i < trend.size(); ++i)
				steps[i] = static_cast<double>(i + 1);

			if (ImPlot::BeginPlot("##ConvergencePlot", ImVec2(-1.0f, 220.0f)))
			{
				ImPlot::SetupAxes("Ionic step", "Energy (eV)");
				ImPlot::PlotLine("Energy", steps.data(), trend.data(), static_cast<int>(trend.size()));
				ImPlot::EndPlot();
			}
		}
		else
		{
			ImGui::TextDisabled("Convergence trend unavailable (needs more than one ionic step)");
		}

		ImGui::Separator();
		ImGui::TextUnformatted("Timing");
		if (summary.cpuTimeSeconds.has_value())
			ImGui::Text("CPU %.1fs   User %.1fs   System %.1fs   Elapsed %.1fs",
				*summary.cpuTimeSeconds, summary.userTimeSeconds.value_or(0.0), summary.systemTimeSeconds.value_or(0.0),
				summary.elapsedTimeSeconds.value_or(0.0));
		else
			ImGui::TextDisabled("Timing unavailable (no OUTCAR)");

		ImGui::Separator();
		ImGui::TextUnformatted("Sanity checks");
		if (summary.totalDrift.has_value())
			RenderVec3("Total drift (eV/A)", *summary.totalDrift);
		else
			ImGui::TextDisabled("Total drift unavailable");

		if (summary.nelect.has_value() || summary.ispin.has_value())
		{
			ImGui::Text("NELECT %s   ISPIN %s",
				summary.nelect.has_value() ? std::to_string(*summary.nelect).c_str() : "-",
				summary.ispin.has_value() ? std::to_string(*summary.ispin).c_str() : "-");
		}

		if (summary.pressureKilobar.has_value())
			ImGui::Text("Pressure: %.3f kB", *summary.pressureKilobar);
		if (summary.stressTensorKilobar.has_value())
		{
			const auto &stress = *summary.stressTensorKilobar;
			ImGui::Text("Stress (kB): [%.2f %.2f %.2f / %.2f %.2f %.2f / %.2f %.2f %.2f]", stress[0][0], stress[0][1],
				stress[0][2], stress[1][0], stress[1][1], stress[1][2], stress[2][0], stress[2][1], stress[2][2]);
		}

		if (summary.spaceGroupSymbol.has_value())
		{
			ImGui::Text("Space group: %s (#%s)", summary.spaceGroupSymbol->c_str(),
				summary.spaceGroupNumber.has_value() ? std::to_string(*summary.spaceGroupNumber).c_str() : "?");
		}
		else
		{
			ImGui::TextDisabled("Space group unavailable");
		}
	}

	void CalculationSummaryPanel::Render()
	{
		if (!IsVisible())
			return;

		ImGui::SetNextWindowSize(ImVec2(480.0f, 620.0f), ImGuiCond_FirstUseEver);
		bool stillOpen = true;
		if (!ImGui::Begin(GetTitle().c_str(), &stillOpen))
		{
			ImGui::End();
			return;
		}
		if (!stillOpen)
		{
			SetVisible(false);
			ImGui::End();
			return;
		}

		pollJob();

		if (m_Directory.Empty())
		{
			ImGui::TextDisabled(
				"No directory selected - right-click a folder in the Project Tree and choose "
				"\"Show Calculation Summary\".");
			ImGui::End();
			return;
		}

		ImGui::TextDisabled("%s", m_Directory.String().c_str());

		const bool loading = m_PendingJob != nullptr;
		ImGui::BeginDisabled(loading);
		if (ImGui::Button(m_Data.has_value() ? "Reload" : "Load"))
			dispatchLoad();
		ImGui::EndDisabled();
		if (loading)
		{
			ImGui::SameLine();
			ImGui::TextDisabled("Loading...");
		}
		if (!m_Error.empty())
			ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", m_Error.c_str());

		if (m_Data.has_value())
			renderSummary(*m_Data);

		ImGui::End();
	}
} // namespace DefectStudio
