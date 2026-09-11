// prototypes.yaml / materials.yaml: loading the catalog and turning a chosen prototype back
// into centering x motif. Split out of NewStructureWizardPanel.cpp.
#include "Core/dspch.hpp"

#include "Presentation/Panels/NewStructureWizardPanel.hpp"

#include <algorithm>
#include <cstdio>

#include <glm/common.hpp>

#include "Core/Logging/Logger.hpp"
#include "Domain/Crystal/PrimitiveCell.hpp"

namespace DefectStudio
{
	namespace
	{
			[[nodiscard]] bool SamePosition(const glm::vec3 &lhs, const glm::vec3 &rhs)
			{
				const glm::vec3 difference = glm::abs(lhs - rhs);
				const auto axisMatches = [](float value) { return value < 1e-3f || value > 1.0f - 1e-3f; };
				return axisMatches(difference.x) && axisMatches(difference.y) && axisMatches(difference.z);
			}

			// Splits a site listed in CONVENTIONAL-cell coordinates back into the motif positions that,
			// repeated at every centering translation, reproduce exactly that list. Greedy: take the
			// first uncovered position as a motif representative, mark it and its translated copies
			// covered, repeat. Returns empty when the positions do not factor - prototypes.yaml is
			// hand-written, and a partial factorisation would silently drop atoms.
			[[nodiscard]] std::vector<glm::vec3> FactorSitePositions(
				const std::vector<glm::vec3> &positions, BravaisCenteringPreset centering)
			{
				const std::vector<glm::vec3> translations = GetCenteringTranslations(centering);
				if (positions.empty() || positions.size() % translations.size() != 0)
					return {};

				std::vector<bool> covered(positions.size(), false);
				std::vector<glm::vec3> motif;
				for (std::size_t i = 0; i < positions.size(); ++i)
				{
					if (covered[i])
						continue;
					motif.push_back(positions[i]);
					for (const glm::vec3 &translation : translations)
					{
						const glm::vec3 target = glm::fract(positions[i] + translation);
						const auto match = std::find_if(
							positions.begin(), positions.end(), [&](const glm::vec3 &candidate) {
								return SamePosition(glm::fract(candidate), target);
							});
						if (match == positions.end())
							return {};
						covered[static_cast<std::size_t>(std::distance(positions.begin(), match))] = true;
					}
				}
				return motif.size() * translations.size() == positions.size() ? motif : std::vector<glm::vec3>{};
			}

			// prototypes.yaml spells the system out; CrystalSystem is what BuildLatticeCell takes.
			std::optional<CrystalSystem> parseCrystalSystem(const std::string &name)
			{
				static const std::array<std::pair<const char *, CrystalSystem>, 7> kNames = {
					std::pair{"Cubic", CrystalSystem::Cubic},
					std::pair{"Tetragonal", CrystalSystem::Tetragonal},
					std::pair{"Orthorhombic", CrystalSystem::Orthorhombic},
					std::pair{"Hexagonal", CrystalSystem::Hexagonal},
					std::pair{"Trigonal", CrystalSystem::Trigonal},
					std::pair{"Monoclinic", CrystalSystem::Monoclinic},
					std::pair{"Triclinic", CrystalSystem::Triclinic}};
				for (const auto &[label, system] : kNames)
					if (name == label)
						return system;
				return std::nullopt;
			}
	} // namespace

	void NewStructureWizardPanel::ensureCatalogLoaded()
	{
		if (m_CatalogLoaded)
			return;
		m_CatalogLoaded = true;

		Result<PrototypesAndMaterials> loaded = PrototypeLoader::LoadBuiltIn();
		if (!loaded)
		{
			m_CatalogError = loaded.Error().userMessage;
			return;
		}
		m_Catalog = std::move(loaded).Value();
		if (!m_Catalog.prototypes.empty())
			m_SelectedPrototypeIndex = 0;
		m_SiteSpecies.clear();
	}

	const PrototypeDefinition *NewStructureWizardPanel::selectedPrototype() const
	{
		if (m_SelectedPrototypeIndex < 0 || m_SelectedPrototypeIndex >= static_cast<int>(m_Catalog.prototypes.size()))
			return nullptr;
		return &m_Catalog.prototypes[static_cast<std::size_t>(m_SelectedPrototypeIndex)];
	}

	void NewStructureWizardPanel::applyPrototypeToBasis()
	{
		const PrototypeDefinition *prototype = selectedPrototype();
		if (prototype == nullptr)
			return;

		m_SiteSpecies.resize(prototype->sites.size());

		if (const std::optional<CrystalSystem> system = parseCrystalSystem(prototype->crystalSystem))
			m_System = *system;
		const BravaisCenteringPreset centering =
			ParseCenteringName(prototype->centering).value_or(BravaisCenteringPreset::Primitive);

		// prototypes.yaml lists every CONVENTIONAL-cell position of a site, but the basis table now
		// holds the motif only - so each site has to be factored back into centering x motif, or the
		// expansion below would multiply the positions a second time (diamond would come out at 32
		// atoms instead of 8).
		m_BasisRows.clear();
		for (std::size_t siteIndex = 0; siteIndex < prototype->sites.size(); ++siteIndex)
		{
			const SiteDefinition &site = prototype->sites[siteIndex];
			const std::vector<glm::vec3> motif = FactorSitePositions(site.positions, centering);
			if (motif.empty())
			{
				// Does not factor cleanly - fall back to P plus every position, which is still
				// correct, just not minimal.
				DS_LOG_WARN(
					"New Structure: prototype {} site {} does not factor into {} centering; falling back to P",
					prototype->name, siteIndex, prototype->centering);
				m_Centering = BravaisCenteringPreset::Primitive;
				m_BasisRows.clear();
				for (const SiteDefinition &fallbackSite : prototype->sites)
				{
					const std::size_t fallbackIndex =
						static_cast<std::size_t>(&fallbackSite - prototype->sites.data());
					for (const glm::vec3 &fractional : fallbackSite.positions)
						m_BasisRows.push_back(BasisRow{m_SiteSpecies[fallbackIndex], fractional});
				}
				return;
			}
			for (const glm::vec3 &fractional : motif)
				m_BasisRows.push_back(BasisRow{m_SiteSpecies[siteIndex], fractional});
		}
		m_Centering = centering;
	}

	void NewStructureWizardPanel::applySelectedMaterial()
	{
		if (m_SelectedMaterialIndex < 0)
			return;
		const MaterialDefinition &material = m_Catalog.materials[static_cast<std::size_t>(m_SelectedMaterialIndex)];

		const auto polytypeIt = material.polytypes.find(m_SelectedPolytype);
		if (polytypeIt == material.polytypes.end())
			return;

		const auto prototypeIt = std::find_if(
			m_Catalog.prototypes.begin(),
			m_Catalog.prototypes.end(),
			[&](const PrototypeDefinition &prototype) { return prototype.name == polytypeIt->second; });
		if (prototypeIt == m_Catalog.prototypes.end())
		{
			m_CatalogError = "materials.yaml references unknown prototype: " + polytypeIt->second;
			return;
		}
		m_SelectedPrototypeIndex = static_cast<int>(std::distance(m_Catalog.prototypes.begin(), prototypeIt));

		// The lattice constant is the whole reason to pick a material rather than a bare prototype.
		// Without it the wizard built every structure at the LatticeParameters default of a = 1 A,
		// which is below a single covalent radius - hence the ball of overlapping spheres.
		const auto constantsIt = material.constants.find(m_SelectedFunctional);
		if (constantsIt != material.constants.end())
		{
			if (constantsIt->second.a > 0.0f)
				m_Params.a = constantsIt->second.a;
			if (constantsIt->second.c > 0.0f)
				m_Params.c = constantsIt->second.c;
		}

		const auto mappingIt = material.defaultSiteMapping.find(m_SelectedPolytype);
		m_SiteSpecies = mappingIt != material.defaultSiteMapping.end()
			? mappingIt->second
			: std::vector<std::string>(prototypeIt->sites.size());
		m_SiteSpecies.resize(prototypeIt->sites.size());

		std::snprintf(m_StructureNameBuffer.data(), m_StructureNameBuffer.size(), "%s", material.name.c_str());
		m_FormulaBuffer[0] = '\0';
		applyPrototypeToBasis();
	}
} // namespace DefectStudio
