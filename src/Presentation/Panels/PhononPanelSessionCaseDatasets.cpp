#include "Core/dspch.hpp"
#include "Presentation/Panels/PhononPanelSession.hpp"

#include <algorithm>

namespace DefectStudio
{
	std::string PhononPanelSession::DatasetCaseId(const Path &directory) const
	{
		for (const auto &item : m_Settings.cases)
			for (const auto &path : item.datasets)
				if (SameFilePath(path, directory, m_ProjectDirectory)) return item.id;
		return {};
	}

	bool PhononPanelSession::HasUnattachedCaseDatasets() const
	{
		return std::any_of(m_Datasets.begin(), m_Datasets.end(), [&](const auto &dataset) {
			return dataset.kind != AnalysisDatasetKind::Dispersion && DatasetCaseId(dataset.directory).empty() &&
				dataset.inputs.contains("gs_dir") && dataset.inputs.contains("es_dir");
		});
	}

	std::vector<Path> PhononPanelSession::ProposedDatasets(const std::string &caseId) const
	{
		std::vector<Path> proposed;
		for (const auto &item : Cases())
			if (item.id == caseId)
				for (const auto &dataset : m_Datasets)
				{
					if (dataset.kind == AnalysisDatasetKind::Dispersion || !DatasetCaseId(dataset.directory).empty()) continue;
					const auto gs = dataset.inputs.find("gs_dir"), es = dataset.inputs.find("es_dir");
					if (gs != dataset.inputs.end() && es != dataset.inputs.end() &&
						SameFilePath(gs->second, item.settings.groundStateDirectory, m_ProjectDirectory) &&
						SameFilePath(es->second, item.settings.excitedStateDirectory, m_ProjectDirectory))
						proposed.push_back(dataset.directory);
				}
		return proposed;
	}

	std::vector<std::string> PhononPanelSession::CreateCasesFromDatasets()
	{
		std::vector<std::vector<AnalysisDatasetRef>> groups;
		for (const auto &dataset : m_Datasets)
		{
			if (dataset.kind == AnalysisDatasetKind::Dispersion || !DatasetCaseId(dataset.directory).empty()) continue;
			const auto gs = dataset.inputs.find("gs_dir"), es = dataset.inputs.find("es_dir");
			if (gs == dataset.inputs.end() || es == dataset.inputs.end() || gs->second.Empty() || es->second.Empty()) continue;
			const auto group = std::find_if(groups.begin(), groups.end(), [&](const auto &items) {
				return SameFilePath(items.front().inputs.at("gs_dir"), gs->second, m_ProjectDirectory) &&
					SameFilePath(items.front().inputs.at("es_dir"), es->second, m_ProjectDirectory);
			});
			if (group == groups.end()) groups.push_back({dataset});
			else group->push_back(dataset);
		}
		std::vector<std::string> ids;
		for (const auto &group : groups)
		{
			std::string name = group.front().label;
			for (const auto &dataset : group)
			{
				std::size_t shared = 0;
				while (shared < name.size() && shared < dataset.label.size() && name[shared] == dataset.label[shared]) ++shared;
				while (shared > 0 && shared < name.size() && (static_cast<unsigned char>(name[shared]) & 0xc0) == 0x80) --shared;
				name.resize(shared);
			}
			if (const auto embedding = name.find(" emb"); embedding != std::string::npos) name.resize(embedding);
			const auto first = name.find_first_not_of(" \t\r\n");
			if (first == std::string::npos) name.clear();
			else name = name.substr(first, name.find_last_not_of(" \t\r\n") - first + 1);
			const auto &gs = group.front().inputs.at("gs_dir");
			if (name.empty()) name = gs.parent_path().filename().Utf8() + "/" + gs.filename().Utf8();
			const auto id = AddCase(name);
			if (id.empty()) break;
			auto &settings = m_Settings;
			for (const auto &dataset : group)
			{
				const auto fill = [&](const std::string &key, Path &path) {
					if (const auto input = dataset.inputs.find(key); input != dataset.inputs.end() && path.Empty())
						path = input->second;
				};
				fill("fc_dir", settings.forceConstantDirectory);
				fill("gs_dir", settings.groundStateDirectory);
				fill("es_dir", settings.excitedStateDirectory);
				fill("triplet_dir", settings.tripletDirectory);
				fill("bulk_fc_dir", settings.bulkForceConstantDirectory);
				AttachDataset(id, dataset.directory);
			}
			const auto phonons = std::find_if(group.begin(), group.end(), [](const auto &dataset) { return dataset.kind == AnalysisDatasetKind::Phonons; });
			if (phonons != group.end())
			{
				std::string error;
				if (const auto summary = Spectra(phonons->directory, error))
				{
					settings.zplFormula = summary->get().zplFormulaText;
					if (summary->get().sigmaMeV > 0.0) settings.sigmaMeV = summary->get().sigmaMeV;
					if (summary->get().gammaMeV > 0.0) settings.gammaMeV = summary->get().gammaMeV;
				}
				settings.resultDirectory = phonons->directory;
				loadResult(phonons->directory);
			}
			const auto ccd = std::find_if(group.begin(), group.end(), [](const auto &dataset) { return dataset.kind == AnalysisDatasetKind::Ccd; });
			if (ccd != group.end())
			{
				settings.selectedCcdDirectory = ccd->directory;
				(void)loadCcdResult(ccd->directory);
			}
			ids.push_back(id);
			Changed();
		}
		return ids;
	}
}
