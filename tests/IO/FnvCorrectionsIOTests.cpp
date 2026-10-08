#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "Core/Utils/Path.hpp"
#include "Core/Utils/Time.hpp"
#include "IO/FnvCorrectionsIO.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		const Path kProject = Path::FromUtf8("D:/projekty/diament");

		FnvProfiles Profiles()
		{
			FnvProfiles profiles;
			profiles.charge = -1;
			profiles.dielectric = 5.7;
			profiles.centreFractional = glm::dvec3(0.46872, 0.53126, 0.46865);
			profiles.energyCutoff = 600.123456789;
			profiles.latticeEnergy = 0.252685123456789;
			profiles.modelPotentialMean = -0.000781234567891;
			for (int a = 0; a < 3; ++a)
			{
				auto &axis = profiles.axes[a];
				axis.length = 14.183163 + a;
				for (int k = 0; k < 6; ++k)
				{
					axis.position.push_back(k * axis.length / 6.0);
					axis.deltaV.push_back(0.1 * k - 0.0123456789012345 * a);
					axis.modelPotential.push_back(-0.01 * k + 1.0 / 3.0);
				}
			}
			return profiles;
		}

		PersistedFnvCorrection Entry(const char *defect)
		{
			PersistedFnvCorrection entry;
			entry.defectLocpot = kProject / Path::FromUtf8(defect);
			entry.bulkLocpot = Path::FromUtf8("E:/obliczenia/bulk/LOCPOT");
			entry.charge = -1;
			entry.dielectric = 5.7;
			entry.dielectricSource = "promotor (diament)";
			entry.centreFractional = glm::dvec3(0.46872, 0.53126, 0.46865);
			entry.energyCutoff = 0.0;
			entry.convention = FnvModelConvention::Pymatgen;
			entry.useAxis = {true, false, true};
			return entry;
		}

		FnvCorrectionsFile RoundTrip(const FnvCorrectionsFile &file, std::vector<std::string> &warnings)
		{
			FnvCorrectionsFile parsed;
			std::string error;
			EXPECT_TRUE(FnvCorrectionsIO::Parse(FnvCorrectionsIO::Serialize(file, kProject), kProject, parsed, warnings, error))
				<< error;
			return parsed;
		}
	} // namespace

	TEST(FnvCorrectionsIOTests, InputsProfilesAndWindowsRoundTrip)
	{
		FnvCorrectionsFile file;
		PersistedFnvCorrection entry = Entry("data/GeV/q-1/LOCPOT");
		entry.profiles = Profiles();
		entry.windows = {FnvPlateauWindow{13.1, 2.5}, FnvPlateauWindow{0.25, 3.0}, FnvPlateauWindow{7.0, 4.75}};
		file.corrections.push_back(entry);
		file.corrections.push_back(Entry("data/GeV/q+1/LOCPOT"));
		file.active = 1;

		std::vector<std::string> warnings;
		const FnvCorrectionsFile parsed = RoundTrip(file, warnings);
		EXPECT_TRUE(warnings.empty());
		ASSERT_EQ(parsed.corrections.size(), 2u);
		EXPECT_EQ(parsed.active, 1);

		const PersistedFnvCorrection &read = parsed.corrections[0];
		EXPECT_EQ(read.defectLocpot, entry.defectLocpot);
		EXPECT_EQ(read.bulkLocpot, entry.bulkLocpot);
		EXPECT_EQ(read.charge, -1);
		EXPECT_DOUBLE_EQ(read.dielectric, 5.7);
		EXPECT_EQ(read.dielectricSource, "promotor (diament)");
		EXPECT_DOUBLE_EQ(read.centreFractional.y, 0.53126);
		EXPECT_DOUBLE_EQ(read.energyCutoff, 0.0);
		EXPECT_EQ(read.convention, FnvModelConvention::Pymatgen);
		EXPECT_EQ(read.useAxis, (std::array<bool, 3>{true, false, true}));
		ASSERT_TRUE(read.profiles.has_value());
		const FnvProfiles expected = Profiles();
		EXPECT_EQ(read.profiles->charge, -1);
		EXPECT_DOUBLE_EQ(read.profiles->energyCutoff, expected.energyCutoff);
		EXPECT_DOUBLE_EQ(read.profiles->latticeEnergy, expected.latticeEnergy);
		EXPECT_DOUBLE_EQ(read.profiles->modelPotentialMean, expected.modelPotentialMean);
		EXPECT_DOUBLE_EQ(read.profiles->centreFractional.z, expected.centreFractional.z);
		for (int a = 0; a < 3; ++a)
		{
			SCOPED_TRACE(a);
			const auto &axis = read.profiles->axes[a];
			EXPECT_DOUBLE_EQ(axis.length, expected.axes[a].length);
			ASSERT_EQ(axis.position.size(), 6u);
			ASSERT_EQ(axis.deltaV.size(), 6u);
			ASSERT_EQ(axis.modelPotential.size(), 6u);
			for (int k = 0; k < 6; ++k)
			{
				EXPECT_NEAR(axis.position[k], expected.axes[a].position[k], 1e-12);
				EXPECT_DOUBLE_EQ(axis.deltaV[k], expected.axes[a].deltaV[k]);
				EXPECT_DOUBLE_EQ(axis.modelPotential[k], expected.axes[a].modelPotential[k]);
			}
			EXPECT_DOUBLE_EQ(read.windows[a].centre, entry.windows[a].centre);
			EXPECT_DOUBLE_EQ(read.windows[a].width, entry.windows[a].width);
		}
		EXPECT_FALSE(parsed.corrections[1].profiles.has_value());
	}

	TEST(FnvCorrectionsIOTests, LocpotsInsideTheProjectAreWrittenRelative)
	{
		FnvCorrectionsFile file;
		file.corrections.push_back(Entry("data/GeV/q-1/LOCPOT"));
		const std::string text = FnvCorrectionsIO::Serialize(file, kProject);
		EXPECT_NE(text.find("data/GeV/q-1/LOCPOT"), std::string::npos);
		EXPECT_EQ(text.find("projekty/diament/data"), std::string::npos);
		EXPECT_NE(text.find("E:/obliczenia/bulk/LOCPOT"), std::string::npos);
	}

	TEST(FnvCorrectionsIOTests, AFileFromAFutureVersionIsRejected)
	{
		FnvCorrectionsFile parsed;
		std::vector<std::string> warnings;
		std::string error;
		EXPECT_FALSE(FnvCorrectionsIO::Parse("formatVersion: 2\ncorrections: []\n", kProject, parsed, warnings, error));
		EXPECT_FALSE(error.empty());
		EXPECT_TRUE(FnvCorrectionsIO::Parse("  \n", kProject, parsed, warnings, error)) << error;
		EXPECT_TRUE(parsed.corrections.empty());
		EXPECT_EQ(parsed.active, -1);
	}

	TEST(FnvCorrectionsIOTests, ABrokenEntryIsDroppedAndTheRestKept)
	{
		FnvCorrectionsFile file;
		PersistedFnvCorrection broken = Entry("data/GeV/q-1/LOCPOT");
		broken.profiles = Profiles();
		broken.profiles->axes[1].deltaV.pop_back(); // arrays of unequal length
		file.corrections.push_back(broken);
		file.corrections.push_back(Entry("data/GeV/q+1/LOCPOT"));
		file.active = 0;

		std::vector<std::string> warnings;
		const FnvCorrectionsFile parsed = RoundTrip(file, warnings);
		EXPECT_EQ(warnings.size(), 1u);
		ASSERT_EQ(parsed.corrections.size(), 1u);
		EXPECT_EQ(parsed.corrections[0].defectLocpot, kProject / Path::FromUtf8("data/GeV/q+1/LOCPOT"));
		EXPECT_EQ(parsed.active, -1);
	}

	TEST(FnvCorrectionsIOTests, SaveThenLoadAndAMissingFileIsEmpty)
	{
		const Path directory = Path::FromResolved(FileSystem::TempDirectoryPath()) /
			("defectstudio_fnv_" + std::to_string(Time::NowSteady().time_since_epoch().count()));
		ASSERT_TRUE(FileSystem::CreateDirectories(directory));

		FnvCorrectionsFile loaded;
		std::vector<std::string> warnings;
		std::string error;
		ASSERT_TRUE(FnvCorrectionsIO::Load(directory, loaded, warnings, error)) << error;
		EXPECT_TRUE(loaded.corrections.empty());

		FnvCorrectionsFile file;
		PersistedFnvCorrection entry = Entry("data/GeV/q-1/LOCPOT");
		entry.defectLocpot = directory / "GeV" / "LOCPOT";
		entry.profiles = Profiles();
		file.corrections.push_back(entry);
		file.active = 0;
		ASSERT_TRUE(FnvCorrectionsIO::Save(directory, file, error)) << error;
		EXPECT_TRUE(FileSystem::Exists(FnvCorrectionsIO::FilePath(directory)));
		ASSERT_TRUE(FnvCorrectionsIO::Load(directory, loaded, warnings, error)) << error;
		ASSERT_EQ(loaded.corrections.size(), 1u);
		EXPECT_EQ(loaded.corrections[0].defectLocpot, entry.defectLocpot);
		EXPECT_EQ(loaded.active, 0);
		FileSystem::RemoveAll(directory);
	}

	TEST(FnvCorrectionsIOTests, UpsertReplacesTheSameLocpotAndAppendsAnother)
	{
		FnvCorrectionsFile file;
		EXPECT_EQ(UpsertFnvCorrection(file, Entry("data/GeV/q-1/LOCPOT")), 0);
		EXPECT_EQ(UpsertFnvCorrection(file, Entry("data/GeV/q+1/LOCPOT")), 1);
		EXPECT_EQ(file.active, 1);

		PersistedFnvCorrection changed = Entry("data/GeV/q-1/LOCPOT");
		changed.dielectric = 5.8;
		EXPECT_EQ(UpsertFnvCorrection(file, changed), 0);
		EXPECT_EQ(file.active, 0);
		ASSERT_EQ(file.corrections.size(), 2u);
		EXPECT_DOUBLE_EQ(file.corrections[0].dielectric, 5.8);

		PersistedFnvCorrection unnamed;
		EXPECT_EQ(UpsertFnvCorrection(file, unnamed), -1);
		EXPECT_EQ(file.corrections.size(), 2u);
		EXPECT_EQ(file.active, 0);
	}
} // namespace DefectStudio::Tests
