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
		constexpr float kPitchLimit = 1.55334306f; // ~89 degrees
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

	void RendererViewCamera::SetFromDirection(const glm::vec3 &viewDirection)
	{
		if (glm::length(viewDirection) <= 0.00001f)
			return;
		const glm::vec3 normalized = glm::normalize(viewDirection);
		m_Pitch = clampedPitch(std::asin(normalized.y));
		m_Yaw = std::atan2(normalized.z, normalized.x);
	}

	void RendererViewCamera::SetAlignToAxis(const glm::vec3 &axis, const glm::vec3 &upHint)
	{
		(void)upHint;
		SetFromDirection(glm::normalize(axis));
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
		m_Yaw = m_Yaw - deltaX * kScale;
		m_Pitch = clampedPitch(m_Pitch - deltaY * kScale);
	}

	void RendererViewCamera::Pan(float deltaX, float deltaY)
	{
		const float panScale = 0.0028f * m_Distance;
		m_Target -= rightDirection() * (deltaX * panScale);
		m_Target += upDirection() * (deltaY * panScale);
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

	CameraProjection RendererViewCamera::Projection() const
	{
		return m_Projection;
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

	float RendererViewCamera::orthoHalfHeight() const
	{
		return std::max(m_OrthoScale, 0.1f);
	}
} // namespace DefectStudio
