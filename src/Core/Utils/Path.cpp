#include "Core/dspch.hpp"

#include "Core/Utils/Path.hpp"

FilePath FileSystem::CurrentPath()
{
	return std::filesystem::current_path();
}

FilePath FileSystem::Absolute(const FilePath &path, std::error_code &error)
{
	return std::filesystem::absolute(path, error);
}

FilePath FileSystem::WeaklyCanonical(const FilePath &path, std::error_code &error)
{
	return std::filesystem::weakly_canonical(path, error);
}

FilePath FileSystem::TempDirectoryPath()
{
	return std::filesystem::temp_directory_path();
}

FilePath FileSystem::TempDirectoryPath(std::error_code &error)
{
	return std::filesystem::temp_directory_path(error);
}

bool FileSystem::Exists(const FilePath &path)
{
	return std::filesystem::exists(path.native());
}

bool FileSystem::CreateDirectories(const FilePath &path)
{
	return std::filesystem::create_directories(path);
}

bool FileSystem::CreateDirectories(const FilePath &path, std::error_code &error)
{
	return std::filesystem::create_directories(path, error);
}

bool FileSystem::Remove(const FilePath &path)
{
	return std::filesystem::remove(path);
}

bool FileSystem::Remove(const FilePath &path, std::error_code &error)
{
	return std::filesystem::remove(path, error);
}

std::uintmax_t FileSystem::RemoveAll(const FilePath &path)
{
	return std::filesystem::remove_all(path);
}

std::uintmax_t FileSystem::RemoveAll(const FilePath &path, std::error_code &error)
	{
	return std::filesystem::remove_all(path, error);
}

namespace DefectStudio
	{
	Path::Path(std::filesystem::path path) : m_Path(std::move(path))
	{
	}

	Path Path::FromResolved(std::filesystem::path path)
	{
		return Path(std::move(path));
	}

	Path Path::Join(std::string_view child) const
	{
		return Path(m_Path / std::filesystem::path(child));
	}

	Path Path::parent_path() const
	{
		return Path(m_Path.parent_path());
	}

	Path Path::filename() const
	{
		return Path(m_Path.filename());
	}

	Path Path::extension() const
	{
		return Path(m_Path.extension());
	}

	std::string Path::string() const
	{
		return m_Path.string();
	}

	std::wstring Path::wstring() const
	{
		return m_Path.wstring();
	}

	bool Path::empty() const
	{
		return m_Path.empty();
	}

	Path::operator const std::filesystem::path &() const
	{
		return m_Path;
	}

	bool Path::operator==(const Path &other) const
	{
		return m_Path == other.m_Path;
	}

	bool Path::operator!=(const Path &other) const
	{
		return !(*this == other);
	}

	bool Path::operator==(const std::filesystem::path &other) const
	{
		return m_Path == other;
	}

	bool Path::operator!=(const std::filesystem::path &other) const
	{
		return !(*this == other);
	}

	Path &Path::operator=(const std::filesystem::path &path)
	{
		m_Path = path;
		return *this;
	}

	Path Path::operator/(const std::filesystem::path &child) const
	{
		return Path(m_Path / child);
	}

	Path Path::operator/(const Path &child) const
	{
		return Path(m_Path / child.m_Path);
	}

	Path &Path::operator/=(const std::filesystem::path &child)
	{
		m_Path /= child;
		return *this;
	}

	Path &Path::operator/=(const Path &child)
	{
		m_Path /= child.m_Path;
		return *this;
	}

	Path operator/(const std::filesystem::path &left, const Path &right)
	{
		return Path(left / right.Native());
	}

	const std::filesystem::path &Path::Native() const
	{
		return m_Path;
	}

	std::string Path::String() const
	{
		return m_Path.string();
	}

	bool Path::Empty() const
	{
		return m_Path.empty();
	}
	} // namespace DefectStudio
