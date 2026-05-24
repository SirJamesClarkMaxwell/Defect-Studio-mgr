#pragma once

#include <string>

#include "Core/Utils/Path.hpp"

namespace DefectStudio::Tests
{
	inline Path FindRepoRoot()
	{
		Path cursor = Path::FromResolved(FileSystem::CurrentPath());
		for (int depth = 0; depth < 10; ++depth)
		{
			if (FileSystem::Exists((cursor / "pyproject.toml").Native()))
				return cursor;

			const Path parent = cursor.parent_path();
			if (parent.Empty() || parent == cursor)
				break;
			cursor = parent;
		}

		return Path::FromResolved(FileSystem::CurrentPath());
	}

	inline Path PythonExamplePath(const std::string &fileName)
	{
		return FindRepoRoot() / "scripts" / "python" / "examples" / fileName;
	}

	inline Path ScientificRuntimeFixturePath(const std::string &fileName)
	{
		return FindRepoRoot() / "tests" / "ScientificRuntime" / "fixtures" / fileName;
	}
} // namespace DefectStudio::Tests
