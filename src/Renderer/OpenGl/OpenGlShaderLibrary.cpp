#include "Core/dspch.hpp"

#include "Renderer/OpenGl/OpenGlShaderLibrary.hpp"

#include <fstream>
#include <sstream>

#include <glad/gl.h>

#include "Core/Utils/Logger.hpp"

namespace DefectStudio
{
	OpenGlShaderLibrary::~OpenGlShaderLibrary()
	{
		for (auto &[name, state] : m_Programs)
		{
			(void)name;
			releaseProgram(state);
		}
	}

	Result<void> OpenGlShaderLibrary::LoadGraphicsProgram(const std::string &name, const Path &vertexPath, const Path &fragmentPath)
	{
		OpenGlShaderProgramState state;
		state.name = name;
		state.paths.vertexPath = vertexPath;
		state.paths.fragmentPath = fragmentPath;
		state.isCompute = false;
		Result<void> compiled = compileGraphicsProgram(state);
		if (!compiled.HasValue())
			return compiled.Error();
		m_Programs[name] = state;
		return {};
	}

	Result<void> OpenGlShaderLibrary::LoadComputeProgram(const std::string &name, const Path &computePath)
	{
		OpenGlShaderProgramState state;
		state.name = name;
		state.paths.computePath = computePath;
		state.isCompute = true;
		Result<void> compiled = compileComputeProgram(state);
		if (!compiled.HasValue())
			return compiled.Error();
		m_Programs[name] = state;
		return {};
	}

	void OpenGlShaderLibrary::ReloadModifiedPrograms()
	{
		for (auto &[name, state] : m_Programs)
		{
			(void)name;
			const std::filesystem::file_time_type vertexTimestamp = readTimestamp(state.paths.vertexPath);
			const std::filesystem::file_time_type fragmentTimestamp = readTimestamp(state.paths.fragmentPath);
			const std::filesystem::file_time_type computeTimestamp = readTimestamp(state.paths.computePath);

			bool changed = false;
			if (!state.isCompute && (vertexTimestamp != state.vertexTimestamp || fragmentTimestamp != state.fragmentTimestamp))
				changed = true;
			if (state.isCompute && computeTimestamp != state.computeTimestamp)
				changed = true;
			if (!changed)
				continue;

			Result<void> compiled = state.isCompute
				? compileComputeProgram(state)
				: compileGraphicsProgram(state);
			if (!compiled.HasValue())
			{
				DS_LOG_ERROR(
					"Renderer shader hot-reload failed [{}]: {}",
					state.name,
					compiled.Error().technicalDetails);
				continue;
			}
			DS_LOG_INFO("Renderer shader reloaded: {}", state.name);
		}
	}

	unsigned int OpenGlShaderLibrary::Program(const std::string &name) const
	{
		auto found = m_Programs.find(name);
		if (found == m_Programs.end())
			return 0;
		return found->second.programId;
	}

	Result<void> OpenGlShaderLibrary::compileGraphicsProgram(OpenGlShaderProgramState &state)
	{
		Result<std::string> vertexSource = loadTextFile(state.paths.vertexPath);
		if (!vertexSource.HasValue())
			return vertexSource.Error();
		Result<std::string> fragmentSource = loadTextFile(state.paths.fragmentPath);
		if (!fragmentSource.HasValue())
			return fragmentSource.Error();

		Result<unsigned int> vertexShader = compileShader(GL_VERTEX_SHADER, vertexSource.Value(), state.paths.vertexPath);
		if (!vertexShader.HasValue())
			return vertexShader.Error();
		Result<unsigned int> fragmentShader = compileShader(GL_FRAGMENT_SHADER, fragmentSource.Value(), state.paths.fragmentPath);
		if (!fragmentShader.HasValue())
		{
			glDeleteShader(vertexShader.Value());
			return fragmentShader.Error();
		}

		const unsigned int program = glCreateProgram();
		glAttachShader(program, vertexShader.Value());
		glAttachShader(program, fragmentShader.Value());
		glLinkProgram(program);
		glDeleteShader(vertexShader.Value());
		glDeleteShader(fragmentShader.Value());

		int linkStatus = 0;
		glGetProgramiv(program, GL_LINK_STATUS, &linkStatus);
		if (linkStatus == GL_FALSE)
		{
			int logLength = 0;
			glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLength);
			std::string log;
			log.resize(static_cast<std::size_t>(std::max(logLength, 1)));
			glGetProgramInfoLog(program, logLength, nullptr, log.data());
			glDeleteProgram(program);
			return StructuredError{
				ErrorCategory::Runtime,
				Severity::Error,
				"Failed to link OpenGL graphics program.",
				"Program '" + state.name + "' link log: " + log,
				"Fix shader code and reload.",
				"renderer.shader.link_failed"};
		}

		releaseProgram(state);
		state.programId = program;
		state.vertexTimestamp = readTimestamp(state.paths.vertexPath);
		state.fragmentTimestamp = readTimestamp(state.paths.fragmentPath);
		return {};
	}

	Result<void> OpenGlShaderLibrary::compileComputeProgram(OpenGlShaderProgramState &state)
	{
		Result<std::string> computeSource = loadTextFile(state.paths.computePath);
		if (!computeSource.HasValue())
			return computeSource.Error();

		Result<unsigned int> computeShader = compileShader(GL_COMPUTE_SHADER, computeSource.Value(), state.paths.computePath);
		if (!computeShader.HasValue())
			return computeShader.Error();

		const unsigned int program = glCreateProgram();
		glAttachShader(program, computeShader.Value());
		glLinkProgram(program);
		glDeleteShader(computeShader.Value());

		int linkStatus = 0;
		glGetProgramiv(program, GL_LINK_STATUS, &linkStatus);
		if (linkStatus == GL_FALSE)
		{
			int logLength = 0;
			glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLength);
			std::string log;
			log.resize(static_cast<std::size_t>(std::max(logLength, 1)));
			glGetProgramInfoLog(program, logLength, nullptr, log.data());
			glDeleteProgram(program);
			return StructuredError{
				ErrorCategory::Runtime,
				Severity::Error,
				"Failed to link OpenGL compute program.",
				"Program '" + state.name + "' link log: " + log,
				"Fix compute shader code and reload.",
				"renderer.compute.link_failed"};
		}

		releaseProgram(state);
		state.programId = program;
		state.computeTimestamp = readTimestamp(state.paths.computePath);
		return {};
	}

	Result<unsigned int> OpenGlShaderLibrary::compileShader(unsigned int type, const std::string &source, const Path &path) const
	{
		const unsigned int shader = glCreateShader(type);
		const char *sourcePtr = source.c_str();
		glShaderSource(shader, 1, &sourcePtr, nullptr);
		glCompileShader(shader);

		int compileStatus = 0;
		glGetShaderiv(shader, GL_COMPILE_STATUS, &compileStatus);
		if (compileStatus == GL_FALSE)
		{
			int logLength = 0;
			glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);
			std::string log;
			log.resize(static_cast<std::size_t>(std::max(logLength, 1)));
			glGetShaderInfoLog(shader, logLength, nullptr, log.data());
			glDeleteShader(shader);
			return StructuredError{
				ErrorCategory::Runtime,
				Severity::Error,
				"Failed to compile OpenGL shader.",
				"Shader '" + path.String() + "' compile log: " + log,
				"Fix GLSL syntax and reload.",
				"renderer.shader.compile_failed"};
		}

		return shader;
	}

	Result<std::string> OpenGlShaderLibrary::loadTextFile(const Path &path) const
	{
		std::ifstream stream(path.Native());
		if (!stream)
		{
			return StructuredError{
				ErrorCategory::IO,
				Severity::Error,
				"Renderer shader file could not be opened.",
				"Cannot open shader: " + path.String(),
				"Check shader path and file permissions.",
				"renderer.shader.open_failed"};
		}

		std::ostringstream text;
		text << stream.rdbuf();
		return text.str();
	}

	std::filesystem::file_time_type OpenGlShaderLibrary::readTimestamp(const Path &path) const
	{
		if (path.Empty())
			return {};
		std::error_code error;
		const auto timestamp = std::filesystem::last_write_time(path.Native(), error);
		if (error)
			return {};
		return timestamp;
	}

	void OpenGlShaderLibrary::releaseProgram(OpenGlShaderProgramState &state)
	{
		if (state.programId == 0)
			return;
		glDeleteProgram(state.programId);
		state.programId = 0;
	}
} // namespace DefectStudio
