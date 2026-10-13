#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

#include "Core/Utils/Path.hpp"
#include "Core/Utils/Uuid.hpp"
#include "IO/PhononAnalysisIO.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		void WriteText(const FilePath &path, const std::string &text)
		{
			std::ofstream out(path, std::ios::binary);
			out << text;
		}

		// NPY v1.0: magic, version, uint16 LE header length, a Python dict literal padded with spaces
		// and a final '\n' so the data starts on a 64-byte boundary, then raw little-endian float32.
		void WriteNpy(const FilePath &path, const std::string &shape, const std::vector<float> &data,
			const std::string &descr = "<f4", bool fortranOrder = false)
		{
			std::string header = "{'descr': '" + descr + "', 'fortran_order': " + (fortranOrder ? "True" : "False") +
				", 'shape': " + shape + ", }";
			const std::size_t unpadded = 10 + header.size() + 1;
			header.append((64 - unpadded % 64) % 64, ' ');
			header.push_back('\n');
			std::ofstream out(path, std::ios::binary);
			out.write("\x93NUMPY\x01\x00", 8);
			const auto length = static_cast<std::uint16_t>(header.size());
			const char lengthBytes[2] = {static_cast<char>(length & 0xff), static_cast<char>(length >> 8)};
			out.write(lengthBytes, 2);
			out.write(header.data(), static_cast<std::streamsize>(header.size()));
			out.write(reinterpret_cast<const char *>(data.data()), static_cast<std::streamsize>(data.size() * sizeof(float)));
		}

		// Two atoms, six modes - the same shape the script writes, with values chosen to be checkable.
		FilePath WriteFixture(const std::string &tag)
		{
			const FilePath dir = std::filesystem::temp_directory_path() / ("ds_phonon_" + tag + "_" + ToString(GenerateUuid()));
			std::filesystem::create_directories(dir);
			WriteText(dir / "summary.json", R"({
  "schema": "ds.phonon_vibronic.v1",
  "n_atoms": 2, "n_modes": 6,
  "translations": {"indices": [0, 1, 2], "freq_meV": [0.0, 0.0, 0.0]},
  "imaginary_modes": [{"index": 3, "freq_meV": -4.5}],
  "stable": false,
  "s_total": 1.25, "debye_waller": 0.2865047968601901,
  "zpl_eV": {"computed": 3.452, "computed_spin_purified": null, "experimental": 4.08},
  "max_displacement_A": 0.0632, "delta_Q_amu05_A": 0.334,
  "sigma_meV": 6.0, "gamma_meV": 2.0,
  "input_paths": {"fc_dir": "x", "gs_dir": "y", "es_dir": "z", "triplet_dir": null},
  "units": {},
  "approximation": "harmonic, equal ground/excited curvatures, displaced oscillators, T=0"
}
)");
			WriteText(dir / "structure.json", R"({"symbols": ["C", "B"],
 "positions_A": [[0.0, 0.0, 7.5], [1.25, 0.72, 7.5]],
 "cell_A": [[2.5, 0.0, 0.0], [-1.25, 2.165, 0.0], [0.0, 0.0, 15.0]],
 "masses_amu": [12.011, 10.81]}
)");
			WriteText(dir / "modes.tsv",
				"index\tfreq_meV\tirrep\tn_eff\tbeta\tS_k\tq_k_amu05_A\tis_translation\n"
				"0\t1e-06\tA1\t2.0\t1.0\t0.0\t0.0\t1\n"
				"1\t-2e-06\tE\t2.0\t1.0\t0.0\t0.0\t1\n"
				"2\t3e-06\tE\t2.0\t1.0\t0.0\t0.0\t1\n"
				"3\t-4.5\tA2\t1.5\t1.3333333333\t0.0\t0.0\t0\n"
				"4\t120.25\tE\t1.8\t1.1111111111\t0.25\t0.01\t0\n"
				"5\t200.1\tA1\t1.1\t1.8181818182\t1.0\t0.05\t0\n");
			std::vector<float> vectors(6 * 2 * 3, 0.0f);
			std::vector<float> participation(6 * 2, 0.5f);
			vectors[(5 * 2 + 0) * 3 + 2] = 0.6f;
			vectors[(5 * 2 + 1) * 3 + 0] = -0.8f;
			participation[5 * 2 + 0] = 0.36f;
			participation[5 * 2 + 1] = 0.64f;
			WriteNpy(dir / "eigenvectors.npy", "(6, 2, 3)", vectors);
			WriteNpy(dir / "participation.npy", "(6, 2)", participation);
			WriteText(dir / "spectral_function.tsv", "energy_meV\tS_E_per_meV\n0\t0\n0.5\t0.01\n1\t0.02\n");
			WriteText(dir / "pl.tsv",
				"photon_energy_eV\tintensity_computed\tintensity_spin_purified\tintensity_experimental\n"
				"3.0\t0.1\tnan\t0.0\n"
				"3.452\t1\tnan\t0.2\n"
				"4.08\t0\tnan\t1\n");
			return dir;
		}

		bool Load(const FilePath &dir, PhononAnalysisResult &result, std::string &error)
		{
			return PhononAnalysisIO::Load(Path::FromResolved(dir), result, error);
		}
	} // namespace

	TEST(PhononAnalysisIOTests, LoadsEveryFileOfAResultDirectory)
	{
		const FilePath dir = WriteFixture("full");
		PhononAnalysisResult result;
		std::string error;
		ASSERT_TRUE(Load(dir, result, error)) << error;

		ASSERT_EQ(result.AtomCount(), 2u);
		EXPECT_EQ(result.symbols[0], "C");
		EXPECT_NEAR(result.positionsAngstrom[1].x, 1.25, 1e-12);
		EXPECT_NEAR(result.massesAmu[0], 12.011, 1e-12);
		// columns are lattice vectors: b = (-1.25, 2.165, 0)
		EXPECT_NEAR(result.cellAngstrom[1][0], -1.25, 1e-12);
		EXPECT_NEAR(result.cellAngstrom[1][1], 2.165, 1e-12);
		EXPECT_NEAR(result.cellAngstrom[2][2], 15.0, 1e-12);

		ASSERT_EQ(result.ModeCount(), 6u);
		EXPECT_TRUE(result.modes[0].isTranslation);
		EXPECT_FALSE(result.modes[5].isTranslation);
		EXPECT_EQ(result.modes[5].index, 5);
		EXPECT_EQ(result.modes[5].irrep, "A1");
		EXPECT_NEAR(result.modes[5].frequencyMeV, 200.1, 1e-12);
		EXPECT_NEAR(result.modes[5].effectiveAtomCount, 1.1, 1e-12);
		EXPECT_NEAR(result.modes[5].beta, 1.8181818182, 1e-9);
		EXPECT_NEAR(result.modes[5].huangRhys, 1.0, 1e-12);
		EXPECT_NEAR(result.modes[5].qAmuHalfAngstrom, 0.05, 1e-12);
		EXPECT_NEAR(result.modes[3].frequencyMeV, -4.5, 1e-12);

		ASSERT_EQ(result.eigenvectors.size(), 36u);
		ASSERT_EQ(result.participation.size(), 12u);
		EXPECT_FLOAT_EQ(result.eigenvectors[(5 * 2 + 0) * 3 + 2], 0.6f);
		EXPECT_FLOAT_EQ(result.eigenvectors[(5 * 2 + 1) * 3 + 0], -0.8f);
		EXPECT_FLOAT_EQ(result.participation[5 * 2 + 1], 0.64f);

		EXPECT_NEAR(result.sTotal, 1.25, 1e-12);
		EXPECT_NEAR(result.debyeWaller, 0.2865047968601901, 1e-12);
		EXPECT_FALSE(result.stable);
		ASSERT_EQ(result.imaginaryModes.size(), 1u);
		EXPECT_EQ(result.imaginaryModes[0], 3);
		ASSERT_TRUE(result.zplEv.computed.has_value());
		EXPECT_NEAR(*result.zplEv.computed, 3.452, 1e-12);
		EXPECT_FALSE(result.zplEv.spinPurified.has_value());
		ASSERT_TRUE(result.zplEv.experimental.has_value());
		EXPECT_NEAR(*result.zplEv.experimental, 4.08, 1e-12);
		EXPECT_NEAR(result.sigmaMeV, 6.0, 1e-12);
		EXPECT_NEAR(result.gammaMeV, 2.0, 1e-12);
		EXPECT_NEAR(result.maxDisplacementAngstrom, 0.0632, 1e-12);
		EXPECT_NEAR(result.deltaQAmuHalfAngstrom, 0.334, 1e-12);
		EXPECT_EQ(result.approximation.rfind("harmonic", 0), 0u);

		ASSERT_EQ(result.spectralEnergyMeV.size(), 3u);
		EXPECT_NEAR(result.spectralDensityPerMeV[2], 0.02, 1e-12);
		ASSERT_EQ(result.plPhotonEnergyEv.size(), 3u);
		EXPECT_EQ(result.plComputed.size(), 3u);
		EXPECT_TRUE(result.plSpinPurified.empty()); // all nan
		ASSERT_EQ(result.plExperimental.size(), 3u);
		EXPECT_NEAR(result.plExperimental[2], 1.0, 1e-12);
		EXPECT_TRUE(result.plFormula.empty());
		EXPECT_FALSE(result.zplEv.formula.has_value());
		EXPECT_TRUE(result.zplFormulaText.empty());
		EXPECT_TRUE(result.label.empty());

		std::filesystem::remove_all(dir);
	}

	TEST(PhononAnalysisIOTests, LoadsFormulaMetadataAndFiveColumnPl)
	{
		const FilePath dir = WriteFixture("formula");
		nlohmann::json summary;
		{
			std::ifstream in(dir / "summary.json");
			in >> summary;
		}
		summary["zpl_eV"]["formula"] = 1.945;
		summary["zpl_formula"] = {{"text", "1.945"}, {"variables_eV", nlohmann::json::object()}, {"value_eV", 1.945}};
		summary["label"] = "Dimer";
		WriteText(dir / "summary.json", summary.dump());
		WriteText(dir / "pl.tsv",
			"photon_energy_eV\tintensity_computed\tintensity_spin_purified\tintensity_experimental\tintensity_formula\n"
			"1.8\t0.1\tnan\t0.0\t0.2\n"
			"1.945\t1\tnan\t0.2\t1\n");
		PhononAnalysisResult result;
		std::string error;
		ASSERT_TRUE(Load(dir, result, error)) << error;
		ASSERT_TRUE(result.zplEv.formula.has_value());
		EXPECT_DOUBLE_EQ(*result.zplEv.formula, 1.945);
		EXPECT_EQ(result.zplFormulaText, "1.945");
		EXPECT_EQ(result.label, "Dimer");
		ASSERT_EQ(result.plFormula.size(), 2u);
		EXPECT_DOUBLE_EQ(result.plFormula[0], 0.2);
		EXPECT_DOUBLE_EQ(result.plFormula[1], 1.0);

		summary["zpl_eV"]["formula"] = nullptr;
		summary["zpl_formula"] = nullptr;
		WriteText(dir / "summary.json", summary.dump());
		WriteText(dir / "pl.tsv",
			"photon_energy_eV\tintensity_computed\tintensity_spin_purified\tintensity_experimental\tintensity_formula\n"
			"1.945\t1\tnan\t0.2\tnan\n");
		ASSERT_TRUE(Load(dir, result, error)) << error;
		EXPECT_TRUE(result.plFormula.empty());
		EXPECT_FALSE(result.zplEv.formula.has_value());
		EXPECT_TRUE(result.zplFormulaText.empty());

		WriteText(dir / "pl.tsv",
			"photon_energy_eV\tintensity_computed\tintensity_spin_purified\tintensity_experimental\tintensity_formula\n"
			"1.8\t0.1\tnan\t0.0\tnan\n"
			"1.945\t1\tnan\t0.2\t1\n");
		EXPECT_FALSE(Load(dir, result, error));
		EXPECT_EQ(error.rfind("pl.tsv:", 0), 0u);
		std::filesystem::remove_all(dir);
	}

	TEST(PhononAnalysisIOTests, SpectraFilesAreOptional)
	{
		const FilePath dir = WriteFixture("nospectra");
		std::filesystem::remove(dir / "spectral_function.tsv");
		std::filesystem::remove(dir / "pl.tsv");
		PhononAnalysisResult result;
		std::string error;
		ASSERT_TRUE(Load(dir, result, error)) << error;
		EXPECT_TRUE(result.spectralEnergyMeV.empty());
		EXPECT_TRUE(result.plPhotonEnergyEv.empty());
		std::filesystem::remove_all(dir);
	}

	TEST(PhononAnalysisIOTests, RejectsAnotherSchema)
	{
		const FilePath dir = WriteFixture("schema");
		WriteText(dir / "summary.json", R"({"schema": "ds.phonon_vibronic.v2", "n_atoms": 2, "n_modes": 6})");
		PhononAnalysisResult result;
		result.sTotal = -7.0;
		std::string error;
		EXPECT_FALSE(Load(dir, result, error));
		EXPECT_FALSE(error.empty());
		EXPECT_EQ(result.sTotal, -7.0); // untouched on failure
		std::filesystem::remove_all(dir);
	}

	TEST(PhononAnalysisIOTests, RejectsAnEigenvectorArrayOfTheWrongShape)
	{
		const FilePath dir = WriteFixture("shape");
		WriteNpy(dir / "eigenvectors.npy", "(6, 3, 3)", std::vector<float>(54, 0.0f));
		PhononAnalysisResult result;
		std::string error;
		EXPECT_FALSE(Load(dir, result, error));
		EXPECT_FALSE(error.empty());
		std::filesystem::remove_all(dir);
	}

	TEST(PhononAnalysisIOTests, RejectsFloat64AndFortranOrderArrays)
	{
		for (const bool fortran : {false, true})
		{
			const FilePath dir = WriteFixture(fortran ? "fortran" : "f8");
			if (fortran)
				WriteNpy(dir / "participation.npy", "(6, 2)", std::vector<float>(12, 0.5f), "<f4", true);
			else
				WriteNpy(dir / "participation.npy", "(6, 2)", std::vector<float>(24, 0.5f), "<f8");
			PhononAnalysisResult result;
			std::string error;
			EXPECT_FALSE(Load(dir, result, error)) << (fortran ? "fortran" : "f8");
			EXPECT_FALSE(error.empty());
			std::filesystem::remove_all(dir);
		}
	}

	TEST(PhononAnalysisIOTests, RejectsAModesTableWithTheWrongRowCount)
	{
		const FilePath dir = WriteFixture("rows");
		WriteText(dir / "modes.tsv",
			"index\tfreq_meV\tirrep\tn_eff\tbeta\tS_k\tq_k_amu05_A\tis_translation\n"
			"0\t1e-06\tA1\t2.0\t1.0\t0.0\t0.0\t1\n");
		PhononAnalysisResult result;
		std::string error;
		EXPECT_FALSE(Load(dir, result, error));
		EXPECT_FALSE(error.empty());
		std::filesystem::remove_all(dir);
	}

	TEST(PhononAnalysisIOTests, MissingDirectoryIsAnError)
	{
		PhononAnalysisResult result;
		std::string error;
		EXPECT_FALSE(Load(std::filesystem::temp_directory_path() / "ds_phonon_does_not_exist_8c1f", result, error));
		EXPECT_FALSE(error.empty());
	}
} // namespace DefectStudio::Tests
