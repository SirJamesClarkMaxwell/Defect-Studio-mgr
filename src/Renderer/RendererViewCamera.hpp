#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "Renderer/RendererSettings.hpp"

namespace DefectStudio
{
	class RendererViewCamera
	{
	public:
		[[nodiscard]] static float NormalizeAngleRadians(float angle);
		[[nodiscard]] static float ComputeTransitionDurationSeconds(float rotationSpeed) noexcept;
		static void BuildCameraAxesFromEuler(
			float yaw,
			float pitch,
			float roll,
			glm::vec3 &forward,
			glm::vec3 &up);
		[[nodiscard]] static glm::quat CameraOrientationQuatFromEuler(float yaw, float pitch, float roll);
		static void CameraEulerFromOrientationQuat(
			const glm::quat &orientationQuat,
			float &yaw,
			float &pitch,
			float &roll);

		RendererViewCamera();

		void SetViewport(float width, float height);
		void SetTarget(const glm::vec3 &target);
		void SetDistance(float distance);
		void SetYawPitch(float yawRadians, float pitchRadians);
		void SetOrbitState(const glm::vec3 &target, float distance, float yawRadians, float pitchRadians);
		void SetFromDirection(const glm::vec3 &viewDirection);
		void FocusBounds(const glm::vec3 &minimum, const glm::vec3 &maximum);
		void SetProjection(CameraProjection projection);
		void ToggleProjection();
		void SetRoll(float rollRadians);

		// VESTA-style: orbit uses screen-space axes (local camera frame)
		void Orbit(float deltaX, float deltaY);
		void Roll(float delta);
		void Pan(float deltaX, float deltaY);
		void Zoom(float delta);

		// Snap to crystallographic axis directions
		void SetAlignToAxis(const glm::vec3 &axis, const glm::vec3 &up);

		[[nodiscard]] glm::mat4 ViewMatrix() const;
		[[nodiscard]] glm::mat4 ProjectionMatrix() const;
		[[nodiscard]] glm::vec3 Position() const;
		[[nodiscard]] glm::vec3 Target() const;
		[[nodiscard]] float Distance() const;
		[[nodiscard]] float Yaw() const;
		[[nodiscard]] float Pitch() const;
		[[nodiscard]] float Roll() const;
		[[nodiscard]] CameraProjection Projection() const;

	private:
		[[nodiscard]] glm::vec3 forwardDirection() const;
		[[nodiscard]] glm::vec3 rightDirection() const;
		[[nodiscard]] glm::vec3 upDirection() const;
		[[nodiscard]] float clampedPitch(float value) const;
		[[nodiscard]] float orthoHalfHeight() const;

	private:
		glm::vec3 m_Target = glm::vec3(0.0f, 0.0f, 0.0f);
		float m_Distance = 10.0f;
		float m_Yaw = 0.8f;
		float m_Pitch = 0.5f;
		float m_AspectRatio = 16.0f / 9.0f;
		float m_FieldOfViewRadians = 45.0f * 3.1415926535f / 180.0f;
		float m_NearPlane = 0.01f;
		float m_FarPlane = 500.0f;
		CameraProjection m_Projection = CameraProjection::Perspective;
		float m_OrthoScale = 1.0f; // zoom scale for orthographic
		float m_Roll = 0.0f;
	};
} // namespace DefectStudio
