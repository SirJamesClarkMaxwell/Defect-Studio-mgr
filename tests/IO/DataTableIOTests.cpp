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

	TEST(DataTableIOTests, ParsesHeaderCommentsAndWhitespace)
	{
		ParsedDataTable table;
		std::string error;
		ASSERT_TRUE(ParseDataTable("\xEF\xBB\xBF" "measured 2024\r\nE intensity\r\n1.9  10\r\n# note\r\n\r\n2.0\t20\r\n2.1 x\r\n2.2 40 7\r\n", table, error)) << error;
		EXPECT_EQ(table.delimiter, ' ');
		ASSERT_EQ(table.columns.size(), 2u);
		EXPECT_EQ(table.names, (std::vector<std::string>{"E", "intensity"}));
		EXPECT_EQ(table.columns[0], (std::vector<double>{1.9, 2.0}));
		EXPECT_EQ(table.columns[1], (std::vector<double>{10.0, 20.0}));
		EXPECT_EQ(table.skippedLines, 5u); // "measured 2024", header, comment, "2.1 x", the 3-field row
	}

	TEST(DataTableIOTests, DetectsDelimitersAndDecimalComma)
	{
		ParsedDataTable table;
		std::string error;
		ASSERT_TRUE(ParseDataTable("x;y\n1,5;2\n2,5;3,25\n", table, error)) << error;
		EXPECT_EQ(table.delimiter, ';');
		EXPECT_EQ(table.columns[0], (std::vector<double>{1.5, 2.5}));
		EXPECT_EQ(table.columns[1], (std::vector<double>{2.0, 3.25}));

		ASSERT_TRUE(ParseDataTable("a,b,c\n1,2,3\n4,5,6\n", table, error)) << error;
		EXPECT_EQ(table.delimiter, ',');
		EXPECT_EQ(table.columns.size(), 3u);
		EXPECT_EQ(table.columns[2], (std::vector<double>{3.0, 6.0}));

		ASSERT_TRUE(ParseDataTable("E\tL\n1e0\t-2.5E-3\n", table, error)) << error;
		EXPECT_EQ(table.delimiter, '\t');
		EXPECT_DOUBLE_EQ(table.columns[1][0], -2.5e-3);
	}

	TEST(DataTableIOTests, OriginHeaderUsesLongNamesAndRoundTrips)
	{
		const std::vector<DataTableColumn> columns = {{"E", "eV", "exp", {2.0, 2.1}, {}}, {"L", "1/eV", "", {1.0, 0.5}, {}}};
		const std::string text = FormatDataTable(columns, {'\t', true, 9});
		ParsedDataTable table;
		std::string error;
		ASSERT_TRUE(ParseDataTable(text, table, error)) << error;
		EXPECT_EQ(table.names, (std::vector<std::string>{"E", "L"}));
		EXPECT_EQ(table.columns[0], (std::vector<double>{2.0, 2.1}));
		EXPECT_EQ(table.skippedLines, 3u);
	}

	TEST(DataTableIOTests, NoDataRowIsAnErrorAndLeavesOutputUntouched)
	{
		ParsedDataTable table;
		table.skippedLines = 42;
		std::string error;
		EXPECT_FALSE(ParseDataTable("only text\n1\n# 2 3\n", table, error));
		EXPECT_FALSE(error.empty());
		EXPECT_EQ(table.skippedLines, 42u);
		EXPECT_FALSE(ParseDataTable("", table, error));
	}
} // namespace DefectStudio::Tests
