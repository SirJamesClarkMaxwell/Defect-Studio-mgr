#include "Core/dspch.hpp"
#include "Presentation/Panels/PhononPanelSession.hpp"

#include <algorithm>

namespace DefectStudio
{
	Path PhononPanelSession::CaseOutputDirectory(const std::string &kindDirName) const
	{
		const auto &s = Settings();
		if (kindDirName.starts_with("phonons") && !s.outputDirectory.Empty()) return s.outputDirectory;
		if (kindDirName == "ccd" && !s.ccdAnalysisOutputDirectory.Empty()) return s.ccdAnalysisOutputDirectory;
		if (kindDirName == "ccd_inputs" && !s.ccdOutputDirectory.Empty()) return s.ccdOutputDirectory;
		std::vector<Path> inputs;
		if (kindDirName == "ccd")
		{
			if (!s.ccdDirectory.Empty()) inputs.push_back(s.ccdDirectory);
			else if (!s.groundStateDirectory.Empty()) inputs = {s.groundStateDirectory, s.excitedStateDirectory, s.tripletDirectory,
				s.groundAtExcitedDirectory, s.excitedAtGroundDirectory};
			if (s.ccdPhononCurvature) inputs.push_back(s.resultDirectory);
		}
		else if (kindDirName == "ccd_inputs")
			inputs = {s.groundStateDirectory, s.excitedStateDirectory};
		else
		{
			inputs = {s.forceConstantDirectory, s.groundStateDirectory, s.excitedStateDirectory,
				s.tripletDirectory, s.groundAtExcitedDirectory, s.excitedAtGroundDirectory};
			if (!s.embeddingMult.empty()) inputs.push_back(s.bulkForceConstantDirectory);
		}
		const auto plain = defaultDatasetPath(inputs, kindDirName);
		if (plain.Empty()) return {};
		std::string name;
		for (const auto &item : m_Settings.cases)
			if (item.id == ActiveCaseId()) name = item.name;
		std::string key = ActiveCaseId() + "\n" + name + "\n" + plain.Utf8() + "\n" +
			s.groundStateDirectory.Utf8() + "\n" + s.excitedStateDirectory.Utf8();
		if (const auto cached = m_CaseOutputDirectories.find(key); cached != m_CaseOutputDirectories.end()) return cached->second;
		const auto collides = [&](const Path &directory) {
			const auto owner = DatasetCaseId(directory);
			if (!owner.empty() && owner != ActiveCaseId()) return true;
			if (const auto summary = ReadDatasetRef(directory))
			{
				const auto gs = summary->inputs.find("gs_dir"), es = summary->inputs.find("es_dir");
				return (gs != summary->inputs.end() && !SameFilePath(gs->second, s.groundStateDirectory, m_ProjectDirectory)) ||
					(es != summary->inputs.end() && !SameFilePath(es->second, s.excitedStateDirectory, m_ProjectDirectory));
			}
			return false;
		};
		Path output = plain;
		if (collides(output))
		{
			std::string slug;
			for (const unsigned char c : name)
			{
				if ((c & 0xc0) == 0x80) continue; // one replacement per UTF-8 character
				if (slug.size() == 40) break;
				slug += (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' ? c : '_';
			}
			if (slug.empty()) slug = "case";
			const auto stem = kindDirName + "_" + slug;
			output = plain.parent_path().Join(stem);
			for (int suffix = 2; collides(output); ++suffix)
				output = plain.parent_path().Join(stem + "_" + std::to_string(suffix));
		}
		return m_CaseOutputDirectories.emplace(std::move(key), output).first->second;
	}
}
