#include "Core/dspch.hpp"

#include "Presentation/Panels/ElectronicStructureSession.hpp"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <unordered_map>

#include "Core/JobSystem/JobSystem.hpp"
#include "Core/Logging/Logger.hpp"
#include "IO/TextFileIO.hpp"
#include "ScientificRuntime/Python/VaspOrbitalGridConversion.hpp"
#include "ScientificRuntime/Python/VaspOrbitalGridJob.hpp"
#include "ScientificRuntime/Python/VaspOutputConversion.hpp"
#include "ScientificRuntime/Python/VaspOutputJob.hpp"

namespace DefectStudio
{
	namespace
	{
		[[nodiscard]] Path PersistedDefaultsPath()
		{
			return Path::FromResolved(
				FileSystem::CurrentPath() / "install" / "users" / "default" / "config" /
				"electronic_structure_defaults.txt");
		}

		[[nodiscard]] std::string Trim(const std::string &value)
		{
			const std::size_t begin = value.find_first_not_of(" \t\r\n");
			if (begin == std::string::npos)
				return {};
			const std::size_t end = value.find_last_not_of(" \t\r\n");
			return value.substr(begin, end - begin + 1);
		}

		// Reads the same column layout puntukas's own VaspOutput.save_orbital_data_csv(irreps=True)
		// writes (nr,e(up),occ(up),loc(up),irrep(up),e(down),occ(down),loc(down),irrep(down)) - see
		// ExportOrbitalsCsv, which now writes that exact header so files round-trip either way, and
		// so a file saved directly from puntukas (Python side) also loads here. numpy's savetxt
		// right-pads fields for alignment and prefixes the header with "# " - naive comma-split +
		// Trim() handles both without needing a real CSV parser (irrep labels are short bare
		// symmetry tags, never containing a comma).
		// Rows outside [bandStart, bandEnd] are skipped so this can serve stale/wider exports too.
		// Returns nullopt if the file is missing or nothing in range parsed.
		[[nodiscard]] std::optional<std::vector<OrbitalRecord>> ReadOrbitalsCsv(
			const Path &path, int bandStart, int bandEnd)
		{
			std::ifstream file(path.Native());
			if (!file.is_open())
				return std::nullopt;

			std::string line;
			std::getline(file, line); // header
			std::vector<OrbitalRecord> records;
			while (std::getline(file, line))
			{
				std::vector<std::string> fields;
				std::size_t start = 0;
				while (true)
				{
					const std::size_t comma = line.find(',', start);
					fields.push_back(Trim(
						line.substr(start, comma == std::string::npos ? std::string::npos : comma - start)));
					if (comma == std::string::npos)
						break;
					start = comma + 1;
				}
				if (fields.size() != 9)
					continue;

				try
				{
					const int band = std::stoi(fields[0]);
					if (band < bandStart || band > bandEnd)
						continue;
					OrbitalRecord record;
					record.band = band;
					record.up.energy = std::stof(fields[1]);
					record.up.occupation = std::stof(fields[2]);
					record.up.localization = std::stof(fields[3]);
					if (!fields[4].empty())
						record.up.irrep = fields[4];
					record.down.energy = std::stof(fields[5]);
					record.down.occupation = std::stof(fields[6]);
					record.down.localization = std::stof(fields[7]);
					if (!fields[8].empty())
						record.down.irrep = fields[8];
					records.push_back(std::move(record));
				}
				catch (const std::exception &)
				{
					continue;
				}
			}
			if (records.empty())
				return std::nullopt;
			return records;
		}
	} // namespace

	ElectronicStructureSession::ElectronicStructureSession(RendererLayer &layer, WeakRef<JobSystem> jobSystem)
		: m_Layer(layer), m_JobSystem(std::move(jobSystem))
	{
		loadPersistedDefaults();
	}

	void ElectronicStructureSession::loadPersistedDefaults()
	{
		std::string text;
		std::string error;
		if (!TextFileIO::Load(PersistedDefaultsPath(), text, error))
			return;

		std::unordered_map<std::string, std::string> values;
		std::istringstream stream(text);
		std::string line;
		while (std::getline(stream, line))
		{
			const std::size_t eq = line.find('=');
			if (eq == std::string::npos)
				continue;
			values[line.substr(0, eq)] = line.substr(eq + 1);
		}

		const auto getInt = [&values](const char *key, int fallback)
		{
			const auto it = values.find(key);
			if (it == values.end())
				return fallback;
			try { return std::stoi(it->second); } catch (const std::exception &) { return fallback; }
		};
		const auto getFloat = [&values](const char *key, float fallback)
		{
			const auto it = values.find(key);
			if (it == values.end())
				return fallback;
			try { return std::stof(it->second); } catch (const std::exception &) { return fallback; }
		};
		const auto getBool = [&values](const char *key, bool fallback)
		{
			const auto it = values.find(key);
			return it == values.end() ? fallback : it->second == "1";
		};

		LastUsedSettings settings{};
		settings.bandStart = getInt("band_start", 1022);
		settings.bandEnd = getInt("band_end", 1027);
		settings.gapWindowMargin = getInt("gap_window_margin", 10);
		settings.localizationThreshold = getFloat("localization_threshold", 0.0f);
		settings.splitSpinChannels = getBool("split_spin_channels", true);
		settings.relativeToVbm = getBool("relative_to_vbm", false);
		settings.isoValue = getFloat("iso_value", 0.03f);
		m_LastUsedSettings = settings;
		m_PersistedSettingsCache = settings;

		const auto bulkIt = values.find("bulk_directory");
		if (bulkIt != values.end() && !bulkIt->second.empty())
			m_BulkDirectory = Path(bulkIt->second);
		m_PersistedBulkDirectoryCache = m_BulkDirectory;
	}

	void ElectronicStructureSession::savePersistedDefaultsIfChanged()
	{
		if (!m_LastUsedSettings.has_value())
			return;
		if (m_PersistedSettingsCache.has_value() && *m_PersistedSettingsCache == *m_LastUsedSettings &&
			m_PersistedBulkDirectoryCache == m_BulkDirectory)
			return;

		const LastUsedSettings &settings = *m_LastUsedSettings;
		std::ostringstream stream;
		stream << "band_start=" << settings.bandStart << '\n'
			   << "band_end=" << settings.bandEnd << '\n'
			   << "gap_window_margin=" << settings.gapWindowMargin << '\n'
			   << "localization_threshold=" << settings.localizationThreshold << '\n'
			   << "split_spin_channels=" << (settings.splitSpinChannels ? 1 : 0) << '\n'
			   << "relative_to_vbm=" << (settings.relativeToVbm ? 1 : 0) << '\n'
			   << "iso_value=" << settings.isoValue << '\n'
			   << "bulk_directory=" << m_BulkDirectory.String() << '\n';

		std::string error;
		if (!TextFileIO::Save(PersistedDefaultsPath(), stream.str(), error))
		{
			DS_LOG_WARN("ElectronicStructureSession: failed to persist defaults: {}", error);
			return;
		}
		m_PersistedSettingsCache = settings;
		m_PersistedBulkDirectoryCache = m_BulkDirectory;
	}

	RendererWindowState *ElectronicStructureSession::FindFocusedWindow()
	{
		const std::string &focusedWindowId = m_Layer.GetLastFocusedViewportWindowId();
		for (RendererWindowState &candidate : m_Layer.GetWindows())
		{
			if (candidate.windowId == focusedWindowId)
				return &candidate;
		}
		return nullptr;
	}

	ElectronicStructureSession::WindowState &ElectronicStructureSession::Update(RendererWindowState &windowState)
	{
		const bool isNewWindow = !m_WindowStates.contains(windowState.windowId);
		WindowState &state = m_WindowStates[windowState.windowId];
		if (state.calculationDirectory.Empty())
		{
			state.calculationDirectory = windowState.structure.sourcePath.parent_path();
			// Carry forward whichever window's settings were last touched, instead of resetting to
			// the hardcoded struct defaults - band range/localization/bulk dir/iso value are almost
			// always the same across windows in one session.
			if (isNewWindow && m_LastUsedSettings.has_value())
			{
				state.bandStart = m_LastUsedSettings->bandStart;
				state.bandEnd = m_LastUsedSettings->bandEnd;
				state.gapWindowMargin = m_LastUsedSettings->gapWindowMargin;
				state.localizationThreshold = m_LastUsedSettings->localizationThreshold;
				state.splitSpinChannels = m_LastUsedSettings->splitSpinChannels;
				state.relativeToVbm = m_LastUsedSettings->relativeToVbm;
				state.isoValue = m_LastUsedSettings->isoValue;
			}
		}

		pollOutputJob(state);
		pollBulkJob();
		pollGridJobs(state, windowState);

		// Deliberately NOT auto-dispatched here (unlike bulk below) - opening a defect should only
		// load geometry. Electronic structure (a Python/WAVECAR round-trip) is loaded explicitly,
		// either via the panel's Load button or LoadElectronicStructureForFocusedWindowCommand.
		// Bulk reference is shared session-wide - dispatched once total, not once per window.
		if (!m_BulkLoadAttempted)
		{
			m_BulkLoadAttempted = true;
			DispatchBulkLoad();
		}

		m_LastUsedSettings = LastUsedSettings{
			state.bandStart, state.bandEnd, state.gapWindowMargin, state.localizationThreshold,
			state.splitSpinChannels, state.relativeToVbm, state.isoValue};
		savePersistedDefaultsIfChanged();

		return state;
	}

	const ElectronicStructureSession::WindowState *ElectronicStructureSession::FindWindowState(
		const std::string &windowId) const
	{
		const auto it = m_WindowStates.find(windowId);
		return it != m_WindowStates.end() ? &it->second : nullptr;
	}

	std::uint64_t ElectronicStructureSession::PackGridKey(int spinChannel, int band)
	{
		return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(spinChannel)) << 32) |
			static_cast<std::uint32_t>(band);
	}

	float ElectronicStructureSession::ResolveIsoValue(const WindowState &state, std::uint64_t key)
	{
		const auto it = state.isoValueByKey.find(key);
		return it != state.isoValueByKey.end() ? it->second : state.isoValue;
	}

	void ElectronicStructureSession::DispatchOutputLoad(WindowState &state)
	{
		const Path csvPath = state.calculationDirectory / "orbitals_export.csv";
		std::optional<std::vector<OrbitalRecord>> csvOrbitals =
			ReadOrbitalsCsv(csvPath, state.bandStart, state.bandEnd);
		if (csvOrbitals.has_value())
		{
			if (!state.data.has_value())
				state.data = ElectronicStructureData{};
			state.data->orbitals = std::move(*csvOrbitals);
		}

		Ref<JobSystem> jobSystem = m_JobSystem.lock();
		if (jobSystem == nullptr)
		{
			state.lastError = "JobSystem unavailable";
			return;
		}
		state.pendingOutputJob = CreateRef<VaspOutputJob>(state.calculationDirectory, state.bandStart, state.bandEnd);
		state.pendingOutputJobId = jobSystem->Submit(state.pendingOutputJob, JobPriority::Normal);
		state.lastError.clear();
	}

	void ElectronicStructureSession::DispatchBulkLoad()
	{
		Ref<JobSystem> jobSystem = m_JobSystem.lock();
		if (jobSystem == nullptr)
		{
			m_BulkError = "JobSystem unavailable";
			return;
		}
		m_PendingBulkJob = CreateRef<VaspOutputJob>(m_BulkDirectory, 0, 1);
		m_PendingBulkJobId = jobSystem->Submit(m_PendingBulkJob, JobPriority::Normal);
		m_BulkError.clear();
	}

	void ElectronicStructureSession::pollOutputJob(WindowState &state)
	{
		if (state.pendingOutputJob == nullptr)
			return;

		Ref<JobSystem> jobSystem = m_JobSystem.lock();
		if (jobSystem == nullptr)
			return;

		const std::optional<JobSnapshot> snapshot = jobSystem->GetJob(state.pendingOutputJobId);
		if (!snapshot.has_value() || snapshot->status == JobStatus::Queued || snapshot->status == JobStatus::Running)
			return;

		if (snapshot->status == JobStatus::Completed)
		{
			const std::optional<VaspOutputData> &result = state.pendingOutputJob->GetResult();
			if (result.has_value())
			{
				state.data = ConvertVaspOutputDataToElectronicStructureData(*result);
				state.lastError.clear();
			}
			else
			{
				state.lastError = "Load completed with no result";
			}
		}
		else
		{
			state.lastError = snapshot->errorMessage.empty() ? "Load failed" : snapshot->errorMessage;
			DS_LOG_ERROR("ElectronicStructureSession: VaspOutputJob failed: {}", state.lastError);
		}
		state.pendingOutputJob.reset();
		state.pendingOutputJobId = 0;
	}

	void ElectronicStructureSession::pollBulkJob()
	{
		if (m_PendingBulkJob == nullptr)
			return;

		Ref<JobSystem> jobSystem = m_JobSystem.lock();
		if (jobSystem == nullptr)
			return;

		const std::optional<JobSnapshot> snapshot = jobSystem->GetJob(m_PendingBulkJobId);
		if (!snapshot.has_value() || snapshot->status == JobStatus::Queued || snapshot->status == JobStatus::Running)
			return;

		if (snapshot->status == JobStatus::Completed)
		{
			const std::optional<VaspOutputData> &result = m_PendingBulkJob->GetResult();
			if (result.has_value() && result->gap.has_value())
			{
				m_BulkGap = BandGapData{result->gap->bandgap, result->gap->homo, result->gap->lumo};
				m_BulkError.clear();
			}
			else
			{
				m_BulkError = "Bulk load completed but no band-gap data (missing vasprun.xml?)";
			}
		}
		else
		{
			m_BulkError = snapshot->errorMessage.empty() ? "Bulk load failed" : snapshot->errorMessage;
			DS_LOG_ERROR("ElectronicStructureSession: bulk reference VaspOutputJob failed: {}", m_BulkError);
		}
		m_PendingBulkJob.reset();
		m_PendingBulkJobId = 0;
	}

	void ElectronicStructureSession::dispatchGridJob(WindowState &state, std::uint64_t key, int spinChannel, int band)
	{
		if (state.pendingGridJobs.contains(key))
			return;

		Ref<JobSystem> jobSystem = m_JobSystem.lock();
		if (jobSystem == nullptr)
		{
			state.gridError = "JobSystem unavailable";
			return;
		}

		Ref<VaspOrbitalGridJob> job = CreateRef<VaspOrbitalGridJob>(state.calculationDirectory, spinChannel, 0, band);
		const JobId jobId = jobSystem->Submit(job, JobPriority::Normal);
		state.pendingGridJobs[key] = job;
		state.pendingGridJobIds[key] = jobId;
	}

	void ElectronicStructureSession::pollGridJobs(WindowState &state, RendererWindowState &windowState)
	{
		if (state.pendingGridJobs.empty())
			return;

		Ref<JobSystem> jobSystem = m_JobSystem.lock();
		if (jobSystem == nullptr)
			return;

		for (auto it = state.pendingGridJobs.begin(); it != state.pendingGridJobs.end();)
		{
			const std::uint64_t key = it->first;
			const Ref<VaspOrbitalGridJob> &job = it->second;
			const auto idIt = state.pendingGridJobIds.find(key);
			const JobId jobId = idIt != state.pendingGridJobIds.end() ? idIt->second : 0;
			const std::optional<JobSnapshot> snapshot = jobSystem->GetJob(jobId);
			if (!snapshot.has_value() || snapshot->status == JobStatus::Queued || snapshot->status == JobStatus::Running)
			{
				++it;
				continue;
			}

			int matchingSlot = -1;
			for (int slot = 0; slot < 2; ++slot)
			{
				if (state.activeGridKeys[slot].has_value() && *state.activeGridKeys[slot] == key)
					matchingSlot = slot;
			}

			if (snapshot->status == JobStatus::Completed)
			{
				const std::optional<VaspOrbitalGridData> &result = job->GetResult();
				if (result.has_value())
				{
					OrbitalGridData grid = ConvertVaspOrbitalGridDataToDomain(*result);
					state.gridCache[key] = grid;
					state.gridFetchErrors.erase(key);
					if (matchingSlot >= 0)
					{
						m_Layer.RegenerateOrbitalIsosurface(
							windowState.windowId, grid, ResolveIsoValue(state, key), matchingSlot);
						state.gridError.clear();
					}
				}
				else
				{
					state.gridFetchErrors[key] = "Wavefunction load completed with no result";
					if (matchingSlot >= 0)
						state.gridError = state.gridFetchErrors[key];
				}
			}
			else
			{
				state.gridFetchErrors[key] =
					snapshot->errorMessage.empty() ? "Wavefunction load failed" : snapshot->errorMessage;
				DS_LOG_ERROR("ElectronicStructureSession: VaspOrbitalGridJob failed: {}", state.gridFetchErrors[key]);
				if (matchingSlot >= 0)
					state.gridError = state.gridFetchErrors[key];
			}

			state.pendingGridJobIds.erase(key);
			it = state.pendingGridJobs.erase(it);
		}
	}

	void ElectronicStructureSession::EnsureChannelRendered(
		WindowState &state, RendererWindowState &windowState, int slot, int spinChannel)
	{
		if (state.selectedBand < 0)
			return;

		const std::uint64_t key = PackGridKey(spinChannel, state.selectedBand);
		state.activeGridKeys[slot] = key;
		// Sync the shared slider to whatever this specific orbital's own remembered iso value is
		// (or the shared default, if it's never been individually tuned) - so switching bands
		// restores that orbital's look instead of carrying over whatever the previous one had.
		state.isoValue = ResolveIsoValue(state, key);

		RendererWindowState::OrbitalOverlayChannel &channel =
			slot == 0 ? windowState.orbitalChannelUp : windowState.orbitalChannelDown;
		channel.enabled = true;

		const auto cached = state.gridCache.find(key);
		if (cached != state.gridCache.end())
		{
			m_Layer.RegenerateOrbitalIsosurface(windowState.windowId, cached->second, state.isoValue, slot);
			state.gridError.clear();
		}
		else
		{
			dispatchGridJob(state, key, spinChannel, state.selectedBand);
		}

		// Prefetch the opposite spin channel in the background so enabling the other slot later
		// (or switching bands) is instant, not another wait.
		const int otherChannel = spinChannel == 0 ? 1 : 0;
		const std::uint64_t otherKey = PackGridKey(otherChannel, state.selectedBand);
		if (!state.gridCache.contains(otherKey))
			dispatchGridJob(state, otherKey, otherChannel, state.selectedBand);
	}

	const OrbitalGridData *ElectronicStructureSession::TryGetOrDispatchGrid(
		WindowState &state, int spinChannel, int band)
	{
		const std::uint64_t key = PackGridKey(spinChannel, band);
		const auto cached = state.gridCache.find(key);
		if (cached != state.gridCache.end())
			return &cached->second;

		// Don't retry a key that already failed - see GridFetchError/gridFetchErrors.
		if (!state.gridFetchErrors.contains(key))
			dispatchGridJob(state, key, spinChannel, band);
		return nullptr;
	}

	const std::string *ElectronicStructureSession::GridFetchError(const WindowState &state, std::uint64_t key)
	{
		const auto it = state.gridFetchErrors.find(key);
		return it != state.gridFetchErrors.end() ? &it->second : nullptr;
	}

	void ElectronicStructureSession::ExportOrbitalsCsv(WindowState &state)
	{
		if (!state.data.has_value() || !state.data->orbitals.has_value())
		{
			state.csvExportMessage = "No orbital data loaded.";
			return;
		}

		const Path outputPath = state.calculationDirectory / "orbitals_export.csv";
		std::ofstream file(outputPath.Native());
		if (!file.is_open())
		{
			state.csvExportMessage = "Failed to open " + outputPath.String();
			return;
		}

		// Same column names/order/precision as puntukas's own
		// VaspOutput.save_orbital_data_csv(irreps=True) - "#"-prefixed header (numpy comment
		// convention, so numpy/pandas readers skip it same as a real puntukas export) instead of
		// our own made-up column names. Padding/alignment is cosmetic in numpy's version and
		// doesn't survive a naive writer anyway - every consumer (numpy, pandas, our own
		// ReadOrbitalsCsv) splits on comma and ignores whitespace, so skipping exact byte-for-byte
		// padding costs nothing.
		file << "# nr,e(up),occ(up),loc(up),irrep(up),e(down),occ(down),loc(down),irrep(down)\n";
		char row[256];
		for (const OrbitalRecord &record : *state.data->orbitals)
		{
			std::snprintf(
				row, sizeof(row), "%d,%.5f,%.2f,%.1f,%s,%.5f,%.2f,%.1f,%s",
				record.band,
				record.up.energy, record.up.occupation, record.up.localization, record.up.irrep.value_or("").c_str(),
				record.down.energy, record.down.occupation, record.down.localization,
				record.down.irrep.value_or("").c_str());
			file << row << '\n';
		}
		state.csvExportMessage = "Exported: " + outputPath.String();
		DS_LOG_INFO("ElectronicStructureSession: exported orbitals CSV to {}", outputPath.String());
	}

	void ElectronicStructureSession::PrefetchAllOrbitals(WindowState &state, const std::vector<OrbitalRecord> &orbitals)
	{
		for (const OrbitalRecord &record : orbitals)
		{
			for (int spinChannel = 0; spinChannel < 2; ++spinChannel)
			{
				const std::uint64_t key = PackGridKey(spinChannel, record.band);
				if (!state.gridCache.contains(key))
					dispatchGridJob(state, key, spinChannel, record.band);
			}
		}
	}
} // namespace DefectStudio
