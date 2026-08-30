#pragma once

#include <string>
#include <vector>

#include "Core/Diagnostics/StructuredError.hpp"
#include "Core/Utils/Path.hpp"
#include "Domain/Crystal/CrystalStructure.hpp"
#include "ScientificRuntime/Python/ScriptRunner.hpp"

namespace DefectStudio
{
	struct MaterialLibraryEntry
	{
		std::string id; // ase.db row id, as string
		std::string name;
		std::string reducedFormula;
		std::string notes;
	};

	// Thin subprocess wrapper over ase.db (SQLite-backed) - one library file per scope (project vs
	// personal), same class for both, only libraryPath differs. No in-memory ProjectWorkspace
	// registry: panels call this directly, the way ProjectTreePanel talks to the filesystem
	// directly instead of through a cached Domain registry - this collection IS the persistence,
	// not a cache of it.
	class MaterialLibraryIO
	{
	public:
		explicit MaterialLibraryIO(Path libraryPath);

		[[nodiscard]] Result<MaterialLibraryEntry> AddMaterial(
			const CrystalStructure &structure,
			const std::string &name,
			const std::string &notes) const;
		[[nodiscard]] Result<std::vector<MaterialLibraryEntry>> ListMaterials() const;
		[[nodiscard]] Result<CrystalStructure> LoadMaterial(const std::string &entryId) const;
		[[nodiscard]] Result<void> RemoveMaterial(const std::string &entryId) const;

	private:
		Path m_LibraryPath;
		ScriptRunner m_ScriptRunner;
	};
} // namespace DefectStudio
