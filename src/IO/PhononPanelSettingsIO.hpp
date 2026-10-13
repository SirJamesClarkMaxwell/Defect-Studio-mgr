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
	struct PhononPanelSettings
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
		// task/104g: CCD single points (scripts/python/examples/ccd_inputs.py); empty = <project>/ccd/<es dir name>
		Path ccdOutputDirectory;
		double ccdStep = 0.25;
		Path dispersionDirectory; // optional output override
		Path selectedDispersionDirectory;
		std::vector<Path> comparedDirectories;
		bool dispersionInPlane = true;
		int dispersionPoints = 101;

		// The loaded result directory (PhononAnalysisIO); empty = nothing loaded
		Path resultDirectory;

		int selectedMode = -1;            // index into the modes table, -1 = none
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
		std::string plZpl = "experimental";   // computed | spin_purified | experimental | formula
		std::string irrepFilter;              // empty = all irreps
	};

	// phonon_panel.yaml next to manifest.yaml. Schema (all keys optional on read):
	//   formatVersion: 1
	//   inputs: {fcDir, gsDir, esDir, tripletDir, zplFormula, gsAtEsDir, esAtGsDir, outputDir, sigma, gamma,
	//            bulkFcDir, mult, dcutoff, bcutoff, ccdDir, ccdStep,
	//            dispersionDir, dispersionInPlane, dispersionPoints} # dcutoff/bcutoff absent = default
	//   resultDir: ...
	//   datasets: {compared: [paths], dispersion: path}
	//   arrowsFollowPhase: true
	//   seriesStyles: {"<plot>/<series>": {color: [r,g,b,a] | null, weight, visible, filled, fillAlpha, lineStyle}}
	//   view: {selectedMode, participationThreshold, carriedFraction, arrowLength, showArrows,
	//          arrowLowColor: [r, g, b], arrowHighColor: [r, g, b], amplitude, period, plZpl, irrepFilter}
	// Paths inside projectDirectory are written relative to it with '/' separators, others absolute
	// (same rule as FnvCorrectionsIO). Legacy zplExp is read as numeric formula text.
	class PhononPanelSettingsIO
	{
	public:
		static constexpr int kFormatVersion = 1;

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
