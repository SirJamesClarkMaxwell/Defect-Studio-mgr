#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include "Core/Utils/Uuid.hpp"
#include "IO/CcdAnalysisIO.hpp"
#include "ScientificRuntime/ScientificRuntimeTestPaths.hpp"

namespace DefectStudio::Tests
{
	class CcdAnalysisIOTests : public testing::Test
	{
	protected:
		Path directory;
		void Write(const char *name, const std::string &text)
		{
			std::ofstream stream(directory.Join(name).Native(), std::ios::binary);
			stream << text;
			ASSERT_TRUE(stream.good());
		}
		void SetUp() override
		{
			directory = Path::FromResolved(std::filesystem::temp_directory_path() / ("ds_ccd_" + ToString(GenerateUuid())));
			std::filesystem::create_directories(directory.Native());
			Write("summary.json", R"({"schema":"ds.ccd_analysis.v1","label":"Dimer","delta_Q_amu05_A":1,
"zpl_eV":{"used":2},"zpl_formula":{"text":"2*E_es - E_T - E_gs"},
"harmonic":{"gs":{"hbar_omega_meV":100,"S":1},"es":{"hbar_omega_meV":120,"S":2}},
"franck_condon":{"absorption_eV":2.2,"emission_eV":1.8,"relaxation_gs_eV":0.2,"relaxation_es_eV":0.2,"source":"points"},
"numerov":{"fc_0n":[0.6,0.4]}})");
			Write("ccd_points.tsv", "branch\tt\tQ_amu05_A\tenergy_eV\tstatus\ngs\t0\t0\t0\tok\nes\t1\t1\t2\tok\ngs\t1.5\t1.5\t\tmissing OUTCAR\n");
			Write("ccd_curves.tsv", "Q_amu05_A\tV_gs_eV\tV_es_eV\n-1\t1\t6\n0\t0\t3\n1\t1\t2\n2\t4\t3\n");
			Write("levels.tsv", "branch\tn\tenergy_meV\ngs\t0\t50\ngs\t1\t150\nes\t0\t60\nes\t1\t180\n");
			// Deliberately reordered columns: the header, not column position, defines n and branch.
			Write("wavefunctions.tsv", "Q_amu05_A\tes_1\tgs_0\tes_0\tgs_1\n0\t4\t1\t3\t2\n1\t8\t5\t7\t6\n");
			Write("pl_1d.tsv", "photon_energy_eV\tintensity_numerov\tintensity_harmonic\tintensity_phonon_1d\n1\t0.1\t0.2\tnan\n2\t1\t1\tnan\n");
		}
		void TearDown() override
		{
			std::error_code ignored;
			std::filesystem::remove_all(directory.Native(), ignored);
		}
	};

	TEST_F(CcdAnalysisIOTests, LoadsMetadataPointsLevelsAndHeaderDrivenWavefunctions)
	{
		CcdAnalysisResult result;
		std::string error;
		ASSERT_TRUE(CcdAnalysisIO::Load(directory, result, error)) << error;
		EXPECT_TRUE(error.empty());
		EXPECT_EQ(result.label, "Dimer");
		EXPECT_DOUBLE_EQ(result.deltaQ, 1);
		EXPECT_DOUBLE_EQ(result.zplEv, 2);
		EXPECT_EQ(result.zplFormulaText, "2*E_es - E_T - E_gs");
		ASSERT_TRUE(result.harmonic.gs);
		ASSERT_TRUE(result.harmonic.es);
		EXPECT_DOUBLE_EQ(result.harmonic.es->hbarOmegaMeV, 120);
		EXPECT_EQ(result.franckCondon.absorptionEv, 2.2);
		EXPECT_EQ(result.franckCondon.source, "points");
		ASSERT_EQ(result.points.size(), 3u);
		EXPECT_FALSE(result.points.back().energyEv);
		EXPECT_EQ(result.points.back().status, "missing OUTCAR");
		EXPECT_EQ(result.curves.q, (std::vector<double>{-1, 0, 1, 2}));
		EXPECT_EQ(result.levels.gsMeV, (std::vector<double>{50, 150}));
		EXPECT_EQ(result.levels.esMeV, (std::vector<double>{60, 180}));
		ASSERT_EQ(result.wavefunctions.gs.size(), 2u);
		ASSERT_EQ(result.wavefunctions.es.size(), 2u);
		EXPECT_EQ(result.wavefunctions.gs[0], (std::vector<double>{1, 5}));
		EXPECT_EQ(result.wavefunctions.gs[1], (std::vector<double>{2, 6}));
		EXPECT_EQ(result.wavefunctions.es[0], (std::vector<double>{3, 7}));
		EXPECT_EQ(result.wavefunctions.es[1], (std::vector<double>{4, 8}));
		EXPECT_EQ(result.fc0n, (std::vector<double>{0.6, 0.4}));
		EXPECT_EQ(result.pl1d.numerov, (std::vector<double>{0.1, 1}));
		EXPECT_TRUE(result.pl1d.phonon1d.empty());
	}

	TEST_F(CcdAnalysisIOTests, MissingOptionalFilesAndLegacyMetadataRemainLoadable)
	{
		for (const char *name : {"ccd_points.tsv", "ccd_curves.tsv", "levels.tsv", "wavefunctions.tsv", "pl_1d.tsv"})
			std::filesystem::remove(directory.Join(name).Native());
		Write("summary.json", R"({"schema":"ds.ccd_analysis.v1","delta_Q_amu05_A":1,"zpl_eV":{"used":2},"harmonic":null,"zpl_formula":null})");
		CcdAnalysisResult result;
		std::string error;
		ASSERT_TRUE(CcdAnalysisIO::Load(directory, result, error)) << error;
		EXPECT_TRUE(result.wavefunctions.q.empty());
		EXPECT_TRUE(result.wavefunctions.gs.empty());
		EXPECT_TRUE(result.levels.esMeV.empty());
		EXPECT_TRUE(result.curves.q.empty());
		EXPECT_TRUE(result.pl1d.energyEv.empty());
	}

	TEST_F(CcdAnalysisIOTests, MissingWavefunctionsAloneKeepsLevelsAndSpectra)
	{
		std::filesystem::remove(directory.Join("wavefunctions.tsv").Native());
		CcdAnalysisResult result;
		std::string error;
		ASSERT_TRUE(CcdAnalysisIO::Load(directory, result, error)) << error;
		EXPECT_TRUE(result.wavefunctions.gs.empty());
		EXPECT_EQ(result.levels.gsMeV.size(), 2u);
		EXPECT_EQ(result.pl1d.numerov.size(), 2u);
	}

	TEST_F(CcdAnalysisIOTests, WrongSchemaOrMalformedSummaryNamesFileAndPreservesResult)
	{
		for (const char *text : {"{", R"({"schema":"ds.ccd_analysis.v2"})",
			R"({"schema":"ds.ccd_analysis.v1","delta_Q_amu05_A":null,"zpl_eV":{"used":2}})",
			R"({"schema":"ds.ccd_analysis.v1","delta_Q_amu05_A":1,"zpl_eV":{"used":2},"numerov":[]})"})
		{
			Write("summary.json", text);
			CcdAnalysisResult result;
			result.label = "keep";
			std::string error;
			EXPECT_FALSE(CcdAnalysisIO::Load(directory, result, error));
			EXPECT_NE(error.find("summary.json"), std::string::npos);
			EXPECT_EQ(result.label, "keep");
		}
	}

	TEST_F(CcdAnalysisIOTests, MalformedOptionalFileFailsInsteadOfSilentlyDisappearing)
	{
		for (const auto &[file, text] : std::vector<std::pair<std::string, std::string>>{
			{"ccd_curves.tsv", "Q_amu05_A\tV_gs_eV\tV_es_eV\n0\t1\tinf\n"},
			{"ccd_curves.tsv", "Q_amu05_A\tV_gs_eV\tV_es_eV\n1\t0\t2\n0\t0\t2\n"},
			{"levels.tsv", "branch\tn\tenergy_meV\ngs\t1\t50\n"},
			{"wavefunctions.tsv", "Q_amu05_A\tgs_1\n0\t1\n"},
			{"wavefunctions.tsv", "Q_amu05_A\tgs_0\tgs_0\n0\t1\t2\n"},
			{"wavefunctions.tsv", "Q_amu05_A\tgs_0\tgs_00\n0\t1\t2\n"},
			{"pl_1d.tsv", "photon_energy_eV\tintensity_numerov\tintensity_harmonic\tintensity_phonon_1d\n1\t1x\t1\tnan\n"},
			{"ccd_points.tsv", "branch\tt\tQ_amu05_A\tenergy_eV\tstatus\ngs\t0\t0\tnan\tok\n"}})
		{
			std::ifstream original(directory.Join(file).Native(), std::ios::binary);
			const std::string saved((std::istreambuf_iterator<char>(original)), std::istreambuf_iterator<char>());
			original.close();
			Write(file.c_str(), text);
			CcdAnalysisResult result;
			std::string error;
			EXPECT_FALSE(CcdAnalysisIO::Load(directory, result, error)) << file;
			EXPECT_NE(error.find(file), std::string::npos) << error;
			Write(file.c_str(), saved);
		}
	}

	TEST(CcdAnalysisReferenceTests, LoadsRealPointAndPhononEffectiveOutputsWhenPresent)
	{
		const auto root = FindRepoRoot();
		bool loaded = false;
		for (const char *relative : {
			"test-directory/sjcm_mgr/hBN/V2CBCN/dimer/defectstudio/ccd",
			"test-directory/sjcm_mgr/PL-spectra/dimer/CH0/R2SCAN/9x9x1/defectstudio/ccd"})
		{
			const auto dir = root / relative;
			if (!FileSystem::Exists((dir / "summary.json").Native())) continue;
			loaded = true;
			CcdAnalysisResult result;
			std::string error;
			ASSERT_TRUE(CcdAnalysisIO::Load(dir, result, error)) << error;
			EXPECT_FALSE(result.curves.q.empty());
			EXPECT_FALSE(result.wavefunctions.gs.empty());
			EXPECT_FALSE(result.levels.esMeV.empty());
			EXPECT_FALSE(result.pl1d.numerov.empty());
			ASSERT_TRUE(result.franckCondon.absorptionEv && result.franckCondon.emissionEv &&
				result.franckCondon.relaxationGsEv && result.franckCondon.relaxationEsEv);
			EXPECT_NEAR(*result.franckCondon.absorptionEv - *result.franckCondon.emissionEv,
				*result.franckCondon.relaxationGsEv + *result.franckCondon.relaxationEsEv, 1e-9);
		}
		if (!loaded) GTEST_SKIP() << "real CCD outputs not present";
	}
}
