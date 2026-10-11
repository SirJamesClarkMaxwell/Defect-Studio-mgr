#include "Core/dspch.hpp"

#include "Presentation/Panels/CalculationSummaryPanel.hpp"

#include <cmath>
#include <cstdio>

#include <imgui.h>

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
				RenderRow("Band gap", "unavailable (no complete eigenvalues)");
			}

			if (summary.finished)
				RenderRow("Timing footer present", *summary.finished ? "yes (finished)" : "no (unfinished / truncated)");
			if (summary.reachedAccuracy)
				RenderRow("Reached required accuracy", *summary.reachedAccuracy ? "yes" : "no");
			if (summary.nsw)
				RenderRow("NSW", std::to_string(*summary.nsw));
			if (summary.ibrion)
				RenderRow("IBRION", std::to_string(*summary.ibrion));
			if (summary.ediffg)
				RenderRow("EDIFFG", std::to_string(*summary.ediffg));
			if (!summary.forceTrend.empty() && std::isfinite(summary.forceTrend.back()))
			{
				std::snprintf(buffer, sizeof(buffer), "%.6f eV/A", summary.forceTrend.back());
				RenderRow("Last max |F|", buffer);
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
	}
} // namespace DefectStudio
