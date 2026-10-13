#pragma once

#include <span>
#include <string>
#include <vector>

#include "Core/Utils/Path.hpp"

namespace DefectStudio
{
	// task/110: one exported column - numeric (values, NaN = empty cell) or text (text non-empty, e.g. irreps).
	struct DataTableColumn
	{
		std::string name;
		std::string unit;
		std::string comment;
		std::vector<double> values;
		std::vector<std::string> text;
	};

	struct DataTableFormat
	{
		char delimiter = '\t';
		bool originHeader = false; // adds "Units" and "Comments" lines (Origin: Long Name / Units / Comments)
		int significantDigits = 9;
	};

	// One header line of names (+ units and comments lines with originHeader), then rows; columns of unequal
	// length are padded with empty cells. CSV (delimiter ',') quotes cells holding the delimiter, a quote or a
	// newline (RFC 4180); TSV turns tabs/newlines inside text into spaces. UTF-8, '\n', '.' decimal point always.
	[[nodiscard]] std::string FormatDataTable(std::span<const DataTableColumn> columns, const DataTableFormat &format);
	// FormatDataTable written through TextFileIO.
	[[nodiscard]] bool SaveDataTable(const Path &file, std::span<const DataTableColumn> columns,
		const DataTableFormat &format, std::string &outError);
} // namespace DefectStudio
