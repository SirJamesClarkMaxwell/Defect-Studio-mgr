#pragma once

#include <span>
#include <cstddef>
#include <string>
#include <string_view>
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

	// task/113f: a numeric text table read back (measured spectra, other programs' exports).
	struct ParsedDataTable
	{
		std::vector<std::string> names;          // one per column; "" when the file has no header line
		std::vector<std::vector<double>> columns; // all the same length
		std::size_t skippedLines = 0;            // non-empty lines that were not data rows (header, comments, short rows)
		char delimiter = '\t';                   // '\t', ';', ',' or ' ' (any run of spaces/tabs)
	};

	// Delimiter from the first data row: tab, else ';', else ',', else whitespace runs. A data row is a line
	// whose fields (trimmed) all parse as finite numbers, with at least 2 fields; the first data row fixes the
	// column count. With a delimiter other than ',', a field with one ',' and no '.' is a decimal comma
	// ("1,5" = 1.5). Lines before the first data row are skipped; the last of them with the same field count
	// gives the names (Origin's 3-line header: the first of the three - Long Name - is used when the three
	// lines all have that field count). After the first data row, empty lines, '#' comments and rows with a
	// different field count or a non-number are skipped and counted (blank lines are never counted). UTF-8 BOM and '\r' are ignored.
	// False (outError set, out untouched) when no data row exists.
	[[nodiscard]] bool ParseDataTable(std::string_view text, ParsedDataTable &out, std::string &outError);
	// ParseDataTable on a file read through TextFileIO.
	[[nodiscard]] bool LoadDataTable(const Path &file, ParsedDataTable &out, std::string &outError);
} // namespace DefectStudio
