#include "Core/dspch.hpp"

#include "Core/Utils/PathValidation.hpp"

#include <array>
#include <cctype>
#include <string_view>

namespace DefectStudio::PathValidation
{
	namespace
	{
		constexpr std::size_t MaxNameLengthUtf16 = 255; // NTFS component limit, in UTF-16 code units

		[[nodiscard]] StructuredError NameError(std::string userMessage, std::string technicalDetails)
		{
			return StructuredError(
				ErrorCategory::Validation,
				Severity::Error,
				std::move(userMessage),
				std::move(technicalDetails),
				"Use letters, digits, spaces, '-' or '_' and keep the name under 255 characters.",
				"PathValidation::ValidateAndSanitizeName");
		}

		[[nodiscard]] std::string_view Trim(std::string_view text)
		{
			const auto isSpace = [](unsigned char character) { return std::isspace(character) != 0; };
			while (!text.empty() && isSpace(static_cast<unsigned char>(text.front())))
				text.remove_prefix(1);
			while (!text.empty() && isSpace(static_cast<unsigned char>(text.back())))
				text.remove_suffix(1);
			return text;
		}

		// UTF-16 code units the name would occupy on NTFS, counted from its UTF-8 bytes. Doing this
		// by hand keeps the limit exact without depending on the platform's narrow-string codepage.
		[[nodiscard]] std::size_t Utf16LengthOf(std::string_view utf8)
		{
			std::size_t units = 0;
			for (std::size_t i = 0; i < utf8.size();)
			{
				const auto lead = static_cast<unsigned char>(utf8[i]);
				std::size_t sequenceLength = 1;
				if ((lead & 0xE0u) == 0xC0u)
					sequenceLength = 2;
				else if ((lead & 0xF0u) == 0xE0u)
					sequenceLength = 3;
				else if ((lead & 0xF8u) == 0xF0u)
					sequenceLength = 4;

				units += (sequenceLength == 4) ? 2 : 1; // Non-BMP code points are surrogate pairs
				i += sequenceLength;
			}
			return units;
		}

		[[nodiscard]] std::string ToUpperAscii(std::string_view text)
		{
			std::string upper;
			upper.reserve(text.size());
			for (const char character : text)
				upper.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(character))));
			return upper;
		}

		// Windows reserves these regardless of extension: "con.txt" and "CoM1.tar.gz" are both refused
		// by the OS, so refusing them here keeps our error message better than the OS's.
		[[nodiscard]] bool IsReservedWindowsName(std::string_view name)
		{
			const std::string stem = ToUpperAscii(name.substr(0, name.find('.')));
			if (stem == "CON" || stem == "PRN" || stem == "AUX" || stem == "NUL")
				return true;

			for (const std::string_view prefix : {std::string_view("COM"), std::string_view("LPT")})
			{
				if (stem.size() == prefix.size() + 1 && stem.starts_with(prefix)
					&& std::isdigit(static_cast<unsigned char>(stem.back())) != 0)
					return true;
			}
			return false;
		}
	} // namespace

	Result<std::string> ValidateAndSanitizeName(std::string_view userInput)
	{
		const std::string_view trimmed = Trim(userInput);
		if (trimmed.empty())
			return NameError("Structure name is empty", "Name was empty or whitespace only");

		if (Utf16LengthOf(trimmed) > MaxNameLengthUtf16)
			return NameError("Structure name is too long", "Name exceeds the 255 UTF-16 code unit filesystem limit");

		for (const char character : trimmed)
		{
			const auto value = static_cast<unsigned char>(character);
			if (value < 0x20u)
				return NameError("Structure name contains a control character", "Control characters (0x00-0x1F) are not allowed");

			switch (character)
			{
				case '/':
				case '\\':
				case ':':
				case '|':
				case '?':
				case '*':
				case '<':
				case '>':
				case '"':
				case '~':
					return NameError(
						std::string("Structure name contains an invalid character: ") + character,
						"Rejected character in directory component");
				default:
					break;
			}
		}

		if (trimmed == "." || trimmed == "..")
			return NameError("Structure name is a path traversal component", "'.' and '..' are not valid directory names");

		// Windows silently strips these, which makes "Si " and "Si" the same directory on disk.
		if (trimmed.back() == '.' || trimmed.back() == ' ')
			return NameError("Structure name ends with a dot or space", "Trailing dots/spaces are stripped by Windows and cause name collisions");

		if (IsReservedWindowsName(trimmed))
			return NameError("Structure name is reserved by Windows", "Reserved device name (CON, PRN, AUX, NUL, COM0-9, LPT0-9)");

		return std::string(trimmed);
	}

	bool IsAncestor(const FilePath &canonicalAncestor, const FilePath &canonicalPath)
	{
		if (canonicalAncestor.empty() || canonicalPath.empty())
			return false;

		auto ancestorIt = canonicalAncestor.begin();
		auto pathIt = canonicalPath.begin();
		for (; ancestorIt != canonicalAncestor.end(); ++ancestorIt, ++pathIt)
		{
			if (pathIt == canonicalPath.end())
				return false;

#ifdef DS_PLATFORM_WINDOWS
			if (ToUpperAscii(ancestorIt->string()) != ToUpperAscii(pathIt->string()))
				return false;
#else
			if (*ancestorIt != *pathIt)
				return false;
#endif
		}
		return true;
	}

	Result<void> RejectSymlinkComponents(const FilePath &path)
	{
		FilePath prefix;
		for (const FilePath &component : path)
		{
			prefix /= component;

			std::error_code error;
			const std::filesystem::file_status status = std::filesystem::symlink_status(prefix, error);
			if (error)
				continue; // Component does not exist (or is unreadable): nothing to follow here

			if (std::filesystem::is_symlink(status))
			{
				return StructuredError(
					ErrorCategory::Validation,
					Severity::Error,
					"Target path contains a symbolic link",
					"Symlink/junction component: " + prefix.string(),
					"Select a directory that is not reached through a symlink or junction.",
					"PathValidation::RejectSymlinkComponents");
			}
		}
		return Result<void>{};
	}
} // namespace DefectStudio::PathValidation
