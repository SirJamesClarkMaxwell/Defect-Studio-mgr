#include "Core/dspch.hpp"

#include "IO/TextFileIO.hpp"

#include <fstream>
#include <iterator>

namespace DefectStudio
{
	bool TextFileIO::Save(const Path &path, std::string_view text, std::string &error)
	{
		if (path.Empty())
		{
			error = "Target path is empty";
			return false;
		}

		if (!path.parent_path().empty())
		{
			std::error_code directoryError;
			FileSystem::CreateDirectories(path.parent_path().Native(), directoryError);
			if (directoryError)
			{
				error = directoryError.message();
				return false;
			}
		}

		std::ofstream stream(path.Native(), std::ios::binary | std::ios::trunc);
		if (!stream)
		{
			error = "Unable to open file for writing: " + path.String();
			return false;
		}

		stream.write(text.data(), static_cast<std::streamsize>(text.size()));
		if (stream.good())
			return true;

		error = "Unable to write file: " + path.String();
		return false;
	}

	bool TextFileIO::Load(const Path &path, std::string &text, std::string &error)
	{
		if (path.Empty())
		{
			error = "Target path is empty";
			return false;
		}

		std::ifstream stream(path.Native(), std::ios::binary);
		if (!stream)
		{
			error = "Unable to open file for reading: " + path.String();
			return false;
		}

		text.assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
		return true;
	}
} // namespace DefectStudio