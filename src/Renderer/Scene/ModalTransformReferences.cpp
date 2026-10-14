#include "Core/dspch.hpp"

#include "Renderer/Scene/ModalTransform.hpp"
#include "Renderer/Scene/ModalSnapTargets.hpp"

#include <cmath>
#include <iomanip>
#include <sstream>

#include "Renderer/RendererTypes.hpp"

namespace DefectStudio
{
	namespace
	{
		bool Finite(const glm::dvec3 &value)
		{
			return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
		}
	}

	std::optional<glm::vec3> TranslationAtDistance(const glm::vec3 &anchor, const glm::vec3 &reference,
		float distance, const TransformConstraint &constraint, const TransformBases &bases)
	{
		if (!Finite(glm::dvec3(anchor)) || !Finite(glm::dvec3(reference)) ||
			!std::isfinite(distance) || distance < 0.0f)
			return std::nullopt;

		const glm::dvec3 offset = glm::dvec3(anchor) - glm::dvec3(reference);
		glm::dvec3 translation(0.0);
		if (constraint.kind == ConstraintKind::None)
		{
			const double length = glm::length(offset);
			if (length == 0.0)
				return std::nullopt;
			translation = offset * (static_cast<double>(distance) / length - 1.0);
		}
		else
		{
			if (constraint.axis < 0 || constraint.axis > 2)
				return std::nullopt;
			const glm::mat3 basis = ResolveBasis(constraint.space, bases);
			glm::dvec3 direction = constraint.kind == ConstraintKind::Axis
				? glm::dvec3(basis[constraint.axis])
				: glm::cross(glm::dvec3(basis[(constraint.axis + 1) % 3]),
					glm::dvec3(basis[(constraint.axis + 2) % 3]));
			const double length = glm::length(direction);
			if (!Finite(direction) || length == 0.0)
				return std::nullopt;
			direction /= length;
			const double component = glm::dot(offset, direction);
			const glm::dvec3 inPlane = offset - component * direction;
			const double radiusSquared = static_cast<double>(distance) * distance;
			if (constraint.kind == ConstraintKind::Axis)
			{
				const double discriminant = radiusSquared - glm::dot(inPlane, inPlane);
				if (discriminant < 0.0)
					return std::nullopt;
				const double root = std::sqrt(discriminant);
				const double positive = -component + root, negative = -component - root;
				translation = direction * (std::abs(positive) <= std::abs(negative) ? positive : negative);
			}
			else
			{
				const double circleRadiusSquared = radiusSquared - component * component;
				const double radialLength = glm::length(inPlane);
				if (circleRadiusSquared < 0.0 || radialLength == 0.0)
					return std::nullopt;
				translation = inPlane * (std::sqrt(circleRadiusSquared) / radialLength - 1.0);
			}
		}
		const glm::vec3 result(translation);
		return Finite(glm::dvec3(result)) && Finite(glm::dvec3(anchor + result))
			? std::optional<glm::vec3>(result) : std::nullopt;
	}

	bool CanConfirmModalTransform(const ModalTransformSession &session)
	{
		if (session.numericText.empty())
			return true;
		const auto value = NumericValue(session);
		if (!value)
			return false;
		return session.op != ModalTransformOp::Translate || !session.distanceReference ||
			TranslationAtDistance(session.anchor, *session.distanceReference, *value, session.constraint, session.bases).has_value();
	}

	std::string FormatModalReferenceHeader(const ModalTransformSession &session, const TransformDelta &delta)
	{
		std::ostringstream output;
		if (!session.pickedPivotLabel.empty() && session.op != ModalTransformOp::Translate)
			output << "  punkt obrotu: " << session.pickedPivotLabel;
		if (session.distanceReference && session.op == ModalTransformOp::Translate)
		{
			output << "  odległość od " << session.distanceReferenceLabel << ": ";
			if (!CanConfirmModalTransform(session))
				output << "nieosiągalne";
			else
				output << std::fixed << std::setprecision(2)
					<< glm::distance(session.anchor + delta.translation, *session.distanceReference) << " Å";
		}
		return output.str();
	}

	void PickedAxisState::Pick(const RendererStructureData &structure, std::size_t atomIndex,
		ModalTransformSession &session)
	{
		Pick(structure, atomIndex, session, session.lastMouse);
	}

	void PickedAxisState::Pick(const RendererStructureData &structure, std::size_t atomIndex,
		ModalTransformSession &session, const glm::vec2 &mouse)
	{
		if (!active || atomIndex >= structure.atoms.size())
			return;
		const glm::vec3 position = structure.atoms[atomIndex].cartesianPosition;
		if (!Finite(glm::dvec3(position)))
			return;
		const std::string label = structure.atoms[atomIndex].element + std::to_string(atomIndex + 1);
		if (purpose == ModalPickPurpose::Pivot)
		{
			if (session.op == ModalTransformOp::Translate)
				return;
			session.pivot = position;
			session.pickedPivotLabel = label;
			session.startMouse = mouse;
			session.lastMouse = mouse;
			session.accumulatedAngleRadians = 0.0f;
			session.numericText.clear();
			active = false;
		}
		else if (purpose == ModalPickPurpose::DistanceReference)
		{
			if (session.op != ModalTransformOp::Translate)
				return;
			session.distanceReference = position;
			session.distanceReferenceLabel = label;
			active = false;
		}
		else if (!firstPosition)
		{
			firstPosition = position;
			firstLabel = label;
			purpose = ModalPickPurpose::AxisB;
		}
		else if (const auto basis = MakePickedBasis(*firstPosition, position))
		{
			session.bases.picked = *basis;
			session.constraint = {ConstraintKind::Axis, 0, TransformOrientation::Picked};
			session.pickedAxisLabel = firstLabel + "→" + label;
			active = false;
		}
	}

	std::string PickedAxisState::Prompt() const
	{
		if (!active)
			return {};
		if (purpose == ModalPickPurpose::Pivot)
			return "  Wskaż punkt obrotu (atom)";
		if (purpose == ModalPickPurpose::DistanceReference)
			return "  Wskaż atom odniesienia odległości";
		return firstPosition ? "  Wskaż B (A: " + firstLabel + ")" : "  Wskaż A";
	}
} // namespace DefectStudio
