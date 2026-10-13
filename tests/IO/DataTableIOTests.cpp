#include <gtest/gtest.h>

#include <clocale>
#include <cmath>
#include <limits>

#include "IO/DataTableIO.hpp"

namespace DefectStudio::Tests
{
	TEST(DataTableIOTests, UnequalColumnsArePaddedAndNanIsAnEmptyCell)
	{
		const std::vector<DataTableColumn> columns = {
			{"E", "meV", "", {1.0, 2.5, 3.0}, {}},
			{"S", "", "", {0.5, std::numeric_limits<double>::quiet_NaN()}, {}},
			{"irrep", "", "", {}, {"A1", "E"}}};
		EXPECT_EQ(FormatDataTable(columns, {}), "E\tS\tirrep\n1\t0.5\tA1\n2.5\t\tE\n3\t\t\n");
	}

	TEST(DataTableIOTests, CsvQuotesDelimitersQuotesAndNewlinesTsvFlattensThem)
	{
		const std::vector<DataTableColumn> columns = {{"a,b", "", "", {}, {"say \"x\"", "line\nbreak"}}};
		DataTableFormat csv;
		csv.delimiter = ',';
		EXPECT_EQ(FormatDataTable(columns, csv), "\"a,b\"\n\"say \"\"x\"\"\"\n\"line\nbreak\"\n");
		const std::vector<DataTableColumn> tabbed = {{"a\tb", "", "", {}, {"line\nbreak"}}};
		EXPECT_EQ(FormatDataTable(tabbed, {}), "a b\nline break\n");
	}

	TEST(DataTableIOTests, OriginHeaderAddsUnitsAndCommentsLines)
	{
		DataTableFormat format;
		format.originHeader = true;
		const std::vector<DataTableColumn> columns = {{"E", "eV", "phonons", {1.0}, {}}, {"PL", "", "C2 dimer", {0.25}, {}}};
		EXPECT_EQ(FormatDataTable(columns, format), "E\tPL\neV\t\nphonons\tC2 dimer\n1\t0.25\n");
	}

	TEST(DataTableIOTests, NumbersKeepNineSignificantDigitsAndADotUnderAnyLocale)
	{
		const char *previous = std::setlocale(LC_NUMERIC, nullptr);
		const std::string saved = previous ? previous : "C";
		std::setlocale(LC_NUMERIC, "pl_PL.UTF-8"); // may be unavailable; the check holds either way
		const std::vector<DataTableColumn> columns = {{"x", "", "", {3.14159265358979, -1.0e-12, 200.105}, {}}};
		const std::string text = FormatDataTable(columns, {});
		std::setlocale(LC_NUMERIC, saved.c_str());
		EXPECT_EQ(text, "x\n3.14159265\n-1e-12\n200.105\n");
		EXPECT_NEAR(std::stod("3.14159265"), 3.14159265358979, 1e-8);
	}
} // namespace DefectStudio::Tests
