#include "Core/dspch.hpp"

#include "Renderer/Scene/ModalTransform.hpp"

#include <charconv>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <string_view>
#include <system_error>
#include <utility>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_inverse.hpp>

namespace DefectStudio
{
	namespace
	{
		constexpr float kEpsilon = 1.0e-6f;

		template <typename Vector>
		float LengthSquared(const Vector &vector)
		{
			return glm::dot(vector, vector);
		}

		struct Ray
		{
			glm::vec3 origin = glm::vec3(0.0f);
			glm::vec3 direction = glm::vec3(0.0f, 0.0f, -1.0f);
		};

		[[nodiscard]] bool HasBasis(TransformOrientation space, const TransformBases &bases)
		{
			return space == TransformOrientation::Global ||
				(space == TransformOrientation::Local && bases.local.has_value()) ||
				(space == TransformOrientation::Lattice && bases.lattice.has_value()) ||
				(space == TransformOrientation::Defect && bases.defect.has_value());
		}
		[[nodiscard]] glm::vec3 NormalizedColumn(const glm::mat3 &basis, int axis)
		{
			if (axis < 0 || axis > 2 || LengthSquared(basis[axis]) <= kEpsilon * kEpsilon)
				return glm::vec3(0.0f);
			return glm::normalize(basis[axis]);
		}
		[[nodiscard]] glm::mat3 NormalizedBasis(const glm::mat3 &basis)
		{
			return glm::mat3(
				NormalizedColumn(basis, 0),
				NormalizedColumn(basis, 1),
				NormalizedColumn(basis, 2));
		}
		[[nodiscard]] Ray ScreenRay(const ModalTransformView &view, const glm::vec2 &mouse)
		{
			if (view.viewportSize.x <= kEpsilon || view.viewportSize.y <= kEpsilon)
				return {};

			const glm::vec2 relative = (mouse - view.viewportOrigin) / view.viewportSize;
			const glm::vec2 ndc(relative.x * 2.0f - 1.0f, 1.0f - relative.y * 2.0f);
			const glm::mat4 inverseViewProjection = glm::inverse(view.projection * view.view);
			const glm::vec4 nearH = inverseViewProjection * glm::vec4(ndc, -1.0f, 1.0f);
			const glm::vec4 farH = inverseViewProjection * glm::vec4(ndc, 1.0f, 1.0f);
			if (std::abs(nearH.w) <= kEpsilon || std::abs(farH.w) <= kEpsilon)
				return {};

			const glm::vec3 nearPoint = glm::vec3(nearH) / nearH.w;
			const glm::vec3 farPoint = glm::vec3(farH) / farH.w;
			const glm::vec3 direction = farPoint - nearPoint;
			if (LengthSquared(direction) <= kEpsilon * kEpsilon)
				return {};
			return Ray{nearPoint, glm::normalize(direction)};
		}
		[[nodiscard]] std::optional<glm::vec3> IntersectPlane(
			const Ray &ray, const glm::vec3 &point, const glm::vec3 &normal)
		{
			const float denominator = glm::dot(ray.direction, normal);
			if (std::abs(denominator) <= kEpsilon)
				return std::nullopt;
			return ray.origin + ray.direction * (glm::dot(point - ray.origin, normal) / denominator);
		}
		[[nodiscard]] std::optional<glm::vec3> ClosestPointOnAxis(
			const Ray &ray, const glm::vec3 &point, const glm::vec3 &axis)
		{
			const float rayAxisDot = glm::dot(ray.direction, axis);
			const float denominator = 1.0f - rayAxisDot * rayAxisDot;
			if (denominator <= kEpsilon)
				return std::nullopt;

			const glm::vec3 originToPoint = ray.origin - point;
			const float rayOriginDot = glm::dot(ray.direction, originToPoint);
			const float axisOriginDot = glm::dot(axis, originToPoint);
			const float axisParameter = (axisOriginDot - rayAxisDot * rayOriginDot) / denominator;
			return point + axis * axisParameter;
		}
		[[nodiscard]] glm::vec3 TowardViewer(const ModalTransformView &view)
		{
			const glm::vec3 direction(view.view[0][2], view.view[1][2], view.view[2][2]);
			return LengthSquared(direction) > kEpsilon * kEpsilon
				? glm::normalize(direction)
				: glm::vec3(0.0f, 0.0f, 1.0f);
		}
		[[nodiscard]] std::optional<glm::vec2> ProjectToScreen(
			const ModalTransformView &view, const glm::vec3 &world)
		{
			const glm::vec4 clip = view.projection * view.view * glm::vec4(world, 1.0f);
			if (clip.w <= kEpsilon)
				return std::nullopt;
			const glm::vec3 ndc = glm::vec3(clip) / clip.w;
			return view.viewportOrigin + glm::vec2(
				(ndc.x * 0.5f + 0.5f) * view.viewportSize.x,
				(1.0f - (ndc.y * 0.5f + 0.5f)) * view.viewportSize.y);
		}
		[[nodiscard]] glm::vec3 RotationAxis(const ModalTransformSession &session, const ModalTransformView &view)
		{
			if (session.constraint.kind == ConstraintKind::None)
				return TowardViewer(view);
			return NormalizedColumn(ResolveBasis(session.constraint.space, session.bases), session.constraint.axis);
		}

		[[nodiscard]] int FirstPlaneAxis(int excludedAxis)
		{
			for (int axis = 0; axis < 3; ++axis)
			{
				if (axis != excludedAxis)
					return axis;
			}
			return 0;
		}

		[[nodiscard]] glm::vec3 NumericTranslationDirection(const ModalTransformSession &session)
		{
			if (session.constraint.kind == ConstraintKind::None)
				return NormalizedColumn(ResolveBasis(session.orientation, session.bases), 0);

			const int axis = session.constraint.kind == ConstraintKind::Axis
				? session.constraint.axis
				: FirstPlaneAxis(session.constraint.axis);
			return NormalizedColumn(ResolveBasis(session.constraint.space, session.bases), axis);
		}

		[[nodiscard]] glm::vec3 SnapTranslation(
			const glm::vec3 &translation, const ModalTransformSession &session, float step)
		{
			if (step <= 0.0f)
				return translation;

			const TransformOrientation space = session.constraint.kind == ConstraintKind::None
				? session.orientation
				: session.constraint.space;
			const glm::mat3 basis = NormalizedBasis(ResolveBasis(space, session.bases));
			if (std::abs(glm::determinant(basis)) <= kEpsilon)
				return translation;

			glm::vec3 coordinates = glm::inverse(basis) * translation;
			for (int axis = 0; axis < 3; ++axis)
			{
				const bool allowed = session.constraint.kind == ConstraintKind::None ||
					(session.constraint.kind == ConstraintKind::Axis && session.constraint.axis == axis) ||
					(session.constraint.kind == ConstraintKind::Plane && session.constraint.axis != axis);
				coordinates[axis] = allowed ? SnapValue(coordinates[axis], step) : 0.0f;
			}
			return basis * coordinates;
		}

		[[nodiscard]] std::string_view OperationName(ModalTransformOp op)
		{
			switch (op)
			{
				case ModalTransformOp::Translate: return "Move";
				case ModalTransformOp::Rotate: return "Rotate";
				case ModalTransformOp::Scale: return "Scale";
			}
			return "Transform";
		}

		[[nodiscard]] std::string_view OrientationName(TransformOrientation orientation)
		{
			switch (orientation)
			{
				case TransformOrientation::Global: return "Global";
				case TransformOrientation::Local: return "Local";
				case TransformOrientation::Lattice: return "Lattice";
				case TransformOrientation::Defect: return "Defect";
			}
			return "Global";
		}
	} // namespace

	ModalTransformSession BeginModalTransform(
		ModalTransformOp op, TransformOrientation orientation, TransformBases bases, const glm::vec3 &pivot,
		const glm::vec2 &mouse)
	{
		ModalTransformSession session;
		session.op = op;
		session.orientation = orientation;
		session.bases = std::move(bases);
		session.pivot = pivot;
		session.startMouse = mouse;
		session.lastMouse = mouse;
		return session;
	}

	glm::mat3 ResolveBasis(TransformOrientation space, const TransformBases &bases)
	{
		if (space == TransformOrientation::Local && bases.local.has_value())
			return *bases.local;
		if (space == TransformOrientation::Lattice && bases.lattice.has_value())
			return *bases.lattice;
		if (space == TransformOrientation::Defect && bases.defect.has_value())
			return *bases.defect;
		return glm::mat3(1.0f);
	}

	TransformConstraint CycleConstraint(
		const TransformConstraint &current, int axis, bool plane, TransformOrientation orientation,
		const TransformBases &bases)
	{
		if (axis < 0 || axis > 2)
			return {};

		const ConstraintKind kind = plane ? ConstraintKind::Plane : ConstraintKind::Axis;
		TransformOrientation primary = HasBasis(orientation, bases) ? orientation : TransformOrientation::Global;
		std::optional<TransformOrientation> secondary;
		if (primary != TransformOrientation::Global)
			secondary = TransformOrientation::Global;
		else if (HasBasis(TransformOrientation::Local, bases))
			secondary = TransformOrientation::Local;
		else if (HasBasis(TransformOrientation::Defect, bases))
			secondary = TransformOrientation::Defect;

		if (current.kind != kind || current.axis != axis)
			return TransformConstraint{kind, axis, primary};
		if (current.space == primary && secondary.has_value())
			return TransformConstraint{kind, axis, *secondary};
		return {};
	}

	glm::vec3 ConstrainTranslation(
		const glm::vec3 &worldDelta, const TransformConstraint &constraint, const TransformBases &bases)
	{
		if (constraint.kind == ConstraintKind::None || constraint.axis < 0 || constraint.axis > 2)
			return worldDelta;

		const glm::mat3 basis = ResolveBasis(constraint.space, bases);
		if (constraint.kind == ConstraintKind::Axis)
		{
			const glm::vec3 axis = NormalizedColumn(basis, constraint.axis);
			return axis * glm::dot(worldDelta, axis);
		}

		const int first = (constraint.axis + 1) % 3;
		const int second = (constraint.axis + 2) % 3;
		const glm::vec3 normal = glm::cross(basis[first], basis[second]);
		if (LengthSquared(normal) <= kEpsilon * kEpsilon)
			return glm::vec3(0.0f);
		const glm::vec3 unitNormal = glm::normalize(normal);
		return worldDelta - unitNormal * glm::dot(worldDelta, unitNormal);
	}

	glm::mat3 ConstrainedScaleMatrix(
		float factor, const TransformConstraint &constraint, const TransformBases &bases)
	{
		if (constraint.kind == ConstraintKind::None)
			return glm::mat3(factor);
		if (constraint.axis < 0 || constraint.axis > 2)
			return glm::mat3(1.0f);

		const glm::mat3 basis = ResolveBasis(constraint.space, bases);
		if (std::abs(glm::determinant(basis)) <= kEpsilon)
			return glm::mat3(1.0f);

		glm::vec3 diagonal(1.0f);
		if (constraint.kind == ConstraintKind::Axis)
			diagonal[constraint.axis] = factor;
		else
		{
			for (int axis = 0; axis < 3; ++axis)
				if (axis != constraint.axis)
					diagonal[axis] = factor;
		}
		return basis * glm::mat3(diagonal.x, 0.0f, 0.0f, 0.0f, diagonal.y, 0.0f, 0.0f, 0.0f, diagonal.z) *
			glm::inverse(basis);
	}

	float SnapValue(float value, float step)
	{
		return step > 0.0f ? std::round(value / step) * step : value;
	}

	float ResolveSnapStep(ModalTransformOp op, SnapMode mode, const TransformSnapSteps &steps)
	{
		if (mode == SnapMode::Off)
			return 0.0f;
		float step = steps.translate;
		if (op == ModalTransformOp::Rotate)
			step = steps.rotateDegrees;
		else if (op == ModalTransformOp::Scale)
			step = steps.scale;
		return mode == SnapMode::Fine ? step / 10.0f : step;
	}

	SnapMode SnapModeFromModifiers(bool ctrl, bool shift)
	{
		if (!ctrl)
			return SnapMode::Off;
		return shift ? SnapMode::Fine : SnapMode::Increment;
	}

	void AppendNumericChar(ModalTransformSession &session, char character)
	{
		if (character >= '0' && character <= '9')
			session.numericText += character;
		else if (character == '.' && session.numericText.find('.') == std::string::npos)
			session.numericText += character;
		else if (character == '-')
		{
			if (!session.numericText.empty() && session.numericText.front() == '-')
				session.numericText.erase(session.numericText.begin());
			else
				session.numericText.insert(session.numericText.begin(), '-');
		}
	}

	void EraseNumericChar(ModalTransformSession &session)
	{
		if (!session.numericText.empty())
			session.numericText.pop_back();
	}

	std::optional<float> NumericValue(const ModalTransformSession &session)
	{
		if (session.numericText.empty())
			return std::nullopt;
		float value = 0.0f;
		const auto [parsedEnd, error] = std::from_chars(
			session.numericText.data(), session.numericText.data() + session.numericText.size(), value);
		if (error != std::errc{} || parsedEnd != session.numericText.data() + session.numericText.size() ||
			!std::isfinite(value))
			return std::nullopt;
		return value;
	}

	TransformDelta EvaluateModalTransform(
		ModalTransformSession &session, const ModalTransformView &view, const glm::vec2 &mouse, SnapMode snap,
		const TransformSnapSteps &steps)
	{
		TransformDelta result;
		const std::optional<float> numeric = NumericValue(session);
		if (!session.numericText.empty())
		{
			const float value = numeric.value_or(session.op == ModalTransformOp::Scale ? 1.0f : 0.0f);
			if (session.op == ModalTransformOp::Translate)
				result.translation = NumericTranslationDirection(session) * value;
			else if (session.op == ModalTransformOp::Rotate)
			{
				const glm::vec3 axis = RotationAxis(session, view);
				if (LengthSquared(axis) > kEpsilon * kEpsilon)
					result.rotation = glm::angleAxis(glm::radians(value), axis);
			}
			else
				result.linear = ConstrainedScaleMatrix(value, session.constraint, session.bases);
			session.lastMouse = mouse;
			return result;
		}

		const float snapStep = ResolveSnapStep(session.op, snap, steps);
		if (session.op == ModalTransformOp::Translate)
		{
			const Ray startRay = ScreenRay(view, session.startMouse);
			const Ray currentRay = ScreenRay(view, mouse);
			std::optional<glm::vec3> startHit;
			std::optional<glm::vec3> currentHit;
			if (session.constraint.kind == ConstraintKind::Axis)
			{
				const glm::vec3 axis = NormalizedColumn(
					ResolveBasis(session.constraint.space, session.bases), session.constraint.axis);
				startHit = ClosestPointOnAxis(startRay, session.pivot, axis);
				currentHit = ClosestPointOnAxis(currentRay, session.pivot, axis);
			}
			else
			{
				glm::vec3 normal = TowardViewer(view);
				if (session.constraint.kind == ConstraintKind::Plane)
				{
					const glm::mat3 basis = ResolveBasis(session.constraint.space, session.bases);
					const int first = (session.constraint.axis + 1) % 3;
					const int second = (session.constraint.axis + 2) % 3;
					normal = glm::cross(basis[first], basis[second]);
					if (LengthSquared(normal) > kEpsilon * kEpsilon)
						normal = glm::normalize(normal);
				}
				startHit = IntersectPlane(startRay, session.pivot, normal);
				currentHit = IntersectPlane(currentRay, session.pivot, normal);
			}

			if (startHit.has_value() && currentHit.has_value())
			{
				const glm::vec3 constrained = ConstrainTranslation(*currentHit - *startHit, session.constraint, session.bases);
				result.translation = SnapTranslation(constrained, session, snapStep);
			}
		}
		else if (session.op == ModalTransformOp::Rotate)
		{
			const std::optional<glm::vec2> pivotScreen = ProjectToScreen(view, session.pivot);
			if (pivotScreen.has_value())
			{
				const glm::vec2 previous = session.lastMouse - *pivotScreen;
				const glm::vec2 current = mouse - *pivotScreen;
				if (LengthSquared(previous) > 1.0f && LengthSquared(current) > 1.0f)
				{
					const float previousAngle = std::atan2(-previous.y, previous.x);
					const float currentAngle = std::atan2(-current.y, current.x);
					float delta = currentAngle - previousAngle;
					while (delta > glm::pi<float>())
						delta -= glm::two_pi<float>();
					while (delta < -glm::pi<float>())
						delta += glm::two_pi<float>();
					session.accumulatedAngleRadians += delta;
				}
			}
			const glm::vec3 axis = RotationAxis(session, view);
			float angle = session.accumulatedAngleRadians;
			if (snapStep > 0.0f)
				angle = glm::radians(SnapValue(glm::degrees(angle), snapStep));
			if (glm::dot(axis, TowardViewer(view)) < 0.0f)
				angle = -angle;
			if (LengthSquared(axis) > kEpsilon * kEpsilon)
				result.rotation = glm::angleAxis(angle, axis);
		}
		else
		{
			const std::optional<glm::vec2> pivotScreen = ProjectToScreen(view, session.pivot);
			if (pivotScreen.has_value())
			{
				const float startRadius = glm::length(session.startMouse - *pivotScreen);
				float factor = startRadius > kEpsilon ? glm::length(mouse - *pivotScreen) / startRadius : 1.0f;
				factor = SnapValue(factor, snapStep);
				result.linear = ConstrainedScaleMatrix(factor, session.constraint, session.bases);
			}
		}

		session.lastMouse = mouse;
		return result;
	}

	glm::vec3 ApplyTransformDelta(const TransformDelta &delta, const glm::vec3 &start, const glm::vec3 &pivot)
	{
		return pivot + delta.rotation * (delta.linear * (start - pivot)) + delta.translation;
	}

	glm::vec3 ComputeTransformPivot(
		TransformPivotMode mode, const std::vector<glm::vec3> &positions, const std::optional<glm::vec3> &cursor3D)
	{
		if (mode == TransformPivotMode::Cursor3D && cursor3D.has_value())
			return *cursor3D;
		if (positions.empty())
			return cursor3D.value_or(glm::vec3(0.0f));

		glm::vec3 sum(0.0f);
		for (const glm::vec3 &position : positions)
			sum += position;
		return sum / static_cast<float>(positions.size());
	}

	std::string FormatModalTransformHeader(
		const ModalTransformSession &session, const TransformDelta &delta, SnapMode snap,
		const TransformSnapSteps &steps)
	{
		const TransformOrientation space = session.constraint.kind == ConstraintKind::None
			? session.orientation
			: session.constraint.space;
		const std::string_view letters = space == TransformOrientation::Lattice ? "abc" : "XYZ";
		std::string constraintName = "Free";
		if (session.constraint.kind == ConstraintKind::Axis && session.constraint.axis >= 0 && session.constraint.axis < 3)
			constraintName.assign(1, letters[session.constraint.axis]);
		else if (session.constraint.kind == ConstraintKind::Plane && session.constraint.axis >= 0 && session.constraint.axis < 3)
		{
			constraintName.clear();
			for (int axis = 0; axis < 3; ++axis)
				if (axis != session.constraint.axis)
					constraintName += letters[axis];
		}

		std::ostringstream output;
		output << OperationName(session.op) << "  " << constraintName << " (" << OrientationName(space) << ")  ";
		if (!session.numericText.empty())
			output << (session.op == ModalTransformOp::Rotate ? "A " : session.op == ModalTransformOp::Scale ? "F " : "D ")
				<< session.numericText;
		else
		{
			output << std::fixed << std::setprecision(3);
			if (session.op == ModalTransformOp::Translate)
				output << "D " << glm::length(delta.translation) << " A";
			else if (session.op == ModalTransformOp::Rotate)
				output << "A " << glm::degrees(session.accumulatedAngleRadians) << " deg";
			else
			{
				const glm::vec3 direction = NumericTranslationDirection(session);
				output << "F " << glm::length(delta.linear * direction);
			}
		}

		const float snapStep = ResolveSnapStep(session.op, snap, steps);
		if (snapStep > 0.0f)
			output << "  Snap " << std::defaultfloat << snapStep;
		return output.str();
	}
} // namespace DefectStudio
