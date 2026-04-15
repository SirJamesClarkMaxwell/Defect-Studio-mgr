#include "Core/dspch.hpp"

#include "Core/Utils/Path.hpp"

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
