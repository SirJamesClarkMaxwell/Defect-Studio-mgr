#include "Core/dspch.hpp"

#include "IO/LogCsvIO.hpp"

#include <span>

#include "IO/TextFileIO.hpp"

namespace DefectStudio
{
	namespace
	{
		std::string CsvEscape(const std::string &value)
		{
			std::string escaped;
			escaped.reserve(value.size() + 8);
			escaped.push_back('"');
			for (const char ch : value)
			{
				if (ch == '"')
					escaped += "\"\"";
				else
					escaped.push_back(ch);
			}
			escaped.push_back('"');
			return escaped;
		}

		StructuredError MakeLogCsvError(
			std::string userMessage,
			std::string technicalDetails,
			std::string suggestion,
			std::string code)
		{
			return StructuredError{
				ErrorCategory::IO,
				Severity::Error,
				std::move(userMessage),
				std::move(technicalDetails),
				std::move(suggestion),
				"IO/LogCsvIO",
				std::move(code)};
		}
	} // namespace

	std::string LogCsvIO::Serialize(std::span<const LogEntry> entries)
	{
		std::string csv;
		csv += "timestamp,category,severity,origin,logger,message,formatted\n";
		for (const LogEntry &entry : entries)
		{
			csv += CsvEscape(entry.TimestampString());
			csv += ',';
			csv += CsvEscape(ToString(entry.category));
			csv += ',';
			csv += CsvEscape(ToString(entry.level));
			csv += ',';
			csv += CsvEscape(entry.Origin());
			csv += ',';
			csv += CsvEscape(entry.loggerName);
			csv += ',';
			csv += CsvEscape(entry.message);
			csv += ',';
			csv += CsvEscape(entry.ToString());
			csv += '\n';
		}
		return csv;
	}

	Result<std::size_t> LogCsvIO::Save(const Path &path, std::span<const LogEntry> entries)
	{
		const std::string csv = Serialize(entries);
		std::string error;
		if (!TextFileIO::Save(path, csv, error))
		{
			return MakeLogCsvError(
				"Log CSV export failed.",
				error,
				"Check export directory permissions and target path.",
				"io.logs.export_failed");
		}
		return csv.size();
	}
} // namespace DefectStudio
