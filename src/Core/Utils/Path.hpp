#pragma once

#include <filesystem>
#include <system_error>
#include <string>
#include <string_view>

using FilePath = std::filesystem::path;

class FileSystem
{
public:
	[[nodiscard]] static FilePath CurrentPath();
	[[nodiscard]] static FilePath Absolute(const FilePath &path, std::error_code &error);
	[[nodiscard]] static FilePath TempDirectoryPath();
	[[nodiscard]] static FilePath TempDirectoryPath(std::error_code &error);

	[[nodiscard]] static bool Exists(const FilePath &path);
	static bool CreateDirectories(const FilePath &path);
	static bool CreateDirectories(const FilePath &path, std::error_code &error);
	static bool Remove(const FilePath &path);
	static bool Remove(const FilePath &path, std::error_code &error);
	static std::uintmax_t RemoveAll(const FilePath &path);
	static std::uintmax_t RemoveAll(const FilePath &path, std::error_code &error);
};

namespace DefectStudio
{
	using FilePath = ::FilePath;
	using FileSystem = ::FileSystem;

	class Path
	{
	public:
		Path() = default;
		Path(std::filesystem::path path);

		[[nodiscard]] static Path FromResolved(std::filesystem::path path);
		[[nodiscard]] Path Join(std::string_view child) const;
		[[nodiscard]] Path parent_path() const;
		[[nodiscard]] Path filename() const;
		[[nodiscard]] Path extension() const;
		[[nodiscard]] std::string string() const;
		[[nodiscard]] std::wstring wstring() const;
		[[nodiscard]] bool empty() const;

		operator const std::filesystem::path &() const;

		[[nodiscard]] bool operator==(const Path &other) const;
		[[nodiscard]] bool operator!=(const Path &other) const;
		[[nodiscard]] bool operator==(const std::filesystem::path &other) const;
		[[nodiscard]] bool operator!=(const std::filesystem::path &other) const;

		Path &operator=(const std::filesystem::path &path);
		[[nodiscard]] Path operator/(const std::filesystem::path &child) const;
		[[nodiscard]] Path operator/(const Path &child) const;
		Path &operator/=(const std::filesystem::path &child);
		Path &operator/=(const Path &child);
		friend Path operator/(const std::filesystem::path &left, const Path &right);

		[[nodiscard]] const std::filesystem::path &Native() const;
		[[nodiscard]] std::string String() const;
		[[nodiscard]] bool Empty() const;

	private:
		std::filesystem::path m_Path;
	};
} // namespace DefectStudio
