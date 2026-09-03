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
#include "Domain/Crystal/Supercell.hpp"
#include "Renderer/CrystalStructurePreviewWindow.hpp"
#include "Renderer/RendererWindowState.hpp"
#include "Renderer/OpenCrystalStructureAsWindow.hpp"
#include "ScientificRuntime/Python/GetSymmetryInfoJob.hpp"

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

		struct PresetButtonSpec
		{
			BravaisCenteringPreset preset;
			const char *label;
		};
		constexpr std::array<PresetButtonSpec, 4> kPresetButtons = {
			PresetButtonSpec{BravaisCenteringPreset::Primitive, "P"},
			PresetButtonSpec{BravaisCenteringPreset::BodyCentered, "I"},
			PresetButtonSpec{BravaisCenteringPreset::FaceCentered, "F"},
			PresetButtonSpec{BravaisCenteringPreset::BaseCentered, "C"}};

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

	NewStructureWizardPanel::NewStructureWizardPanel(
		RendererLayer &rendererLayer,
		WeakRef<DomainLayer> domainLayer,
		WeakRef<JobSystem> jobSystem,
		ElementPropertiesTable elementPropertiesTable,
		AtomStyleTable atomStyleTable,
		std::string title,
		bool visibleByDefault)
		: IPanel(std::move(title), visibleByDefault),
		  m_RendererLayer(rendererLayer),
		  m_DomainLayer(std::move(domainLayer)),
		  m_JobSystem(std::move(jobSystem)),
		  m_ElementPropertiesTable(std::move(elementPropertiesTable)),
		  m_AtomStyleTable(std::move(atomStyleTable))
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
		const glm::mat3 latticeMatrix = structure.cell.ToMatrix();

		structure.atoms.reserve(m_BasisRows.size());
		int index = 0;
		for (const BasisRow &row : m_BasisRows)
		{
			AtomSite atom;
			atom.species = row.species;
			atom.fractional = row.fractional;
			atom.position = latticeMatrix * row.fractional;
			atom.index = index++;
			structure.atoms.push_back(std::move(atom));
		}
		return structure;
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

	void NewStructureWizardPanel::drawCenteringPresetRow()
	{
		ImGui::TextUnformatted("Centering preset (replaces the basis table below):");
		for (std::size_t i = 0; i < kPresetButtons.size(); ++i)
		{
			if (i > 0)
				ImGui::SameLine();
			const PresetButtonSpec &spec = kPresetButtons[i];
			const bool supported = IsPresetSupportedFor(m_System, spec.preset);
			ImGui::BeginDisabled(!supported);
			if (ImGui::Button(spec.label))
			{
				m_Centering = spec.preset;
				m_BasisRows.clear();
				for (const glm::vec3 &fractional : GetCenteringPresetBasis(spec.preset))
					m_BasisRows.push_back(BasisRow{"X", fractional});
			}
			ImGui::EndDisabled();
		}
	}

	void NewStructureWizardPanel::drawElementPickerPopup(const char *popupId, std::string &targetSpecies)
	{
		if (!ImGui::BeginPopup(popupId))
			return;
		const std::string clicked = DrawPeriodicTableGrid(
			m_RendererLayer,
			[&](const std::string &symbol) -> glm::vec3
			{ return CategoryColor(ClassifyElement(AtomicNumberForSymbol(m_RendererLayer, symbol))); },
			targetSpecies);
		if (!clicked.empty())
		{
			targetSpecies = clicked;
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}

	void NewStructureWizardPanel::drawBasisTable()
	{
		ImGui::TextUnformatted("Atomic basis");
		ImGui::SameLine();
		ImGui::TextDisabled("(fractional, 0 to 1 inside the cell)");
		int rowToRemove = -1;
		if (ImGui::BeginTable(
				"##BasisRows", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
		{
			// One shared width for x/y/z: stretch columns give three different widths for three
			// fields that hold the same kind of number, which reads as if they were different fields.
			const float coordinateWidth = ImGui::CalcTextSize("-0.00000").x + ImGui::GetStyle().FramePadding.x * 4.0f;
			ImGui::TableSetupColumn("Element", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("(select)").x + 16.0f);
			ImGui::TableSetupColumn("x", ImGuiTableColumnFlags_WidthFixed, coordinateWidth);
			ImGui::TableSetupColumn("y", ImGuiTableColumnFlags_WidthFixed, coordinateWidth);
			ImGui::TableSetupColumn("z", ImGuiTableColumnFlags_WidthFixed, coordinateWidth);
			ImGui::TableSetupColumn("##Remove", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("Remove").x + 16.0f);
			ImGui::TableHeadersRow();

			for (std::size_t i = 0; i < m_BasisRows.size(); ++i)
			{
				BasisRow &row = m_BasisRows[i];
				ImGui::PushID(static_cast<int>(i));
				ImGui::TableNextRow();

				ImGui::TableSetColumnIndex(0);
				if (ImGui::Button(row.species.empty() ? "(select)" : row.species.c_str(), ImVec2(-1.0f, 0.0f)))
					ImGui::OpenPopup("##ElementPicker");
				drawElementPickerPopup("##ElementPicker", row.species);

				ImGui::TableSetColumnIndex(1);
				ImGui::SetNextItemWidth(-1.0f);
				ImGui::DragFloat("##X", &row.fractional.x, 0.005f, -1.0f, 2.0f, "%.4f");

				ImGui::TableSetColumnIndex(2);
				ImGui::SetNextItemWidth(-1.0f);
				ImGui::DragFloat("##Y", &row.fractional.y, 0.005f, -1.0f, 2.0f, "%.4f");

				ImGui::TableSetColumnIndex(3);
				ImGui::SetNextItemWidth(-1.0f);
				ImGui::DragFloat("##Z", &row.fractional.z, 0.005f, -1.0f, 2.0f, "%.4f");

				ImGui::TableSetColumnIndex(4);
				if (ImGui::Button("Remove"))
					rowToRemove = static_cast<int>(i);

				ImGui::PopID();
			}
			ImGui::EndTable();
		}
		if (rowToRemove >= 0)
			m_BasisRows.erase(m_BasisRows.begin() + rowToRemove);

		if (ImGui::Button("+ Add row"))
			m_BasisRows.push_back(BasisRow{});

		const CrystalStructure preview = buildStructure();
		const std::vector<std::string> uniqueSpecies = preview.UniqueSpecies();
		std::string formula;
		for (const std::string &symbol : uniqueSpecies)
		{
			if (!formula.empty())
				formula += " ";
			formula += symbol;
		}
		ImGui::TextDisabled(
			"%zu atom(s) - %s", m_BasisRows.size(), formula.empty() ? "(empty)" : formula.c_str());
	}

	void NewStructureWizardPanel::dispatchSymmetryCheck()
	{
		Ref<JobSystem> jobSystem = m_JobSystem.lock();
		if (jobSystem == nullptr)
		{
			m_SymmetryError = "JobSystem unavailable";
			return;
		}
		m_PendingSymmetryJob = CreateRef<GetSymmetryInfoJob>(buildStructure(), 0.01f);
		m_PendingSymmetryJobId = jobSystem->Submit(m_PendingSymmetryJob, JobPriority::Normal);
		m_SymmetryError.clear();
		m_SymmetryResult.reset();
	}

	void NewStructureWizardPanel::pollSymmetryJob()
	{
		if (m_PendingSymmetryJob == nullptr)
			return;

		Ref<JobSystem> jobSystem = m_JobSystem.lock();
		if (jobSystem == nullptr)
			return;

		const std::optional<JobSnapshot> snapshot = jobSystem->GetJob(m_PendingSymmetryJobId);
		if (!snapshot.has_value() || snapshot->status == JobStatus::Queued || snapshot->status == JobStatus::Running)
			return;

		if (snapshot->status == JobStatus::Completed)
		{
			m_SymmetryResult = m_PendingSymmetryJob->GetResult();
			if (!m_SymmetryResult.has_value())
				m_SymmetryError = "Symmetry check completed with no result";
			else
				m_SymmetryError.clear();
		}
		else
		{
			m_SymmetryError = snapshot->errorMessage.empty() ? "Symmetry check failed" : snapshot->errorMessage;
		}
		m_PendingSymmetryJob.reset();
		m_PendingSymmetryJobId = 0;
	}

	void NewStructureWizardPanel::drawSymmetrySection()
	{
		const bool loading = m_PendingSymmetryJob != nullptr;
		ImGui::BeginDisabled(loading || m_BasisRows.empty());
		if (ImGui::Button("Show symmetry"))
			dispatchSymmetryCheck();
		ImGui::EndDisabled();
		if (loading)
		{
			ImGui::SameLine();
			ImGui::TextDisabled("Checking...");
		}
		if (!m_SymmetryError.empty())
			ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", m_SymmetryError.c_str());

		if (m_SymmetryResult.has_value())
		{
			ImGui::Text("Spacegroup: %d (%s)", m_SymmetryResult->spacegroupNumber, m_SymmetryResult->spacegroupSymbol.c_str());
			ImGui::Text("Point group: %s", m_SymmetryResult->pointGroupSymbol.c_str());
			if (ImGui::TreeNode("Wyckoff positions"))
			{
				for (std::size_t i = 0; i < m_SymmetryResult->wyckoffLetters.size(); ++i)
				{
					const std::string species = i < m_BasisRows.size() ? m_BasisRows[i].species : "?";
					ImGui::Text("Atom %zu (%s): %s", i, species.c_str(), m_SymmetryResult->wyckoffLetters[i].c_str());
				}
				ImGui::TreePop();
			}
		}
	}

	std::size_t NewStructureWizardPanel::computePreviewSignature() const
	{
		// Hashing the inputs beats a hand-maintained dirty flag on each widget: a field added later
		// cannot be forgotten here, it simply changes the hash.
		std::size_t signature = std::hash<int>{}(static_cast<int>(m_System));
		const auto mix = [&signature](float value) {
			signature ^= std::hash<float>{}(value) + 0x9e3779b97f4a7c15ULL + (signature << 6) + (signature >> 2);
		};
		mix(m_Params.a);
		mix(m_Params.b);
		mix(m_Params.c);
		mix(m_Params.alphaDegrees);
		mix(m_Params.betaDegrees);
		mix(m_Params.gammaDegrees);
		for (const BasisRow &row : m_BasisRows)
		{
			signature ^= std::hash<std::string>{}(row.species) + 0x9e3779b97f4a7c15ULL + (signature << 6) + (signature >> 2);
			mix(row.fractional.x);
			mix(row.fractional.y);
			mix(row.fractional.z);
		}
		mix(static_cast<float>(m_SupercellCounts.x));
		mix(static_cast<float>(m_SupercellCounts.y));
		mix(static_cast<float>(m_SupercellCounts.z));
		mix(m_PreviewBasisOnly ? 1.0f : 0.0f);
		mix(m_ShowPrimitiveCell ? 1.0f : 0.0f);
		mix(static_cast<float>(m_Centering.has_value() ? static_cast<int>(*m_Centering) + 1 : 0));
		return signature;
	}

	bool NewStructureWizardPanel::pullGizmoEditsFromPreview()
	{
		if (m_PreviewWindowId.empty())
			return false;

		std::vector<RendererWindowState> &windows = m_RendererLayer.GetWindows();
		const auto it = std::find_if(windows.begin(), windows.end(), [&](const RendererWindowState &window) {
			return window.windowId == m_PreviewWindowId;
		});
		if (it == windows.end())
			return false;

		// The preview is built straight from m_BasisRows in order, and neither RegenerateAutoBonds
		// nor BuildRendererStructureData reorders atoms, so index i is row i. A mismatch means the
		// window is showing something else entirely - bail rather than write coordinates into the
		// wrong rows.
		if (it->structure.atoms.size() != m_BasisRows.size())
			return false;

		const glm::mat3 lattice = BuildLatticeCell(m_System, m_Params).ToMatrix();
		if (std::abs(glm::determinant(lattice)) < 1e-6f)
			return false;
		const glm::mat3 inverseLattice = glm::inverse(lattice);

		bool changed = false;
		for (std::size_t i = 0; i < m_BasisRows.size(); ++i)
		{
			const glm::vec3 fractional = inverseLattice * it->structure.atoms[i].cartesianPosition;
			if (glm::all(glm::epsilonEqual(fractional, m_BasisRows[i].fractional, 1e-5f)))
				continue;
			m_BasisRows[i].fractional = fractional;
			changed = true;
		}

		if (changed)
		{
			// Adopt the new state as the preview's own signature: the window already shows exactly
			// these positions, so rebuilding it would be pure work, and rebuilding mid-drag would
			// yank the atoms out from under the cursor.
			m_PreviewSignature = computePreviewSignature();
		}
		return it->gizmoDragActive;
	}

	void NewStructureWizardPanel::refreshPreview()
	{
		if (!m_LivePreview || m_BasisRows.empty())
			return;

		const std::size_t signature = computePreviewSignature();
		if (signature == m_PreviewSignature && !m_PreviewWindowId.empty())
			return;
		m_PreviewSignature = signature;

		const CrystalStructure unitCell = buildStructure();

		std::optional<glm::mat3> primitiveOverlay;
		if (m_ShowPrimitiveCell && m_Centering.has_value())
			primitiveOverlay = PrimitiveCellVectors(unitCell.cell.ToMatrix(), *m_Centering);

		m_PreviewWindowId = ShowCrystalStructurePreview(
			m_PreviewWindowId,
			unitCell,
			std::string(m_StructureNameBuffer.data()) + " (preview)",
			m_RendererLayer,
			m_ElementPropertiesTable,
			m_AtomStyleTable,
			/*showCellBox=*/!m_PreviewBasisOnly,
			/*showGrid=*/!m_PreviewBasisOnly,
			primitiveOverlay);

		const bool wantsSupercell = m_SupercellCounts.x > 1 || m_SupercellCounts.y > 1 || m_SupercellCounts.z > 1;
		if (!wantsSupercell)
		{
			CloseCrystalStructurePreview(m_SupercellPreviewWindowId, m_RendererLayer);
			m_SupercellPreviewWindowId.clear();
			return;
		}

		Result<CrystalStructure> supercell = BuildSupercell(
			unitCell, SupercellMatrix::Diagonal(m_SupercellCounts.x, m_SupercellCounts.y, m_SupercellCounts.z));
		if (!supercell)
			return;

		m_SupercellPreviewWindowId = ShowCrystalStructurePreview(
			m_SupercellPreviewWindowId,
			std::move(supercell).Value(),
			std::string(m_StructureNameBuffer.data()) + " (supercell preview)",
			m_RendererLayer,
			m_ElementPropertiesTable,
			m_AtomStyleTable,
			/*showCellBox=*/!m_PreviewBasisOnly,
			/*showGrid=*/!m_PreviewBasisOnly);
	}

	void NewStructureWizardPanel::closePreviews()
	{
		CloseCrystalStructurePreview(m_PreviewWindowId, m_RendererLayer);
		CloseCrystalStructurePreview(m_SupercellPreviewWindowId, m_RendererLayer);
		m_PreviewWindowId.clear();
		m_SupercellPreviewWindowId.clear();
		m_PreviewSignature = 0;
	}

	void NewStructureWizardPanel::drawPreviewControls()
	{
		if (ImGui::Checkbox("Live preview", &m_LivePreview) && !m_LivePreview)
			closePreviews();
		ImGui::SameLine();
		// A checkbox on the one preview window, not a second button opening a second window. That
		// button was the direct cause of the pile of duplicate tabs: its only difference from Create
		// was the cell-box and grid flags.
		ImGui::Checkbox("Basis only (no cell box)", &m_PreviewBasisOnly);

		// Greyed out when the centering is unknown: for a hand-edited basis the primitive cell is
		// something spglib has to READ BACK from the atoms, not a formula - and drawing the wrong
		// smaller box is worse than drawing none.
		const bool primitiveAvailable =
			m_Centering.has_value() && *m_Centering != BravaisCenteringPreset::Primitive;
		ImGui::BeginDisabled(!primitiveAvailable);
		ImGui::Checkbox("Show primitive cell", &m_ShowPrimitiveCell);
		ImGui::EndDisabled();
		if (!primitiveAvailable && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
		{
			ImGui::SetTooltip(
				m_Centering.has_value()
					? "A primitive lattice already IS its primitive cell"
					: "Pick a prototype or a centering preset first - the primitive cell follows from the centering");
		}

		ImGui::TextUnformatted("Supercell preview");
		ImGui::SetNextItemWidth(180.0f);
		if (ImGui::InputInt3("n x k x l##supercell_counts", &m_SupercellCounts.x))
			m_SupercellCounts = glm::max(m_SupercellCounts, glm::ivec3(1));

		const int cells = m_SupercellCounts.x * m_SupercellCounts.y * m_SupercellCounts.z;
		if (cells > 1)
		{
			ImGui::SameLine();
			ImGui::TextDisabled("%d atoms in a second window", static_cast<int>(m_BasisRows.size()) * cells);
		}
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

		drawMaterialSection();
		ImGui::Separator();
		drawLatticeSection();
		ImGui::Separator();
		drawCenteringPresetRow();
		ImGui::Separator();
		drawBasisTable();
		ImGui::Separator();
		drawFormulaAndMappingSection();
		ImGui::Separator();
		drawSymmetrySection();
		ImGui::Separator();

		drawPreviewControls();
		ImGui::Separator();

		ImGui::Checkbox("Export POTCAR##potcar_export", &m_ExportPotcar);
		ImGui::SameLine();
		ImGui::TextDisabled("(requires pseudopotential directory configured)");

		ImGui::Separator();

		Ref<DomainLayer> domainLayer = m_DomainLayer.lock();
		// A row with no species would reach the renderer as an atom of element "", so Create waits
		// until every row has one rather than quietly inventing a default.
		const bool speciesMissing = std::any_of(
			m_BasisRows.begin(), m_BasisRows.end(), [](const BasisRow &row) { return row.species.empty(); });
		ImGui::BeginDisabled(domainLayer == nullptr || m_BasisRows.empty() || speciesMissing);
		if (ImGui::Button("Create"))
		{
			OpenCrystalStructureAsWindow(
				buildStructure(),
				m_StructureNameBuffer.data(),
				*domainLayer,
				m_RendererLayer,
				m_ElementPropertiesTable,
				m_AtomStyleTable,
				/*showCellBox=*/true,
				/*showGrid=*/true,
				m_ExportPotcar);

			// The preview has served its purpose; leaving it open beside the real window is the
			// duplicate-tab complaint in a new costume.
			closePreviews();
		}
		ImGui::EndDisabled();
		if (domainLayer == nullptr)
			ImGui::TextDisabled("DomainLayer unavailable.");
		else if (speciesMissing)
			ImGui::TextDisabled("Every basis row needs a species before the structure can be created.");

		ImGui::End();

		// After the widgets, so an edit made this frame lands in the same frame rather than one
		// behind - and outside Begin/End, since it touches renderer windows, not this one.
		// The gizmo is read back first: a drag in progress owns the atom positions, and rebuilding
		// the window underneath it would fight the mouse.
		if (!pullGizmoEditsFromPreview())
			refreshPreview();
		if (!windowOpen)
			closePreviews();
		SetVisible(windowOpen);
	}

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

		m_BasisRows.clear();
		for (std::size_t siteIndex = 0; siteIndex < prototype->sites.size(); ++siteIndex)
		{
			const SiteDefinition &site = prototype->sites[siteIndex];
			for (const glm::vec3 &fractional : site.positions)
				m_BasisRows.push_back(BasisRow{m_SiteSpecies[siteIndex], fractional});
		}

		if (const std::optional<CrystalSystem> system = parseCrystalSystem(prototype->crystalSystem))
			m_System = *system;
		m_Centering = ParseCenteringName(prototype->centering);
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

	void NewStructureWizardPanel::drawMaterialSection()
	{
		if (!m_CatalogError.empty())
			ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", m_CatalogError.c_str());
		if (m_Catalog.materials.empty())
			return;

		ImGui::TextUnformatted("Material:");
		const char *materialLabel = m_SelectedMaterialIndex >= 0
			? m_Catalog.materials[static_cast<std::size_t>(m_SelectedMaterialIndex)].name.c_str()
			: "(none)";
		ImGui::SetNextItemWidth(-1.0f);
		if (ImGui::BeginCombo("##material", materialLabel))
		{
			for (std::size_t i = 0; i < m_Catalog.materials.size(); ++i)
			{
				const MaterialDefinition &material = m_Catalog.materials[i];
				if (ImGui::Selectable(material.name.c_str(), m_SelectedMaterialIndex == static_cast<int>(i)))
				{
					m_SelectedMaterialIndex = static_cast<int>(i);
					m_SelectedPolytype = material.polytypes.empty() ? std::string{} : material.polytypes.begin()->first;
					applySelectedMaterial();
				}
			}
			ImGui::EndCombo();
		}

		if (m_SelectedMaterialIndex < 0)
			return;
		const MaterialDefinition &material = m_Catalog.materials[static_cast<std::size_t>(m_SelectedMaterialIndex)];

		ImGui::SetNextItemWidth(140.0f);
		if (ImGui::BeginCombo("Polytype", m_SelectedPolytype.c_str()))
		{
			for (const auto &polytypeEntry : material.polytypes)
			{
				if (ImGui::Selectable(polytypeEntry.first.c_str(), polytypeEntry.first == m_SelectedPolytype))
				{
					m_SelectedPolytype = polytypeEntry.first;
					applySelectedMaterial();
				}
			}
			ImGui::EndCombo();
		}

		ImGui::SameLine();
		ImGui::SetNextItemWidth(140.0f);
		if (ImGui::BeginCombo("Functional", m_SelectedFunctional.c_str()))
		{
			for (const auto &constantsEntry : material.constants)
			{
				if (ImGui::Selectable(constantsEntry.first.c_str(), constantsEntry.first == m_SelectedFunctional))
				{
					m_SelectedFunctional = constantsEntry.first;
					applySelectedMaterial();
				}
			}
			ImGui::EndCombo();
		}
	}

	void NewStructureWizardPanel::drawFormulaAndMappingSection()
	{
		if (m_Catalog.prototypes.empty())
			return;

		ImGui::TextUnformatted("Prototype:");
		const PrototypeDefinition *prototype = selectedPrototype();
		ImGui::SetNextItemWidth(-1.0f);
		if (ImGui::BeginCombo("##prototype", prototype != nullptr ? prototype->name.c_str() : "(none)"))
		{
			for (std::size_t i = 0; i < m_Catalog.prototypes.size(); ++i)
			{
				if (ImGui::Selectable(m_Catalog.prototypes[i].name.c_str(), m_SelectedPrototypeIndex == static_cast<int>(i)))
				{
					m_SelectedPrototypeIndex = static_cast<int>(i);
					m_SelectedMaterialIndex = -1; // a bare prototype carries no chemistry
					m_SiteSpecies.assign(m_Catalog.prototypes[i].sites.size(), std::string{});
					applyPrototypeToBasis();
				}
			}
			ImGui::EndCombo();
		}

		prototype = selectedPrototype();
		if (prototype == nullptr)
			return;
		if (!prototype->description.empty())
			ImGui::TextDisabled("%s", prototype->description.c_str());

		ImGui::TextUnformatted("Chemical Formula:");
		ImGui::SetNextItemWidth(-1.0f);
		if (ImGui::InputText("##formula", m_FormulaBuffer.data(), m_FormulaBuffer.size()))
		{
			// Re-matched on every edit. This used to sit behind a function-local `static bool`,
			// which is per-process, not per-panel or per-formula: the mapping table was filled once
			// for the first formula the app ever saw and never updated again.
			const std::vector<Element> elements = FormulaParser::Parse(m_FormulaBuffer.data());
			if (const std::optional<SiteAssignment> matched =
					PrototypeMatcher::MatchFormulaToPrototype(elements, *prototype))
			{
				m_SiteSpecies = matched->species;
				applyPrototypeToBasis();
			}
		}

		if (m_FormulaBuffer[0] != '\0')
		{
			const std::vector<Element> elements = FormulaParser::Parse(m_FormulaBuffer.data());
			const std::optional<SiteAssignment> matched =
				PrototypeMatcher::MatchFormulaToPrototype(elements, *prototype);
			if (!matched)
			{
				int siteTotal = 0;
				for (const SiteDefinition &site : prototype->sites)
					siteTotal += site.Multiplicity();
				ImGui::TextColored(
					ImVec4(1.0f, 0.7f, 0.3f, 1.0f),
					"Formula does not divide into the %d sites of %s - set the mapping below instead.",
					siteTotal,
					prototype->name.c_str());
			}
			else if (matched->isAmbiguous)
			{
				ImGui::TextColored(
					ImVec4(1.0f, 1.0f, 0.0f, 1.0f),
					"Every site has the same multiplicity, so formula order decided the mapping - check it.");
			}
		}

		m_SiteSpecies.resize(prototype->sites.size());

		ImGui::TextUnformatted("Site Mapping:");
		if (ImGui::BeginTable("site_mapping", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
		{
			ImGui::TableSetupColumn("Site", ImGuiTableColumnFlags_WidthFixed, 60.0f);
			ImGui::TableSetupColumn("Atoms", ImGuiTableColumnFlags_WidthFixed, 60.0f);
			ImGui::TableSetupColumn("Species", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableHeadersRow();

			for (std::size_t i = 0; i < prototype->sites.size(); ++i)
			{
				ImGui::TableNextRow();
				ImGui::PushID(static_cast<int>(i));

				ImGui::TableSetColumnIndex(0);
				ImGui::TextUnformatted(prototype->sites[i].name.c_str());

				ImGui::TableSetColumnIndex(1);
				ImGui::Text("%d", prototype->sites[i].Multiplicity());

				ImGui::TableSetColumnIndex(2);
				const bool speciesEmpty = m_SiteSpecies[i].empty();
				if (ImGui::Button(speciesEmpty ? "(select)" : m_SiteSpecies[i].c_str(), ImVec2(-1.0f, 0.0f)))
					ImGui::OpenPopup("##SiteElementPicker");
				std::string picked = m_SiteSpecies[i];
				drawElementPickerPopup("##SiteElementPicker", picked);
				if (picked != m_SiteSpecies[i])
				{
					m_SiteSpecies[i] = picked;
					applyPrototypeToBasis();
				}

				ImGui::PopID();
			}
			ImGui::EndTable();
		}
	}
} // namespace DefectStudio
