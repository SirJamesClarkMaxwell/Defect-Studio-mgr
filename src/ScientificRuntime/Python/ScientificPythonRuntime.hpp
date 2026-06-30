#pragma once

#include <vector>

#include "Core/Diagnostics/StructuredError.hpp"
#include "Domain/Crystal/CrystalStructure.hpp"
#include "ScientificRuntime/Python/ASEBridge.hpp"
#include "ScientificRuntime/Python/PymatgenBridge.hpp"
#include "ScientificRuntime/Python/PythonInterpreter.hpp"
#include "ScientificRuntime/Python/ScriptRunner.hpp"

namespace DefectStudio
{
	class ScientificPythonRuntime final
	{
	public:
		[[nodiscard]] Result<void> Initialize();
		void Shutdown();

		[[nodiscard]] bool IsReady() const;
		[[nodiscard]] Result<ScriptRunResult> RunBridgeRoundtripDemo() const;

		[[nodiscard]] Result<CrystalStructure> LoadCrystalStructure(const Path &filePath) const;
		[[nodiscard]] Result<std::vector<CrystalStructure>> LoadCrystalStructures(const std::vector<Path> &filePaths) const;
		[[nodiscard]] Result<PymatgenRoundtripResult> RoundtripPoscar(const PymatgenRoundtripRequest &request) const;
		[[nodiscard]] Result<ASEConvertResult> ConvertWithASE(const ASEConvertRequest &request) const;

	private:
		PythonInterpreter m_Interpreter;
		ScriptRunner m_ScriptRunner;
		PymatgenBridge m_PymatgenBridge;
		ASEBridge m_ASEBridge;
		bool m_IsReady = false;
	};
} // namespace DefectStudio
