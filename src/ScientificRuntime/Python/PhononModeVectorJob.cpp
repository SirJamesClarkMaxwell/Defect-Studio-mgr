#include "Core/dspch.hpp"
#include "ScientificRuntime/Python/PhononModeVectorJob.hpp"

#include <sstream>
#include <algorithm>
#include <stdexcept>
#include <nlohmann/json.hpp>
#include "Core/JobSystem/JobContext.hpp"
#include "ScientificRuntime/Python/PythonErrors.hpp"
#include "ScientificRuntime/Python/ScriptBridgeUtils.hpp"

namespace DefectStudio
{
	PhononModeVectorJob::PhononModeVectorJob(Path embeddedDirectory, Path outputDirectory, std::size_t mode)
		: m_EmbeddedDirectory(std::move(embeddedDirectory)), m_OutputDirectory(std::move(outputDirectory)), m_Mode(mode) {}

	PhononModeVectorJob::~PhononModeVectorJob()
	{
		if (!m_OwnedOutput.empty()) { std::error_code ignored; FileSystem::RemoveAll(m_OwnedOutput, ignored); }
	}

	std::string PhononModeVectorJob::GetName() const { return "Wczytywanie wektora modu"; }
	std::string PhononModeVectorJob::GetType() const { return "phonon-mode-vector"; }

	void PhononModeVectorJob::Execute(JobContext &context)
	{
		context.ThrowIfCancellationRequested();
		m_Result.reset();
		m_Error.reset();
		context.SetStage("phonon-mode-vector");
		context.SetMessage("Wczytywanie jednego modu z modes.h5");
		context.SetProgress(0.0f, 1.0f);
		try
		{
			// The session supplies a fresh UUID directory inside the project. Never remove pre-existing data.
			if (m_OutputDirectory.Empty() || FileSystem::Exists(m_OutputDirectory.Native()))
				throw std::invalid_argument("Mode vector output must be a new directory");
			const auto embedded = std::filesystem::weakly_canonical(m_EmbeddedDirectory.Native());
			const auto output = std::filesystem::weakly_canonical(m_OutputDirectory.Native());
			const auto system = embedded.parent_path().parent_path();
			const auto contains = [](const FilePath &parent, const FilePath &child) {
				const auto relative = child.lexically_relative(parent);
				return !relative.empty() && !relative.is_absolute() &&
					std::none_of(relative.begin(), relative.end(), [](const auto &part) { return part == ".."; });
			};
			if (contains(system, output) || contains(output, system))
				throw std::invalid_argument("Mode output must be outside the server system tree");
			struct Cleanup
			{
				FilePath directory;
				bool keep = false;
				~Cleanup() { if (!keep) { std::error_code ignored; FileSystem::RemoveAll(directory, ignored); } }
			} cleanup{output};
			m_OwnedOutput = output;
			const auto script = ResolvePythonExampleScript("phonon_mode_vector.py");
			ScriptRunOptions options;
			options.scriptPath = script.scriptPath;
			options.workingDirectory = script.workingDirectory;
			options.arguments = {"--embedded-dir", m_EmbeddedDirectory.Utf8(), "--mode", std::to_string(m_Mode),
				"--out-dir", m_OutputDirectory.Utf8()};
			options.requireZeroExitCode = false;
			options.shouldCancel = [&context] { return context.IsCancellationRequested(); };
			const auto run = m_ScriptRunner.RunFile(options);
			context.ThrowIfCancellationRequested();
			if (!run)
			{
				m_Error = run.Error();
				throw std::runtime_error(m_Error->userMessage + "\n" + m_Error->technicalDetails);
			}
			std::istringstream stream(run->standardOutput);
			std::string line, jsonLine;
			while (std::getline(stream, line))
				if (const auto candidate = ExtractJsonLineFromOutput(line); !candidate.empty()) jsonLine = candidate;
			const auto payload = nlohmann::json::parse(jsonLine);
			if (!payload.at("ok").get<bool>())
				throw std::runtime_error(payload.at("error").get<std::string>() + "\n" + run->standardError);
			if (run->exitCode != 0) throw std::runtime_error("Mode vector script returned a nonzero exit code");
			context.ThrowIfCancellationRequested();
			m_Result = m_OutputDirectory;
			cleanup.keep = true;
		}
		catch (const JobCancelledException &) { throw; }
		catch (const std::exception &exception)
		{
			if (!m_Error) m_Error = MakePythonExecutionError("Nie udało się wczytać wektora modu.", exception.what(),
				"Sprawdź modes.h5 i POSCAR w katalogu embedded.", "python.phonon_mode_vector.failed");
			throw std::runtime_error(m_Error->userMessage + "\n" + m_Error->technicalDetails);
		}
		context.SetProgress(1.0f, 1.0f);
		context.SetMessage("Wektor modu wczytany");
	}
}
