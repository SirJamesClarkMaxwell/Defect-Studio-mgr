#pragma once

#include <optional>
#include <string>
#include <unordered_map>

#include "Core/Diagnostics/StructuredError.hpp"
#include "Core/Utils/Path.hpp"

namespace DefectStudio
{
	struct OpenGlShaderProgramPaths
	{
		Path vertexPath;
		Path fragmentPath;
		Path computePath;
	};

	struct OpenGlShaderProgramState
	{
		std::string name;
		OpenGlShaderProgramPaths paths;
		std::filesystem::file_time_type vertexTimestamp{};
		std::filesystem::file_time_type fragmentTimestamp{};
		std::filesystem::file_time_type computeTimestamp{};
		unsigned int programId = 0;
		bool isCompute = false;
	};

	class OpenGlShaderLibrary
	{
	public:
		OpenGlShaderLibrary() = default;
		~OpenGlShaderLibrary();

		Result<void> LoadGraphicsProgram(const std::string &name, const Path &vertexPath, const Path &fragmentPath);
		Result<void> LoadComputeProgram(const std::string &name, const Path &computePath);
		void ReloadModifiedPrograms();
		[[nodiscard]] unsigned int Program(const std::string &name) const;

	private:
		[[nodiscard]] Result<void> compileGraphicsProgram(OpenGlShaderProgramState &state);
		[[nodiscard]] Result<void> compileComputeProgram(OpenGlShaderProgramState &state);
		[[nodiscard]] Result<unsigned int> compileShader(unsigned int type, const std::string &source, const Path &path) const;
		[[nodiscard]] Result<std::string> loadTextFile(const Path &path) const;
		[[nodiscard]] std::filesystem::file_time_type readTimestamp(const Path &path) const;
		static void releaseProgram(OpenGlShaderProgramState &state);

	private:
		std::unordered_map<std::string, OpenGlShaderProgramState> m_Programs;
	};
} // namespace DefectStudio

