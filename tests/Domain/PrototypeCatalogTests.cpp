#include <algorithm>

#include <gtest/gtest.h>

#include "Domain/Crystal/FormulaParser.hpp"
#include "Domain/Crystal/PrototypeLoader.hpp"
#include "Domain/Crystal/PrototypeMatcher.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		[[nodiscard]] PrototypeDefinition MakeTwoSitePrototype(int firstMultiplicity, int secondMultiplicity)
		{
			PrototypeDefinition prototype;
			prototype.name = "test";
			prototype.sites.push_back(SiteDefinition{"A", std::vector<glm::vec3>(static_cast<std::size_t>(firstMultiplicity))});
			prototype.sites.push_back(SiteDefinition{"B", std::vector<glm::vec3>(static_cast<std::size_t>(secondMultiplicity))});
			return prototype;
		}
	} // namespace

	// The case the wizard rejected outright: a formula is a stoichiometric ratio, so one element
	// fills every site of a single-element prototype instead of having to count 8 atoms.
	TEST(PrototypeMatcherTests, SingleElementFillsEverySite)
	{
		const PrototypeDefinition diamond = MakeTwoSitePrototype(4, 4);
		const std::optional<SiteAssignment> matched =
			PrototypeMatcher::MatchFormulaToPrototype(FormulaParser::Parse("Si"), diamond);

		ASSERT_TRUE(matched.has_value());
		EXPECT_EQ(matched->species, (std::vector<std::string>{"Si", "Si"}));
	}

	TEST(PrototypeMatcherTests, BinaryFormulaMapsInFormulaOrder)
	{
		const PrototypeDefinition zincblende = MakeTwoSitePrototype(4, 4);
		const std::optional<SiteAssignment> matched =
			PrototypeMatcher::MatchFormulaToPrototype(FormulaParser::Parse("GaAs"), zincblende);

		ASSERT_TRUE(matched.has_value());
		EXPECT_EQ(matched->species, (std::vector<std::string>{"Ga", "As"}));
		EXPECT_TRUE(matched->isAmbiguous) << "equal multiplicities mean only formula order decided this";
	}

	TEST(PrototypeMatcherTests, RatioThatDoesNotDivideTheSitesIsRejected)
	{
		const PrototypeDefinition zincblende = MakeTwoSitePrototype(4, 4);
		// Al2O3 is 5 formula units against 8 sites - no whole-number scaling exists.
		EXPECT_FALSE(PrototypeMatcher::MatchFormulaToPrototype(FormulaParser::Parse("Al2O3"), zincblende).has_value());
	}

	TEST(PrototypeMatcherTests, UnequalMultiplicitiesAreNotAmbiguous)
	{
		const PrototypeDefinition prototype = MakeTwoSitePrototype(2, 4);
		const std::optional<SiteAssignment> matched =
			PrototypeMatcher::MatchFormulaToPrototype(FormulaParser::Parse("AB2"), prototype);

		ASSERT_TRUE(matched.has_value());
		EXPECT_EQ(matched->species, (std::vector<std::string>{"A", "B"}));
		EXPECT_FALSE(matched->isAmbiguous);
	}

	// LoadBuiltIn used to be a stub returning an empty catalogue while the panel carried its own
	// hardcoded copy, so neither YAML file was ever read and the lattice constants in materials.yaml
	// were dead data.
	TEST(PrototypeLoaderTests, BuiltInCatalogueCarriesPrototypesAndMaterials)
	{
		const Result<PrototypesAndMaterials> loaded = PrototypeLoader::LoadBuiltIn();
		if (!loaded)
			GTEST_SKIP() << "prototypes.yaml not reachable from the test working directory: "
						 << loaded.Error().technicalDetails;

		EXPECT_FALSE(loaded->prototypes.empty());
		EXPECT_FALSE(loaded->materials.empty());

		const auto diamond = std::find_if(
			loaded->prototypes.begin(),
			loaded->prototypes.end(),
			[](const PrototypeDefinition &prototype) { return prototype.name == "diamond"; });
		ASSERT_NE(diamond, loaded->prototypes.end());

		int atomCount = 0;
		for (const SiteDefinition &site : diamond->sites)
			atomCount += site.Multiplicity();
		EXPECT_EQ(atomCount, 8) << "the conventional diamond cell holds 8 atoms";
		EXPECT_EQ(diamond->crystalSystem, "Cubic");

		// Every polytype a material names must exist as a prototype, or picking that material in the
		// wizard silently does nothing.
		for (const MaterialDefinition &material : loaded->materials)
		{
			for (const auto &polytype : material.polytypes)
			{
				const bool known = std::any_of(
					loaded->prototypes.begin(),
					loaded->prototypes.end(),
					[&](const PrototypeDefinition &prototype) { return prototype.name == polytype.second; });
				EXPECT_TRUE(known) << material.name << " / " << polytype.first
								   << " references unknown prototype " << polytype.second;
			}
		}
	}
} // namespace DefectStudio::Tests
