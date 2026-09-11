#include "Core/dspch.hpp"

#include "Renderer/CrystalStructurePreviewWindow.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>
#include <vector>

#include "Core/Logging/Logger.hpp"
#include "Core/Utils/Path.hpp"
#include "Domain/Crystal/BondGenerator.hpp"
#include "Renderer/RendererLayer.hpp"
#include "Renderer/RendererStartupBootstrap.hpp"
#include "Renderer/Scene/SceneSystem.hpp"
#include "Renderer/StructureRendererDataBuilder.hpp"

namespace DefectStudio
{
	namespace
	{
		// VESTA-style boundary completion, for the picture only. A cell holds its atoms on
		// [0, 1) per axis, so the far faces carry none: the top row of a rock-salt supercell loses
		// exactly the cations that close its cells, and the box reads as if it had been sliced
		// rather than terminated. An atom sitting on a zero-face has an image on the opposite face
		// that belongs to the drawing just as much, so add it - one image per non-empty subset of
		// the axes it sits on, which gives a corner atom all seven of its images and a face atom
		// exactly one.
		//
		// Never applied to the structure that gets written out: these are duplicates, and a POSCAR
		// built from them would carry the wrong stoichiometry. Only the preview windows that draw a
		// cell box call this; the basis pane deliberately does not, since its atom list has to stay
		// one-to-one with the wizard's basis rows for the gizmo read-back to be able to trust it.
		void AddBoundaryImageAtoms(CrystalStructure &structure)
		{
			constexpr float kOnFaceTolerance = 1e-4f;
			const glm::mat3 lattice = structure.cell.ToMatrix();
			const std::size_t originalCount = structure.atoms.size();
			for (std::size_t i = 0; i < originalCount; ++i)
			{
				// Copied, not referenced: push_back below can reallocate the vector out from under it.
				const AtomSite atom = structure.atoms[i];
				std::array<int, 3> axesOnFace{};
				int onFaceCount = 0;
				for (int axis = 0; axis < 3; ++axis)
				{
					if (std::abs(atom.fractional[axis]) < kOnFaceTolerance)
						axesOnFace[static_cast<std::size_t>(onFaceCount++)] = axis;
				}
				if (onFaceCount == 0)
					continue;

				for (int mask = 1; mask < (1 << onFaceCount); ++mask)
				{
					AtomSite image = atom;
					for (int bit = 0; bit < onFaceCount; ++bit)
					{
						if ((mask & (1 << bit)) != 0)
							image.fractional[axesOnFace[static_cast<std::size_t>(bit)]] += 1.0f;
					}
					image.position = lattice * image.fractional;
					image.index = static_cast<int>(structure.atoms.size());
					structure.atoms.push_back(std::move(image));
				}
			}
		}

		[[nodiscard]] RendererWindowState *FindWindow(RendererLayer &rendererLayer, const std::string &windowId)
		{
			if (windowId.empty())
				return nullptr;
			std::vector<RendererWindowState> &windows = rendererLayer.GetWindows();
			const auto it = std::find_if(
				windows.begin(),
				windows.end(),
				[&](const RendererWindowState &window) { return window.windowId == windowId; });
			return it != windows.end() ? &*it : nullptr;
		}
	} // namespace

	std::string ShowCrystalStructurePreview(
		const std::string &existingWindowId,
		const CrystalStructure &structure,
		const std::string &displayName,
		RendererLayer &rendererLayer,
		const ElementPropertiesTable &elementPropertiesTable,
		const AtomStyleTable &atomStyleTable,
		bool showCellBox,
		bool showGrid,
		const std::optional<glm::mat3> &overlayCellVectors,
		const glm::ivec3 &overlayCellRepeat,
		const std::string &sessionId)
	{
		CrystalStructure bonded = structure;
		// Before bonding, so the images that close the box get their bonds like any other atom.
		if (showCellBox)
			AddBoundaryImageAtoms(bonded);
		RegenerateAutoBonds(bonded, elementPropertiesTable);

		// Empty domainStructureId on purpose: nothing about this structure lives in the domain, and
		// ResolveAtomEditTarget rejects a window without one rather than editing a phantom record.
		RendererStructureData structureData =
			BuildRendererStructureData(bonded, Path{}, displayName, atomStyleTable, std::string{});
		if (overlayCellVectors.has_value())
		{
			// One cell's 12 edges, translated once per repeat. Shared faces are drawn twice over -
			// a line VBO does not care, and de-duplicating them would cost more code than the
			// handful of extra vertices it saves.
			const std::vector<RendererCellEdge> cellEdges = BuildCellEdges(*overlayCellVectors);
			const glm::ivec3 repeat = glm::max(overlayCellRepeat, glm::ivec3(1));
			structureData.overlayCellEdges.reserve(
				cellEdges.size() * static_cast<std::size_t>(repeat.x * repeat.y * repeat.z));
			for (int i = 0; i < repeat.x; ++i)
			{
				for (int j = 0; j < repeat.y; ++j)
				{
					for (int k = 0; k < repeat.z; ++k)
					{
						const glm::vec3 shift = (*overlayCellVectors)[0] * static_cast<float>(i) +
							(*overlayCellVectors)[1] * static_cast<float>(j) +
							(*overlayCellVectors)[2] * static_cast<float>(k);
						for (RendererCellEdge edge : cellEdges)
						{
							edge.start += shift;
							edge.finish += shift;
							structureData.overlayCellEdges.push_back(edge);
						}
					}
				}
			}
		}

		if (RendererWindowState *existing = FindWindow(rendererLayer, existingWindowId))
		{
			// Refresh in place, keeping the camera. Re-framing on every keystroke would make the
			// preview jump around while the user is still typing the number that caused it.
			existing->title = displayName;
			existing->structure = std::move(structureData);
			existing->showCellBox = showCellBox;
			existing->showGrid = showGrid;
			existing->sessionId = sessionId;
			SceneSystem::SyncSceneWithStructure(existing->sceneRegistry, existing->structure);
			SceneSystem::PushSelectionAndVisibilityToWindowState(existing->sceneRegistry, *existing);
			return existingWindowId;
		}

		RendererStartupWindowInput input;
		input.definition.title = displayName;
		input.definition.structureName = displayName;
		input.definition.poscarPath = Path{};
		input.structure = std::move(structureData);

		std::vector<RendererStartupWindowInput> inputs;
		inputs.push_back(std::move(input));
		std::vector<RendererWindowState> windows = BuildRendererStartupWindows(std::move(inputs));
		if (windows.empty())
		{
			DS_LOG_ERROR("Structure preview: failed to build renderer window for '{}'", displayName);
			return {};
		}

		RendererWindowState window = std::move(windows.front());
		window.showCellBox = showCellBox;
		window.showGrid = showGrid;
		window.sessionId = sessionId;
		rendererLayer.AddWindow(std::move(window));

		// AddWindow rewrites windowId on a collision with an already-open window, so the id is read
		// back from the window it actually created rather than from the one handed to it.
		const std::vector<RendererWindowState> &openWindows = rendererLayer.GetWindows();
		return openWindows.empty() ? std::string{} : openWindows.back().windowId;
	}

	void CloseCrystalStructurePreview(const std::string &windowId, RendererLayer &rendererLayer)
	{
		if (windowId.empty())
			return;
		rendererLayer.RemoveWindow(windowId);
	}
} // namespace DefectStudio
