#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace DefectStudio
{
	class Path
	{
	public:
		Path() = default;
		explicit Path(std::filesystem::path path);

		[[nodiscard]] static Path FromResolved(std::filesystem::path path);
		[[nodiscard]] Path Join(std::string_view child) const;

		Path &operator=(const std::filesystem::path &path);
		[[nodiscard]] Path operator/(const std::filesystem::path &child) const;
		[[nodiscard]] Path operator/(const Path &child) const;
		Path &operator/=(const std::filesystem::path &child);
		Path &operator/=(const Path &child);

		[[nodiscard]] const std::filesystem::path &Native() const;
		[[nodiscard]] std::string String() const;
		[[nodiscard]] bool Empty() const;

	private:
		std::filesystem::path m_Path;
	};
} // namespace DefectStudio
