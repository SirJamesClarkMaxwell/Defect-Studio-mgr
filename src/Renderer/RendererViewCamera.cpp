#include "Core/dspch.hpp"

#include "Renderer/RendererViewCamera.hpp"

#include <algorithm>
#include <cmath>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>

namespace DefectStudio
{
	namespace
	{
		constexpr float kPi = 3.1415926535f;
		constexpr float kPitchLimit = 1.55334306f; // ~89 degrees.
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
	}

	void RendererViewCamera::SetYawPitch(float yawRadians, float pitchRadians)
	{
		m_Yaw = yawRadians;
		m_Pitch = clampedPitch(pitchRadians);
	}

	void RendererViewCamera::SetFromDirection(const glm::vec3 &viewDirection)
	{
		if (glm::length(viewDirection) <= 0.00001f)
			return;

		const glm::vec3 normalized = glm::normalize(viewDirection);
		m_Pitch = clampedPitch(std::asin(normalized.y));
		m_Yaw = std::atan2(normalized.z, normalized.x);
	}

	void RendererViewCamera::FocusBounds(const glm::vec3 &minimum, const glm::vec3 &maximum)
	{
		m_Target = 0.5f * (minimum + maximum);
		const glm::vec3 extent = maximum - minimum;
		const float radius = std::max(0.5f * glm::length(extent), 0.5f);
		m_Distance = std::max(radius * 2.25f, 2.5f);
	}

	void RendererViewCamera::Orbit(float deltaX, float deltaY)
	{
		const float rotationScale = 0.0065f;
		m_Yaw += deltaX * rotationScale;
		m_Pitch = clampedPitch(m_Pitch - deltaY * rotationScale);
	}

	void RendererViewCamera::Pan(float deltaX, float deltaY)
	{
		const float panScale = 0.0028f * m_Distance;
		m_Target -= rightDirection() * (deltaX * panScale);
		m_Target += upDirection() * (deltaY * panScale);
	}

	void RendererViewCamera::Zoom(float delta)
	{
		const float zoomScale = std::max(0.1f, m_Distance * 0.12f);
		m_Distance = std::max(0.25f, m_Distance - delta * zoomScale);
	}

	glm::mat4 RendererViewCamera::ViewMatrix() const
	{
		return glm::lookAt(Position(), m_Target, upDirection());
	}

	glm::mat4 RendererViewCamera::ProjectionMatrix() const
	{
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

	glm::vec3 RendererViewCamera::forwardDirection() const
	{
		const float cosPitch = std::cos(m_Pitch);
		return glm::normalize(glm::vec3(
			std::cos(m_Yaw) * cosPitch,
			std::sin(m_Pitch),
			std::sin(m_Yaw) * cosPitch));
	}

	glm::vec3 RendererViewCamera::rightDirection() const
	{
		return glm::normalize(glm::cross(forwardDirection(), glm::vec3(0.0f, 1.0f, 0.0f)));
	}

	glm::vec3 RendererViewCamera::upDirection() const
	{
		return glm::normalize(glm::cross(rightDirection(), forwardDirection()));
	}

	float RendererViewCamera::clampedPitch(float value) const
	{
		return std::clamp(value, -kPitchLimit, kPitchLimit);
	}
} // namespace DefectStudio

