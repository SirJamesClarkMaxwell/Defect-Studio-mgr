#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "Renderer/Scene/ModalTransform.hpp"

namespace DefectStudio
{
	struct RendererStructureData;

	enum class SnapTargetMode { Grid, Targets };
	enum class SnapTargetKind { Atom, BondMidpoint, CellCorner, CellEdgeMidpoint, CellFaceCentre };

	struct SnapTarget
	{
		SnapTargetKind kind;
		glm::vec3 position;
		int atomIndex = -1;
		glm::ivec3 image{0};
		// Bond identity for the readout; atomIndex is its first endpoint.
		int secondAtomIndex = -1;
	};

	[[nodiscard]] std::vector<SnapTarget> CollectSnapTargets(const RendererStructureData &structure,
		std::span<const std::size_t> movedAtoms, bool includeCell);
	[[nodiscard]] std::optional<SnapTarget> PickSnapTarget(std::span<const SnapTarget> targets,
		const ModalTransformView &view, const glm::vec2 &mouse, float radiusPixels);
	[[nodiscard]] glm::vec3 TranslationOntoTarget(const glm::vec3 &anchor, const glm::vec3 &target,
		const TransformConstraint &constraint, const TransformBases &bases);
	[[nodiscard]] std::string FormatSnapTargetHeader(const SnapTarget &target,
		const RendererStructureData &structure, const glm::vec3 &translatedAnchor);

	// Picking keeps the preview still. A and B refer to the positions visible at each click.
	enum class ModalPickPurpose { AxisA, AxisB, Pivot, DistanceReference };

	struct PickedAxisState
	{
		bool active = false;
		std::optional<glm::vec3> firstPosition;
		std::string firstLabel;
		ModalPickPurpose purpose = ModalPickPurpose::AxisA;

		void Pick(const RendererStructureData &structure, std::size_t atomIndex, ModalTransformSession &session);
		void Pick(const RendererStructureData &structure, std::size_t atomIndex, ModalTransformSession &session,
			const glm::vec2 &mouse);
		[[nodiscard]] std::string Prompt() const;
	};
} // namespace DefectStudio
