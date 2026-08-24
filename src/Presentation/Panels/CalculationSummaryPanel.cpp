#include "Core/dspch.hpp"

#include "Presentation/Panels/CalculationSummaryPanel.hpp"

#include <algorithm>
#include <cstdio>

#include <implot.h>
#include <nlohmann/json.hpp>

#include "Core/JobSystem/JobSystem.hpp"
#include "Core/Logging/Logger.hpp"
#include "IO/TextFileIO.hpp"
#include "ScientificRuntime/Python/VaspOutputJob.hpp"

namespace DefectStudio
{
	namespace
	{
		// Property/value two-column layout for every scalar summary field - see user feedback,
		// this replaced a flat list of ImGui::Text lines.
		void RenderRow(const char *label, const std::string &value)
		{
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextUnformatted(label);
			ImGui::TableSetColumnIndex(1);
			ImGui::TextUnformatted(value.c_str());
		}

		std::string FormatVec3(const std::array<double, 3> &v)
		{
			char buffer[96];
			std::snprintf(buffer, sizeof(buffer), "%.4g, %.4g, %.4g", v[0], v[1], v[2]);
			return buffer;
		}

		// "5169.2s" alone is unreadable for real VASP walltimes (routinely hours-to-days) - adds
		// the same duration broken into d/h/m/s alongside the raw seconds value.
		std::string FormatDurationSeconds(double totalSeconds)
		{
			if (totalSeconds < 0.0)
				totalSeconds = 0.0;
			const auto totalWholeSeconds = static_cast<long long>(totalSeconds);
			const long long days = totalWholeSeconds / 86400;
			const long long hours = (totalWholeSeconds % 86400) / 3600;
			const long long minutes = (totalWholeSeconds % 3600) / 60;
			const double seconds = totalSeconds - static_cast<double>(days * 86400 + hours * 3600 + minutes * 60);

			std::string breakdown;
			if (days > 0)
				breakdown += std::to_string(days) + "d ";
			if (days > 0 || hours > 0)
				breakdown += std::to_string(hours) + "h ";
			if (days > 0 || hours > 0 || minutes > 0)
				breakdown += std::to_string(minutes) + "m ";
			char secondsBuffer[32];
			std::snprintf(secondsBuffer, sizeof(secondsBuffer), "%.1fs", seconds);
			breakdown += secondsBuffer;

			char rawBuffer[32];
			std::snprintf(rawBuffer, sizeof(rawBuffer), "%.1fs", totalSeconds);
			return std::string(rawBuffer) + " (" + breakdown + ")";
		}

		[[nodiscard]] Path CachePath()
		{
			return Path::FromResolved(
				FileSystem::CurrentPath() / "install" / "users" / "default" / "config" /
				"calculation_summary_cache.json");
		}

		template <typename T>
		nlohmann::json OptJson(const std::optional<T> &value)
		{
			return value.has_value() ? nlohmann::json(*value) : nlohmann::json(nullptr);
		}

		// Same schema vasp_output_load.py prints (and VaspOutputBridge::ParseVaspOutputJson
		// parses) - so the cache round-trips through that exact same parser, no separate cache
		// format/parser to keep in sync.
		nlohmann::json SerializeVaspOutputData(const VaspOutputData &data)
		{
			nlohmann::json payload;
			payload["path"] = data.path.String();
			if (data.gap.has_value())
				payload["gap"] = {{"bandgap", data.gap->bandgap}, {"homo", data.gap->homo}, {"lumo", data.gap->lumo}};
			else
				payload["gap"] = nullptr;
			// This panel always dispatches with includeOrbitals=false - never anything to cache here.
			payload["orbitals"] = nullptr;
			payload["orbitals_error"] = nullptr;

			const VaspOutputSummaryData &summary = data.summary;
			nlohmann::json summaryJson;
			summaryJson["energy_trend"] = OptJson(summary.energyTrend);
			summaryJson["final_energy"] = OptJson(summary.finalEnergy);
			summaryJson["cpu_time"] = OptJson(summary.cpuTimeSeconds);
			summaryJson["user_time"] = OptJson(summary.userTimeSeconds);
			summaryJson["system_time"] = OptJson(summary.systemTimeSeconds);
			summaryJson["elapsed_time"] = OptJson(summary.elapsedTimeSeconds);
			if (summary.totalDrift.has_value())
				summaryJson["total_drift"] = std::vector<double>(summary.totalDrift->begin(), summary.totalDrift->end());
			else
				summaryJson["total_drift"] = nullptr;
			summaryJson["nelect"] = OptJson(summary.nelect);
			summaryJson["ispin"] = OptJson(summary.ispin);
			summaryJson["pressure"] = OptJson(summary.pressureKilobar);
			if (summary.stressTensorKilobar.has_value())
			{
				std::vector<std::vector<double>> rows;
				for (const std::array<double, 3> &row : *summary.stressTensorKilobar)
					rows.emplace_back(row.begin(), row.end());
				summaryJson["stress_tensor"] = rows;
			}
			else
			{
				summaryJson["stress_tensor"] = nullptr;
			}
			summaryJson["space_group_symbol"] = OptJson(summary.spaceGroupSymbol);
			summaryJson["space_group_number"] = OptJson(summary.spaceGroupNumber);
			summaryJson["point_group_symbol"] = OptJson(summary.pointGroupSymbol);
			summaryJson["point_group_schoenflies"] = OptJson(summary.pointGroupSchoenflies);
			payload["summary"] = summaryJson;

			return payload;
		}

		void SaveCache(const VaspOutputData &data)
		{
			std::string error;
			if (!TextFileIO::Save(CachePath(), SerializeVaspOutputData(data).dump(), error))
				DS_LOG_WARN("CalculationSummaryPanel: failed to persist cache: {}", error);
		}

		// Shows instantly at startup ("serialized cache data, not live data" - a user explicitly
		// asked this NOT auto-dispatch a fresh subprocess load; Reload still does that on demand).
		[[nodiscard]] std::optional<VaspOutputData> LoadCache()
		{
			std::string text;
			std::string error;
			if (!TextFileIO::Load(CachePath(), text, error))
				return std::nullopt;
			Result<VaspOutputData> parsed = ParseVaspOutputJson(text);
			if (!parsed)
				return std::nullopt;
			return *parsed;
		}
	} // namespace

	CalculationSummaryPanel::CalculationSummaryPanel(WeakRef<JobSystem> jobSystem, std::string title, bool visibleByDefault)
		: IPanel(std::move(title), visibleByDefault), m_JobSystem(std::move(jobSystem))
	{
		if (std::optional<VaspOutputData> cached = LoadCache(); cached.has_value())
		{
			m_Directory = cached->path;
			m_Data = std::move(cached);
		}
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
				SaveCache(*m_Data);
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

		if (ImGui::BeginTable("##SummaryFields", 2,
				ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp))
		{
			ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthStretch, 1.0f);
			ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 1.0f);

			char buffer[128];

			if (summary.finalEnergy.has_value())
			{
				std::snprintf(buffer, sizeof(buffer), "%.6f eV", *summary.finalEnergy);
				RenderRow("Final energy", buffer);
			}
			else
			{
				RenderRow("Final energy", "unavailable");
			}

			if (data.gap.has_value())
			{
				std::snprintf(buffer, sizeof(buffer), "%.3f eV", data.gap->homo);
				RenderRow("HOMO", buffer);
				std::snprintf(buffer, sizeof(buffer), "%.3f eV", data.gap->lumo);
				RenderRow("LUMO", buffer);
				std::snprintf(buffer, sizeof(buffer), "%.3f eV", data.gap->bandgap);
				RenderRow("Band gap", buffer);
			}
			else
			{
				RenderRow("Band gap", "unavailable (no vasprun.xml)");
			}

			if (summary.cpuTimeSeconds.has_value())
				RenderRow("CPU time", FormatDurationSeconds(*summary.cpuTimeSeconds));
			if (summary.userTimeSeconds.has_value())
				RenderRow("User time", FormatDurationSeconds(*summary.userTimeSeconds));
			if (summary.systemTimeSeconds.has_value())
				RenderRow("System time", FormatDurationSeconds(*summary.systemTimeSeconds));
			if (summary.elapsedTimeSeconds.has_value())
				RenderRow("Elapsed time", FormatDurationSeconds(*summary.elapsedTimeSeconds));
			if (!summary.cpuTimeSeconds.has_value() && !summary.elapsedTimeSeconds.has_value())
				RenderRow("Timing", "unavailable (no OUTCAR)");

			if (summary.totalDrift.has_value())
				RenderRow("Total drift (eV/A)", FormatVec3(*summary.totalDrift));
			else
				RenderRow("Total drift", "unavailable");

			if (summary.nelect.has_value())
				RenderRow("NELECT", std::to_string(*summary.nelect));
			if (summary.ispin.has_value())
				RenderRow("ISPIN", std::to_string(*summary.ispin));

			if (summary.pressureKilobar.has_value())
			{
				std::snprintf(buffer, sizeof(buffer), "%.3f kB", *summary.pressureKilobar);
				RenderRow("Pressure", buffer);
			}

			if (summary.spaceGroupSymbol.has_value())
			{
				std::snprintf(buffer, sizeof(buffer), "%s (#%s)", summary.spaceGroupSymbol->c_str(),
					summary.spaceGroupNumber.has_value() ? std::to_string(*summary.spaceGroupNumber).c_str() : "?");
				RenderRow("Space group", buffer);
			}
			else
			{
				RenderRow("Space group", "unavailable");
			}

			if (summary.pointGroupSchoenflies.has_value())
			{
				std::snprintf(buffer, sizeof(buffer), "%s (%s)", summary.pointGroupSchoenflies->c_str(),
					summary.pointGroupSymbol.has_value() ? summary.pointGroupSymbol->c_str() : "?");
				RenderRow("Point group", buffer);
			}
			else
			{
				RenderRow("Point group", "unavailable");
			}

			ImGui::EndTable();
		}

		if (summary.stressTensorKilobar.has_value())
		{
			ImGui::Spacing();
			ImGui::TextUnformatted("Stress tensor (kB)");
			const auto &stress = *summary.stressTensorKilobar;
			if (ImGui::BeginTable("##StressTensor", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchSame))
			{
				for (const auto &row : stress)
				{
					ImGui::TableNextRow();
					for (std::size_t col = 0; col < 3; ++col)
					{
						ImGui::TableSetColumnIndex(static_cast<int>(col));
						ImGui::Text("%.4f", row[col]);
					}
				}
				ImGui::EndTable();
			}
		}

		// Fills whatever vertical space is left in the window, rather than a fixed height - the
		// plot is the one element in this panel that benefits from more room, everything else
		// above is fixed-size text/tables. Clamped to a floor so a very short window doesn't
		// collapse it to nothing.
		ImGui::Spacing();
		ImGui::TextUnformatted("Convergence");
		if (summary.energyTrend.has_value() && summary.energyTrend->size() > 1)
		{
			const std::vector<double> &trend = *summary.energyTrend;
			std::vector<double> steps(trend.size());
			for (std::size_t i = 0; i < trend.size(); ++i)
				steps[i] = static_cast<double>(i + 1);

			const float plotHeight = std::max(150.0f, ImGui::GetContentRegionAvail().y);
			if (ImPlot::BeginPlot("##ConvergencePlot", ImVec2(-1.0f, plotHeight)))
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

		ImGui::SetWindowFontScale(1.2f);

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

		ImGui::SetWindowFontScale(1.0f);

		ImGui::End();
	}
} // namespace DefectStudio
