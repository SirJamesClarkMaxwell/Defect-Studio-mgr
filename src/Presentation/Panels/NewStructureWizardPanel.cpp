#include "Core/dspch.hpp"

#include "Presentation/Panels/NewStructureWizardPanel.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <optional>

#include <glm/gtc/epsilon.hpp>
#include <utility>

#include <imgui.h>

#include "Core/JobSystem/JobSystem.hpp"
#include "Domain/Crystal/FormulaParser.hpp"
#include "Domain/Crystal/PrototypeMatcher.hpp"
#include "Domain/DomainLayer.hpp"
#include "Presentation/Panels/PeriodicTableGrid.hpp"
#include "Domain/Crystal/PrimitiveCell.hpp"
#include "Core/Logging/Logger.hpp"
#include "Core/Platform/FileDialog.hpp"
#include "Domain/Crystal/LatticeBasisExpansion.hpp"
#include "Domain/Crystal/Supercell.hpp"
#include "Renderer/CrystalStructurePreviewWindow.hpp"
#include "Renderer/RendererWindowState.hpp"
#include "Renderer/OpenCrystalStructureAsWindow.hpp"
#include "Core/Domain/StructureLifecycleEvents.hpp"
#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "ScientificRuntime/Python/GetSymmetryInfoJob.hpp"
#include "ScientificRuntime/Python/OpenDefectJob.hpp"
#include "ScientificRuntime/Python/PymatgenConversion.hpp"

namespace DefectStudio
{
	namespace
	{
		// Free (unlocked) angle inputs feed BuildLatticeCell's sin(gamma) division for the c-vector's
		// y-component - a value near 0/180 deg blows that up to Inf/NaN (Task 1 code-review finding).
		// Clamped here, at the point raw user typing lands, so it never reaches BuildLatticeCell
		// unclamped.
		constexpr float kMinAngleDegrees = 1.0f;
		constexpr float kMaxAngleDegrees = 179.0f;
		constexpr float kMinLength = 0.01f;

	} // namespace

	NewStructureWizardPanel::NewStructureWizardPanel(
		RendererLayer &rendererLayer,
		WeakRef<DomainLayer> domainLayer,
		WeakRef<JobSystem> jobSystem,
		ElementPropertiesTable elementPropertiesTable,
		AtomStyleTable atomStyleTable,
		Ref<CreationSessionRegistry> sessionRegistry,
		Ref<EventBus> eventBus,
		std::string title,
		bool visibleByDefault)
		: IPanel(std::move(title), visibleByDefault),
		  m_RendererLayer(rendererLayer),
		  m_DomainLayer(std::move(domainLayer)),
		  m_JobSystem(std::move(jobSystem)),
		  m_ElementPropertiesTable(std::move(elementPropertiesTable)),
		  m_AtomStyleTable(std::move(atomStyleTable)),
		  m_SessionRegistry(std::move(sessionRegistry)),
		  m_EventBus(std::move(eventBus))
	{
		std::snprintf(m_StructureNameBuffer.data(), m_StructureNameBuffer.size(), "New Structure");
	}

	Ref<IPanel> NewStructureWizardPanel::Clone() const
	{
		return CreateRef<NewStructureWizardPanel>(*this);
	}

	LatticeParameters NewStructureWizardPanel::computeEffectiveParameters() const
	{
		// Mirrors BuildLatticeCell's own per-system derivation (BravaisLattice.cpp) in degrees, so the
		// UI can show a locked field's live value without duplicating BuildLatticeCell's radians/trig
		// path or risking disagreement with what it actually builds.
		LatticeParameters effective = m_Params;
		switch (m_System)
		{
			case CrystalSystem::Cubic:
				effective.b = effective.c = effective.a;
				effective.alphaDegrees = effective.betaDegrees = effective.gammaDegrees = 90.0f;
				break;
			case CrystalSystem::Tetragonal:
				effective.b = effective.a;
				effective.alphaDegrees = effective.betaDegrees = effective.gammaDegrees = 90.0f;
				break;
			case CrystalSystem::Orthorhombic:
				effective.alphaDegrees = effective.betaDegrees = effective.gammaDegrees = 90.0f;
				break;
			case CrystalSystem::Hexagonal:
				effective.b = effective.a;
				effective.alphaDegrees = effective.betaDegrees = 90.0f;
				effective.gammaDegrees = 120.0f; // NOT constraints.lockedAngleDegrees - see BravaisLattice.hpp caveat
				break;
			case CrystalSystem::Trigonal:
				effective.b = effective.c = effective.a;
				effective.betaDegrees = effective.gammaDegrees = effective.alphaDegrees;
				break;
			case CrystalSystem::Monoclinic:
				effective.alphaDegrees = effective.gammaDegrees = 90.0f;
				break;
			case CrystalSystem::Triclinic:
				break;
		}
		return effective;
	}

	CrystalStructure NewStructureWizardPanel::buildStructure() const
	{
		CrystalStructure structure;
		structure.name = m_StructureNameBuffer.data();
		structure.cell = BuildLatticeCell(m_System, m_Params);
		// Stamped on every rebuild because this function makes a fresh structure each frame. The
		// draft is the only thing the preview panes bond, and RegenerateAutoBonds reads the cutoffs
		// off the structure itself.
		structure.bondSettings = m_BondSettings;
		const glm::mat3 latticeMatrix = structure.cell.ToMatrix();

		// `lattice (x) basis`: the rows are the motif attached to ONE lattice point, and the
		// centering says where the others are. ExpandBasisOverLattice fills everything but the
		// Cartesian position, which needs the lattice matrix this function just built.
		structure.atoms = ExpandBasisOverLattice(motifAtoms(), m_Centering);
		// Per-site overrides typed in the Generated atoms table, applied after the convolution
		// because that is the only place the site indices exist. They change WHAT sits on a site,
		// never where it sits, so the cell and the coordinates stay the centering's.
		for (std::size_t i = 0; i < structure.atoms.size() && i < m_SpeciesOverrides.size(); ++i)
		{
			if (!m_SpeciesOverrides[i].empty())
				structure.atoms[i].species = m_SpeciesOverrides[i];
		}
		for (AtomSite &atom : structure.atoms)
			atom.position = latticeMatrix * atom.fractional;
		return structure;
	}

	std::vector<AtomSite> NewStructureWizardPanel::motifAtoms() const
	{
		std::vector<AtomSite> motif;
		motif.reserve(m_BasisRows.size());
		int index = 0;
		for (const BasisRow &row : m_BasisRows)
		{
			AtomSite atom;
			atom.species = row.species;
			atom.fractional = row.fractional;
			atom.index = index++;
			motif.push_back(std::move(atom));
		}
		return motif;
	}

	CrystalStructure NewStructureWizardPanel::buildMotifStructure() const
	{
		CrystalStructure structure;
		structure.name = std::string(m_StructureNameBuffer.data()) + " (basis)";
		structure.cell = BuildLatticeCell(m_System, m_Params);
		structure.bondSettings = m_BondSettings;
		const glm::mat3 latticeMatrix = structure.cell.ToMatrix();
		structure.atoms = motifAtoms();
		for (AtomSite &atom : structure.atoms)
			atom.position = latticeMatrix * atom.fractional;
		return structure;
	}

	std::optional<CrystalStructure> NewStructureWizardPanel::GetBuiltStructure() const
	{
		const std::string name(m_StructureNameBuffer.data());
		if (name.empty() || m_BasisRows.empty())
			return std::nullopt;

		// Validate that all basis rows have species assigned
		for (const BasisRow &row : m_BasisRows)
		{
			if (row.species.empty())
				return std::nullopt;
		}

		return buildStructure();
	}

	void NewStructureWizardPanel::SetPseudopotentialDirConfigured(bool configured)
	{
		m_PseudopotentialDirConfigured = configured;
		if (!configured)
			m_ExportPotcar = false;
	}

	void NewStructureWizardPanel::drawLatticeSection()
	{
		ImGui::InputText("Name", m_StructureNameBuffer.data(), m_StructureNameBuffer.size());

		static constexpr std::array<const char *, 7> kSystemNames = {
			"Cubic", "Tetragonal", "Orthorhombic", "Hexagonal", "Trigonal", "Monoclinic", "Triclinic"};
		int systemIndex = static_cast<int>(m_System);
		ImGui::SetNextItemWidth(200.0f);
		if (ImGui::Combo("Crystal system", &systemIndex, kSystemNames.data(), static_cast<int>(kSystemNames.size())))
			m_System = static_cast<CrystalSystem>(systemIndex);

		const LatticeFieldConstraints constraints = GetFieldConstraints(m_System);
		const LatticeParameters effective = computeEffectiveParameters();

		// Lengths and angles as two tables side by side, each with the label in a fixed left column.
		// One row of six fields wraps into nonsense the moment the panel is docked narrow, and the
		// labels ImGui puts to the RIGHT of a DragFloat read as if they belong to the next field.
		const float labelWidth = ImGui::CalcTextSize("gamma").x + ImGui::GetStyle().FramePadding.x * 2.0f;
		const auto drawParameterRow =
			[&](const char *label, float *value, bool locked, float lockedValue, float step, float minimum, float maximum, const char *format)
		{
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(label);
			ImGui::TableSetColumnIndex(1);
			ImGui::PushID(label);
			ImGui::BeginDisabled(locked);
			float shown = locked ? lockedValue : *value;
			ImGui::SetNextItemWidth(-1.0f);
			if (ImGui::DragFloat("##value", &shown, step, minimum, maximum, format) && !locked)
				*value = std::clamp(shown, minimum, maximum);
			ImGui::EndDisabled();
			ImGui::PopID();
			if (locked && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
				ImGui::SetTooltip("Derived from the free parameters by the crystal system");
		};

		constexpr ImGuiTableFlags kParameterTableFlags = ImGuiTableFlags_SizingStretchProp;
		const float halfWidth = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;

		if (ImGui::BeginTable("##lattice_lengths", 2, kParameterTableFlags, ImVec2(halfWidth, 0.0f)))
		{
			ImGui::TableSetupColumn("##label", ImGuiTableColumnFlags_WidthFixed, labelWidth);
			ImGui::TableSetupColumn("##value", ImGuiTableColumnFlags_WidthStretch);
			drawParameterRow("a", &m_Params.a, false, effective.a, 0.01f, kMinLength, 1000.0f, "%.4f A");
			drawParameterRow("b", &m_Params.b, constraints.bLocked, effective.b, 0.01f, kMinLength, 1000.0f, "%.4f A");
			drawParameterRow("c", &m_Params.c, constraints.cLocked, effective.c, 0.01f, kMinLength, 1000.0f, "%.4f A");
			ImGui::EndTable();
		}

		ImGui::SameLine();

		if (ImGui::BeginTable("##lattice_angles", 2, kParameterTableFlags, ImVec2(halfWidth, 0.0f)))
		{
			ImGui::TableSetupColumn("##label", ImGuiTableColumnFlags_WidthFixed, labelWidth);
			ImGui::TableSetupColumn("##value", ImGuiTableColumnFlags_WidthStretch);
			drawParameterRow(
				"alpha", &m_Params.alphaDegrees, constraints.alphaLocked, effective.alphaDegrees,
				0.1f, kMinAngleDegrees, kMaxAngleDegrees, "%.2f deg");
			drawParameterRow(
				"beta", &m_Params.betaDegrees, constraints.betaLocked, effective.betaDegrees,
				0.1f, kMinAngleDegrees, kMaxAngleDegrees, "%.2f deg");
			drawParameterRow(
				"gamma", &m_Params.gammaDegrees, constraints.gammaLocked, effective.gammaDegrees,
				0.1f, kMinAngleDegrees, kMaxAngleDegrees, "%.2f deg");
			ImGui::EndTable();
		}
	}

	void NewStructureWizardPanel::drawModeSelector()
	{
		struct ModeTabSpec
		{
			CreationMode mode;
			const char *label;
		};
		// One tab per CreationMode. Three of these existed before the Structure Hub took over
		// session management, and are back where the draft is actually edited.
		static constexpr std::array<ModeTabSpec, 4> kModeTabs = {
			ModeTabSpec{CreationMode::FromScratch, "Create New"},
			ModeTabSpec{CreationMode::FromTemplate, "From Library"},
			ModeTabSpec{CreationMode::ImportFile, "Import File"},
			ModeTabSpec{CreationMode::AnalyzeExisting, "Analyze Existing"}};

		if (!ImGui::BeginTabBar("##new_structure_modes"))
			return;

		for (const ModeTabSpec &spec : kModeTabs)
		{
			if (!ImGui::BeginTabItem(spec.label))
				continue;
			// Switching tabs only changes where the draft's contents come from. The draft itself,
			// and any session it has already been handed to, survive the switch.
			m_Mode = spec.mode;
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}

	void NewStructureWizardPanel::Render()
	{
		if (!IsVisible())
			return;

		ImGui::SetNextWindowSize(ImVec2(480.0f, 640.0f), ImGuiCond_FirstUseEver);
		bool windowOpen = true;
		if (!ImGui::Begin(GetTitle().c_str(), &windowOpen))
		{
			ImGui::End();
			SetVisible(windowOpen);
			return;
		}

		ensureCatalogLoaded();
		pollSymmetryJob();
		pollFileLoadJob();

		drawModeSelector();
		ImGui::Separator();

		switch (m_Mode)
		{
		case CreationMode::FromTemplate:
			drawMaterialSection();
			ImGui::Separator();
			drawFormulaAndMappingSection();
			ImGui::Separator();
			break;
		case CreationMode::ImportFile:
		case CreationMode::AnalyzeExisting:
			drawFileLoadSection();
			ImGui::Separator();
			break;
		case CreationMode::FromScratch:
			break;
		}

		// Lattice, centering and basis are shared by every mode: a library or imported structure is
		// still an editable draft, not a read-only import.
		drawLatticeSection();
		ImGui::Separator();
		drawCenteringPresetRow();
		ImGui::Separator();
		drawBasisTable();
		ImGui::Separator();
		drawGeneratedAtomsSection();
		ImGui::Separator();
		drawSymmetrySection();
		ImGui::Separator();
		drawBondSection();
		ImGui::Separator();

		drawPreviewControls();
		ImGui::Separator();

		// Offered only when there is somewhere to read pseudopotentials from: without the directory
		// the tick produced nothing but a pinned "POTCAR export failed" notification, and only after
		// the structure had already been added.
		ImGui::BeginDisabled(!m_PseudopotentialDirConfigured);
		ImGui::Checkbox("Export POTCAR##potcar_export", &m_ExportPotcar);
		ImGui::EndDisabled();
		ImGui::SameLine();
		if (m_PseudopotentialDirConfigured)
			ImGui::TextDisabled("(written next to the POSCAR)");
		else
			ImGui::TextDisabled("(set ui.pseudopotential_dir in Settings first)");

		ImGui::Separator();

		// A row with no species would reach the renderer as an atom of element "", so the hand-off
		// waits until every row has one rather than quietly inventing a default.
		const bool speciesMissing = std::any_of(
			m_BasisRows.begin(), m_BasisRows.end(), [](const BasisRow &row) { return row.species.empty(); });
		const bool registryAvailable = m_SessionRegistry != nullptr && m_EventBus != nullptr;

		ImGui::BeginDisabled(!registryAvailable || m_BasisRows.empty() || speciesMissing);
		if (ImGui::Button(m_SessionId.has_value() ? "Update Structure Hub" : "Move to Structure Hub"))
			moveToStructureHub();
		ImGui::EndDisabled();

		if (!registryAvailable)
			ImGui::TextDisabled("Creation session registry unavailable.");
		else if (speciesMissing)
			ImGui::TextDisabled("Every basis row needs a species before the structure can be handed over.");

		ImGui::End();

		// After the widgets, so an edit made this frame lands in the same frame rather than one
		// behind - and outside Begin/End, since it touches renderer windows, not this one.
		// The gizmo is read back first: a drag in progress owns the atom positions, and pushing the
		// fields over them would fight the mouse.
		// The panes are the draft viewport while it is edited, so the session exists as soon as the
		// draft is renderable - not as a reward for handing it to the Structure Hub.
		if (!m_BasisRows.empty() && !speciesMissing)
			ensureSession();

		if (!pullGizmoEditsFromPreview())
			syncDraftToSession();

		if (!windowOpen && m_SessionId.has_value() && m_EventBus != nullptr)
		{
			// Closing this panel is one of the three paths into the session close protocol; the
			// coordinator decides what happens to an attempt that is still running.
			DomainEvents::RendererTabClosed closedEvent;
			closedEvent.sessionId = *m_SessionId;
			m_EventBus->Publish(closedEvent);
			m_SessionId.reset();
		}
		SetVisible(windowOpen);
	}

} // namespace DefectStudio
