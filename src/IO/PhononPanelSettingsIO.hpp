#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "Core/Utils/Path.hpp"
#include "IO/PlotSeriesStyle.hpp"

namespace DefectStudio
{
	// task/104c: everything the Phonons panel (task 104d) lets the user set, so reopening the
	// project shows the same result, mode, arrows and animation settings. Inputs of the last
	// computation are kept too, so "Policz ponownie" needs no re-picking.
	struct PhononCaseSettings
	{
		// Computation inputs (PhononAnalysisRequest); empty = not chosen yet
		Path forceConstantDirectory;
		Path groundStateDirectory;
		Path excitedStateDirectory;
		Path tripletDirectory;
		std::string zplFormula;
		Path groundAtExcitedDirectory;
		Path excitedAtGroundDirectory;
		Path outputDirectory; // optional computation override, separate from the active result
		double sigmaMeV = 6.0;
		double gammaMeV = 2.0;
		// task/104g: embedding; empty bulk dir or mult = no embedding
		Path bulkForceConstantDirectory;
		std::string embeddingMult;           // "24x24x1"
		std::optional<double> dcutoffAngstrom;
		std::optional<double> bcutoffAngstrom;
		// task/104g: CCD single points (scripts/python/examples/ccd_inputs.py); empty = collision-safe case output
		Path ccdOutputDirectory;
		double ccdStep = 0.25;
		Path ccdDirectory; // optional completed CCD path, distinct from generated-point output
		Path ccdAnalysisOutputDirectory; // empty = DefaultDatasetDirectory(..., "ccd", ...)
		double ccdTemperatureK = 0.0;
		bool ccdPhononCurvature = false;
		Path selectedCcdDirectory;
		// The loaded result directory (PhononAnalysisIO); empty = nothing loaded
		Path resultDirectory;
		int selectedMode = -1;            // index into the modes table, -1 = none
		std::string plZpl = "experimental";   // computed | spin_purified | experimental | formula
		std::string irrepFilter;              // empty = all irreps
		double minHuangRhys = 0.0;            // mode filter; separate from the per-atom arrow threshold
	};

	struct PhononCase
	{
		std::string id;
		std::string name;
		std::vector<Path> datasets;
		PhononCaseSettings settings;
	};

	struct PhononPanelSettings : PhononCaseSettings
	{
		int ccdLevels = 3;
		bool ccdWavefunctions = false;
		bool ccdProbability = false; // chi squared instead of chi
		double ccdWavefunctionScale = 0.05;
		bool ccdArrows = true;
		bool ccdComparePhononPl = false;
		Path dispersionDirectory; // optional output override
		Path selectedDispersionDirectory;
		std::vector<Path> comparedDirectories;
		bool dispersionInPlane = true;
		int dispersionPoints = 101;


		double participationThreshold = 0.01;
		double carriedFraction = 0.9;     // for the "N atoms carry X % of the mode" readout
		double arrowLengthAngstrom = 1.5; // the longest arrow of the mode
		bool showArrows = true;
		bool arrowsFollowPhase = true;
		std::map<std::string, PlotSeriesStyle> seriesStyles; // <plot>/<series>
		glm::vec3 arrowLowColor = glm::vec3(0.25f, 0.75f, 0.35f);
		glm::vec3 arrowHighColor = glm::vec3(0.85f, 0.2f, 0.2f);
		double animationAmplitudeAngstrom = 0.3;
		double animationPeriodSeconds = 1.5; // visual period, not the physical one
		std::vector<PhononCase> cases; // active settings are synchronized from the base on read/save
		std::string activeCaseId;
		int nextCaseNumber = 1; // high-water mark; removed ids are never reused
		std::string plAxis = "energy"; // energy | zpl_loss (E_ZPL - E, meV)
		std::string plNormalization = "none"; // none | max | area (full grid)
	};

	// phonon_panel.yaml next to manifest.yaml. Schema (all keys optional on read):
	//   formatVersion: 2
	//   activeCase: case-1
	//   nextCaseNumber: 2
	//   cases: [{id, name, datasets: [paths], inputs: {case input keys below}, resultDir, ccd,
	//            view: {selectedMode, plZpl, irrepFilter, minHuangRhys}}]
	//   Top-level inputs/view/datasets contain only global fields; v1 flat keys still read:
	//   inputs: {fcDir, gsDir, esDir, tripletDir, zplFormula, gsAtEsDir, esAtGsDir, outputDir, sigma, gamma,
	//            bulkFcDir, mult, dcutoff, bcutoff, ccdDir, ccdStep, ccdPathDir, ccdAnalysisOutputDir,
	//            ccdTemperature, ccdPhononCurvature,
	//            dispersionDir, dispersionInPlane, dispersionPoints} # dcutoff/bcutoff absent = default
	//   resultDir: ...
	//   datasets: {compared: [paths], dispersion: path, ccd: path}
	//   arrowsFollowPhase: true
	//   seriesStyles: {"<plot>/<series>": {color: [r,g,b,a] | null, weight, visible, filled, fillAlpha, lineStyle}}
	//   view: {selectedMode, participationThreshold, carriedFraction, arrowLength, showArrows,
	//          arrowLowColor: [r, g, b], arrowHighColor: [r, g, b], amplitude, period, plZpl, irrepFilter, minHuangRhys,
	//          ccdLevels, ccdWavefunctions, ccdProbability, ccdWavefunctionScale, ccdArrows, ccdComparePhononPl,
	//          plAxis: energy | zpl_loss, plNormalization: none | max | area}
	// Paths inside projectDirectory are written relative to it with '/' separators, others absolute
	// (same rule as FnvCorrectionsIO). Legacy zplExp is read as numeric formula text.
	class PhononPanelSettingsIO
	{
	public:
		static constexpr int kFormatVersion = 2;

		[[nodiscard]] static Path FilePath(const Path &projectDirectory); // projectDirectory / "phonon_panel.yaml"

		// False (outError set) when the text is not YAML, the root is not a map or formatVersion >
		// kFormatVersion. Empty text = defaults. A key with a bad value keeps its default and adds a
		// line to outWarnings. Relative paths are resolved against projectDirectory.
		[[nodiscard]] static bool Parse(const std::string &text, const Path &projectDirectory,
			PhononPanelSettings &outSettings, std::vector<std::string> &outWarnings, std::string &outError);

		[[nodiscard]] static std::string Serialize(const PhononPanelSettings &settings, const Path &projectDirectory);

		// Missing file = success with defaults.
		[[nodiscard]] static bool Load(const Path &projectDirectory, PhononPanelSettings &outSettings,
			std::vector<std::string> &outWarnings, std::string &outError);

		// Sibling temp file renamed over FilePath(); a failure leaves any existing file untouched.
		[[nodiscard]] static bool Save(const Path &projectDirectory, const PhononPanelSettings &settings, std::string &outError);
	};
} // namespace DefectStudio
