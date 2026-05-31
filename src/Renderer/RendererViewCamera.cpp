#include "Core/dspch.hpp"
#include "Renderer/RendererViewCamera.hpp"

#include <algorithm>
#include <cmath>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>

namespace DefectStudio
{
	namespace
	{
		constexpr float kPitchLimit = 1.5692268f; // ~89.91 degrees

		[[nodiscard]] float NormalizeAngleRadians(float angle)
		{
			constexpr float kTwoPi = 6.283185307f;
			constexpr float kPi = 3.1415926535f;
			while (angle > kPi)
				angle -= kTwoPi;
			while (angle < -kPi)
				angle += kTwoPi;
			return angle;
		}
	}

	RendererViewCamera::RendererViewCamera() = default;

	void RendererViewCamera::SetViewport(float width, float height)
	{
		const float safeWidth = std::max(width, 1.0f);
		const float safeHeight = std::max(height, 1.0f);
		m_AspectRatio = safeWidth / safeHeight;
	}

	void RendererViewCamera::SetTarget(const glm::vec3 &target)
	{
		m_Target = target;
	}

	void RendererViewCamera::SetDistance(float distance)
	{
		m_Distance = std::max(distance, 0.1f);
		if (m_Projection == CameraProjection::Orthographic)
			m_OrthoScale = std::max(0.1f, m_Distance * std::tan(m_FieldOfViewRadians * 0.5f));
	}

	void RendererViewCamera::SetProjection(CameraProjection projection)
	{
		m_Projection = projection;
	}

	void RendererViewCamera::ToggleProjection()
	{
		m_Projection = (m_Projection == CameraProjection::Perspective)
			? CameraProjection::Orthographic
			: CameraProjection::Perspective;
		m_OrthoScale = m_Distance * std::tan(m_FieldOfViewRadians * 0.5f);
	}

	void RendererViewCamera::SetYawPitch(float yawRadians, float pitchRadians)
	{
		m_Yaw = yawRadians;
		m_Pitch = clampedPitch(pitchRadians);
	}

	void RendererViewCamera::SetOrbitState(
		const glm::vec3 &target,
		float distance,
		float yawRadians,
		float pitchRadians)
	{
		m_Target = target;
		m_Distance = std::max(distance, 0.1f);
		if (m_Projection == CameraProjection::Orthographic)
			m_OrthoScale = std::max(0.1f, m_Distance * std::tan(m_FieldOfViewRadians * 0.5f));
		m_Yaw = yawRadians;
		m_Pitch = clampedPitch(pitchRadians);
	}

	void RendererViewCamera::SetFromDirection(const glm::vec3 &viewDirection)
	{
		if (glm::length(viewDirection) <= 0.00001f)
			return;
		const glm::vec3 normalized = glm::normalize(viewDirection);
		m_Pitch = clampedPitch(std::asin(normalized.z));
		m_Yaw = std::atan2(normalized.x, normalized.y);
		m_Roll = 0.0f;
	}

	void RendererViewCamera::SetRoll(float rollRadians)
	{
		m_Roll = NormalizeAngleRadians(rollRadians);
	}

	void RendererViewCamera::SetAlignToAxis(const glm::vec3 &axis, const glm::vec3 &upHint)
	{
		if (glm::length(axis) <= 0.00001f)
			return;

		const glm::vec3 forward = glm::normalize(axis);
		m_Pitch = clampedPitch(std::asin(forward.z));
		m_Yaw = std::atan2(forward.x, forward.y);

		glm::vec3 baseRight = glm::cross(forward, glm::vec3(0.0f, 0.0f, 1.0f));
		if (glm::dot(baseRight, baseRight) <= 1e-8f)
			baseRight = glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f));
		baseRight = glm::normalize(baseRight);
		const glm::vec3 baseUp = glm::normalize(glm::cross(baseRight, forward));

		glm::vec3 requestedUp = upHint - glm::dot(upHint, forward) * forward;
		if (glm::dot(requestedUp, requestedUp) <= 1e-8f)
			requestedUp = baseUp;
		requestedUp = glm::normalize(requestedUp);

		const float sinRoll = glm::dot(glm::cross(baseUp, requestedUp), forward);
		const float cosRoll = glm::dot(baseUp, requestedUp);
		m_Roll = NormalizeAngleRadians(std::atan2(sinRoll, cosRoll));
	}

	void RendererViewCamera::FocusBounds(const glm::vec3 &minimum, const glm::vec3 &maximum)
	{
		m_Target = 0.5f * (minimum + maximum);
		const glm::vec3 extent = maximum - minimum;
		const float radius = std::max(0.5f * glm::length(extent), 0.5f);
		m_Distance = std::max(radius * 2.25f, 2.5f);
		m_OrthoScale = m_Distance * std::tan(m_FieldOfViewRadians * 0.5f);
	}

	void RendererViewCamera::Orbit(float deltaX, float deltaY)
	{
		constexpr float kScale = 0.0065f;

		const glm::vec3 forward = forwardDirection();
		const glm::vec3 up = upDirection();

		const glm::quat yawRotation = glm::angleAxis(-deltaX * kScale, glm::normalize(up));
		glm::vec3 rotatedForward = glm::normalize(yawRotation * forward);
		glm::vec3 rotatedUp = glm::normalize(yawRotation * up);

		glm::vec3 localRight = glm::cross(rotatedForward, rotatedUp);
		if (glm::dot(localRight, localRight) <= 1e-8f)
			localRight = rightDirection();
		localRight = glm::normalize(localRight);

		const glm::quat pitchRotation = glm::angleAxis(-deltaY * kScale, localRight);
		rotatedForward = glm::normalize(pitchRotation * rotatedForward);
		rotatedUp = glm::normalize(pitchRotation * rotatedUp);

		glm::vec3 baseRight = glm::cross(rotatedForward, glm::vec3(0.0f, 0.0f, 1.0f));
		if (glm::dot(baseRight, baseRight) <= 1e-8f)
			baseRight = glm::cross(rotatedForward, glm::vec3(0.0f, 1.0f, 0.0f));
		baseRight = glm::normalize(baseRight);
		const glm::vec3 baseUp = glm::normalize(glm::cross(baseRight, rotatedForward));

		const float sinRoll = glm::dot(glm::cross(baseUp, rotatedUp), rotatedForward);
		const float cosRoll = glm::dot(baseUp, rotatedUp);
		m_Roll = NormalizeAngleRadians(std::atan2(sinRoll, cosRoll));

		m_Yaw = std::atan2(rotatedForward.x, rotatedForward.y);
		m_Pitch = clampedPitch(std::asin(glm::clamp(rotatedForward.z, -1.0f, 1.0f)));
	}

	void RendererViewCamera::Pan(float deltaX, float deltaY)
	{
		const float panScale = 0.0028f * m_Distance;
		m_Target -= rightDirection() * (deltaX * panScale);
		m_Target += upDirection() * (deltaY * panScale);
	}

	void RendererViewCamera::Roll(float delta)
	{
		m_Roll = NormalizeAngleRadians(m_Roll + delta);
	}

	void RendererViewCamera::Zoom(float delta)
	{
		if (m_Projection == CameraProjection::Perspective)
		{
			const float zoomScale = std::max(0.1f, m_Distance * 0.12f);
			m_Distance = std::max(0.25f, m_Distance - delta * zoomScale);
		}
		else
		{
			m_OrthoScale = std::max(0.25f, m_OrthoScale * (1.0f - delta * 0.1f));
			m_Distance = std::max(
				0.25f,
				m_OrthoScale / std::tan(m_FieldOfViewRadians * 0.5f));
		}
	}

	glm::mat4 RendererViewCamera::ViewMatrix() const
	{
		return glm::lookAt(Position(), m_Target, upDirection());
	}

	glm::mat4 RendererViewCamera::ProjectionMatrix() const
	{
		if (m_Projection == CameraProjection::Orthographic)
		{
			const float h = orthoHalfHeight();
			const float w = h * m_AspectRatio;
			return glm::ortho(-w, w, -h, h, m_NearPlane, m_FarPlane);
		}
		return glm::perspective(m_FieldOfViewRadians, m_AspectRatio, m_NearPlane, m_FarPlane);
	}

	glm::vec3 RendererViewCamera::Position() const
	{
		return m_Target - forwardDirection() * m_Distance;
	}

	glm::vec3 RendererViewCamera::Target() const
	{
		return m_Target;
	}

	float RendererViewCamera::Distance() const
	{
		return m_Distance;
	}

	float RendererViewCamera::Yaw() const
	{
		return m_Yaw;
	}

	float RendererViewCamera::Pitch() const
	{
		return m_Pitch;
	}

	float RendererViewCamera::Roll() const
	{
		return m_Roll;
	}

	CameraProjection RendererViewCamera::Projection() const
	{
		return m_Projection;
	}

	glm::vec3 RendererViewCamera::forwardDirection() const
	{
		const float cosPitch = std::cos(m_Pitch);
		return glm::normalize(glm::vec3(
			std::sin(m_Yaw) * cosPitch,
			std::cos(m_Yaw) * cosPitch,
			std::sin(m_Pitch)));
	}

	glm::vec3 RendererViewCamera::rightDirection() const
	{
		const glm::vec3 forward = forwardDirection();
		glm::vec3 baseRight = glm::cross(forward, glm::vec3(0.0f, 0.0f, 1.0f));
		if (glm::dot(baseRight, baseRight) <= 1e-8f)
			baseRight = glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f));
		baseRight = glm::normalize(baseRight);
		if (std::abs(m_Roll) <= 1e-6f)
			return baseRight;

		const glm::quat rollRotation = glm::angleAxis(m_Roll, forward);
		return glm::normalize(rollRotation * baseRight);
	}

	glm::vec3 RendererViewCamera::upDirection() const
	{
		return glm::normalize(glm::cross(rightDirection(), forwardDirection()));
	}

	float RendererViewCamera::clampedPitch(float value) const
	{
		return std::clamp(value, -kPitchLimit, kPitchLimit);
	}

	float RendererViewCamera::orthoHalfHeight() const
	{
		return std::max(m_OrthoScale, 0.1f);
	}
} // namespace DefectStudio
