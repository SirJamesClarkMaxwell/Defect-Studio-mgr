// The four file-load routes (Browse, Project Tree selection, active viewport, drag and drop)
// and the pane-visibility / supercell controls. Split out of NewStructureWizardPanel.cpp.
#include "Core/dspch.hpp"

#include "Presentation/Panels/NewStructureWizardPanel.hpp"

#include <algorithm>
#include <cmath>

#include <glm/gtc/matrix_inverse.hpp>
#include <imgui.h>

#include "Core/JobSystem/JobSystem.hpp"
#include "ScientificRuntime/Python/OpenDefectJob.hpp"
#include "ScientificRuntime/Python/PymatgenConversion.hpp"
#include "Core/Platform/FileDialog.hpp"
#include "Domain/Crystal/BravaisLattice.hpp"
#include "Renderer/RendererWindowState.hpp"

namespace DefectStudio
{
	namespace
	{
			// Inverse of BuildLatticeCell, for a cell read from a file rather than typed in. Only the
			// six scalars are recoverable; which crystal system produced them is not, so callers pick
			// Triclinic and leave every field free.
			[[nodiscard]] LatticeParameters DeriveLatticeParameters(const glm::mat3 &vectors)
			{
				const auto angleDegrees = [](const glm::vec3 &lhs, const glm::vec3 &rhs) {
					const float lengths = glm::length(lhs) * glm::length(rhs);
					if (lengths < 1e-8f)
						return 90.0f;
					return glm::degrees(std::acos(std::clamp(glm::dot(lhs, rhs) / lengths, -1.0f, 1.0f)));
				};

				LatticeParameters params;
				params.a = glm::length(vectors[0]);
				params.b = glm::length(vectors[1]);
				params.c = glm::length(vectors[2]);
				params.alphaDegrees = angleDegrees(vectors[1], vectors[2]);
				params.betaDegrees = angleDegrees(vectors[0], vectors[2]);
				params.gammaDegrees = angleDegrees(vectors[0], vectors[1]);
				return params;
			}
	} // namespace

	void NewStructureWizardPanel::drawFileLoadSection()
	{
		ImGui::TextUnformatted(
			m_Mode == CreationMode::ImportFile
				? "Import a structure file (POSCAR, .vasp, CIF):"
				: "Load an existing structure to inspect and edit:");

		const float buttonRowWidth =
			ImGui::CalcTextSize("Browse").x + ImGui::CalcTextSize("Load").x + ImGui::GetStyle().FramePadding.x * 4.0f
			+ ImGui::GetStyle().ItemSpacing.x * 2.0f;
		ImGui::SetNextItemWidth(-buttonRowWidth);
		ImGui::InputText("##load_file_path", m_LoadFilePathBuffer.data(), m_LoadFilePathBuffer.size());
		ImGui::SameLine();

		const bool loading = m_PendingLoadJob != nullptr;
		ImGui::BeginDisabled(loading);
		if (ImGui::Button("Browse"))
			browseForStructureFile();
		ImGui::EndDisabled();
		ImGui::SameLine();

		ImGui::BeginDisabled(loading || m_LoadFilePathBuffer[0] == 0);
		if (ImGui::Button("Load"))
			dispatchFileLoad();
		ImGui::EndDisabled();

		const bool treeSelectionUsable = !m_ProjectTreeSelection.Empty();
		ImGui::BeginDisabled(loading || !treeSelectionUsable);
		if (ImGui::Button("Use Project Tree selection"))
			loadStructureFile(m_ProjectTreeSelection.string());
		ImGui::EndDisabled();
		if (!treeSelectionUsable && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
			ImGui::SetTooltip("Select a file in the Project Tree first");

		ImGui::SameLine();
		const bool viewportUsable = !m_RendererLayer.GetFocusedViewportWindowId().empty();
		ImGui::BeginDisabled(loading || !viewportUsable);
		if (ImGui::Button("Use active viewport"))
			adoptFocusedViewportStructure();
		ImGui::EndDisabled();

		ImGui::TextDisabled("...or drop a file from the Project Tree onto this text.");
		// Reuses the Project Tree drag payload that already exists rather than adding a second one:
		// a file-specific payload would have to OVERWRITE this one (ImGui keeps a single payload per
		// source, see the WAVECAR case in ProjectTreePanel), which would cost drag-to-move for every
		// structure file. The payload is a newline-joined multi-selection; the first path wins.
		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("DS_TREE_ENTRY_PATHS"))
			{
				if (payload->DataSize > 0)
				{
					const std::string joined(
						static_cast<const char *>(payload->Data), static_cast<std::size_t>(payload->DataSize) - 1);
					loadStructureFile(joined.substr(0, joined.find('\n')));
				}
			}
			ImGui::EndDragDropTarget();
		}

		if (loading)
			ImGui::TextDisabled("Loading...");
		else if (!m_LoadError.empty())
			ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", m_LoadError.c_str());
		else if (!m_LoadStatus.empty())
			ImGui::TextDisabled("%s", m_LoadStatus.c_str());
	}

	void NewStructureWizardPanel::browseForStructureFile()
	{
		// NFD adds its own "All files" entry, which is what covers extension-less POSCAR/CONTCAR.
		Result<std::optional<Path>> picked =
			Platform::PickOpenFile(Path{}, "Structure files", "vasp,cif,POSCAR,CONTCAR");
		if (!picked)
		{
			m_LoadError = picked.Error().userMessage;
			return;
		}
		if (!picked.Value().has_value())
			return; // cancelled

		loadStructureFile(picked.Value()->string());
	}

	void NewStructureWizardPanel::loadStructureFile(const std::string &path)
	{
		if (path.empty())
			return;
		const std::size_t limit = m_LoadFilePathBuffer.size() - 1;
		const std::size_t length = std::min(path.size(), limit);
		std::copy_n(path.begin(), length, m_LoadFilePathBuffer.begin());
		m_LoadFilePathBuffer[length] = 0;
		dispatchFileLoad();
	}

	void NewStructureWizardPanel::SetProjectTreeSelection(Path selectedFile)
	{
		m_ProjectTreeSelection = std::move(selectedFile);
	}

	void NewStructureWizardPanel::adoptFocusedViewportStructure()
	{
		const std::string windowId = m_RendererLayer.GetFocusedViewportWindowId();
		if (windowId.empty())
			return;

		std::vector<RendererWindowState> &windows = m_RendererLayer.GetWindows();
		const auto it = std::find_if(windows.begin(), windows.end(), [&](const RendererWindowState &window) {
			return window.windowId == windowId;
		});
		if (it == windows.end() || it->structure.atoms.empty())
			return;

		// Rebuilt from the renderer snapshot rather than re-read from disk: the viewport already
		// holds Cartesian positions and the lattice, and the fractional coordinates follow.
		CrystalStructure structure;
		structure.name = it->title;
		const glm::mat3 lattice = it->structure.lattice;
		const bool invertible = std::abs(glm::determinant(lattice)) > 1e-6f;
		const glm::mat3 inverseLattice = invertible ? glm::inverse(lattice) : glm::mat3(1.0f);
		for (int column = 0; column < 3; ++column)
			structure.cell.vectors[static_cast<std::size_t>(column)] = lattice[column];

		int index = 0;
		for (const auto &atom : it->structure.atoms)
		{
			AtomSite site;
			site.species = atom.element;
			site.position = atom.cartesianPosition;
			site.fractional = inverseLattice * atom.cartesianPosition;
			site.index = index++;
			structure.atoms.push_back(std::move(site));
		}
		adoptLoadedStructure(structure);
		m_LoadStatus = "Copied " + std::to_string(structure.atoms.size()) + " atoms from the active viewport";
		m_LoadError.clear();
	}

	void NewStructureWizardPanel::dispatchFileLoad()
	{
		Ref<JobSystem> jobSystem = m_JobSystem.lock();
		if (jobSystem == nullptr)
		{
			m_LoadError = "JobSystem unavailable";
			return;
		}

		const std::string filePath(m_LoadFilePathBuffer.data());
		if (filePath.empty())
			return;

		// Parsing goes through a Python subprocess - never inline in Render(), which would stall the
		// frame for the whole cold-import cost.
		m_PendingLoadJob = CreateRef<OpenDefectJob>(Path(filePath), filePath);
		m_PendingLoadJobId = jobSystem->Submit(m_PendingLoadJob, JobPriority::Normal);
		m_LoadError.clear();
		m_LoadStatus.clear();
	}

	void NewStructureWizardPanel::pollFileLoadJob()
	{
		if (m_PendingLoadJob == nullptr)
			return;

		Ref<JobSystem> jobSystem = m_JobSystem.lock();
		if (jobSystem == nullptr)
			return;

		const std::optional<JobSnapshot> snapshot = jobSystem->GetJob(m_PendingLoadJobId);
		if (!snapshot.has_value() || snapshot->status == JobStatus::Queued || snapshot->status == JobStatus::Running)
			return;

		if (snapshot->status == JobStatus::Completed && m_PendingLoadJob->GetResult().has_value())
		{
			const CrystalStructure loaded = ConvertPymatgenStructureToCrystalStructure(
				*m_PendingLoadJob->GetResult(), m_PendingLoadJob->GetDisplayName());
			adoptLoadedStructure(loaded);
			m_LoadStatus = "Loaded " + std::to_string(loaded.atoms.size()) + " atoms";
			m_LoadError.clear();
		}
		else
		{
			m_LoadError = snapshot.has_value() && !snapshot->errorMessage.empty()
				? snapshot->errorMessage
				: "Structure load failed";
		}

		m_PendingLoadJob.reset();
		m_PendingLoadJobId = 0;
	}

	void NewStructureWizardPanel::adoptLoadedStructure(const CrystalStructure &structure)
	{
		// Triclinic on purpose: a loaded cell has no declared crystal system, and any more specific
		// choice would lock fields BuildLatticeCell then re-derives, silently discarding the real
		// lattice that was just read from the file.
		m_System = CrystalSystem::Triclinic;
		m_Params = DeriveLatticeParameters(structure.cell.ToMatrix());
		// P has the single identity translation, so lattice (x) basis reproduces the file exactly.
		// Recovering a smaller motif would be symmetry detection (spglib), not arithmetic.
		m_Centering = BravaisCenteringPreset::Primitive;

		m_BasisRows.clear();
		for (const AtomSite &atom : structure.atoms)
			m_BasisRows.push_back(BasisRow{atom.species, atom.fractional});

		if (!structure.name.empty())
		{
			const std::size_t limit = m_StructureNameBuffer.size() - 1;
			const std::size_t length = std::min(structure.name.size(), limit);
			std::copy_n(structure.name.begin(), length, m_StructureNameBuffer.begin());
			m_StructureNameBuffer[length] = 0;
		}
	}

	void NewStructureWizardPanel::drawPreviewControls()
	{
		ImGui::TextUnformatted("Views:");
		ImGui::SameLine();
		ImGui::Checkbox("Basis##view_basis", &m_ViewVisible[0]);
		ImGui::SameLine();
		ImGui::Checkbox("Unit cell##view_unit_cell", &m_ViewVisible[1]);
		ImGui::SameLine();
		ImGui::Checkbox("Supercell##view_supercell", &m_ViewVisible[2]);

		// The centering is always known now, so this is greyed out only for P, where the primitive
		// cell IS the conventional one.
		const bool primitiveAvailable = m_Centering != BravaisCenteringPreset::Primitive;
		ImGui::BeginDisabled(!primitiveAvailable);
		ImGui::Checkbox("Show primitive cell", &m_ShowPrimitiveCell);
		ImGui::EndDisabled();
		if (!primitiveAvailable && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
			ImGui::SetTooltip("A primitive lattice already IS its primitive cell");

		ImGui::TextUnformatted("Supercell");
		ImGui::SetNextItemWidth(180.0f);
		if (ImGui::InputInt3("n x k x l##supercell_counts", &m_SupercellCounts.x))
			m_SupercellCounts = glm::max(m_SupercellCounts, glm::ivec3(1));

		const int cells = m_SupercellCounts.x * m_SupercellCounts.y * m_SupercellCounts.z;
		if (cells > 1)
		{
			const int unitCellAtoms = static_cast<int>(m_BasisRows.size())
				* static_cast<int>(GetCenteringTranslations(m_Centering).size());
			ImGui::SameLine();
			ImGui::TextDisabled("%d atoms in the supercell view", unitCellAtoms * cells);
		}
	}
} // namespace DefectStudio
