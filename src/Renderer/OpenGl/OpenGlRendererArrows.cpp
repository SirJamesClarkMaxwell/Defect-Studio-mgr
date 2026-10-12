#include "Core/dspch.hpp"
#include "Renderer/OpenGl/OpenGlRendererBackend.hpp"

#include <algorithm>
#include <cmath>
#include <glad/gl.h>
#if defined(TRACY_ENABLE)
#include <tracy/TracyOpenGL.hpp>
#endif

namespace DefectStudio
{
	namespace
	{
		// Same magnitude-driven ramp for both the shaft and the head of one displacement arrow -
		// short/likely-converged moves read green, moves near the current slider ceiling read red.
		[[nodiscard]] glm::vec4 DisplacementArrowColor(
			float magnitudeAngstrom, float normalizationMax, const glm::vec3 &lowColor, const glm::vec3 &highColor)
		{
			const float t = normalizationMax > 0.0001f
				? std::clamp(magnitudeAngstrom / normalizationMax, 0.0f, 1.0f)
				: 0.0f;
			return glm::vec4(glm::mix(lowColor, highColor, t), 1.0f);
		}

		void UploadInstanceBuffer(unsigned int instanceVbo, const void *data, std::size_t byteSize)
		{
			glBindBuffer(GL_ARRAY_BUFFER, instanceVbo);
			GLint currentSize = 0;
			glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &currentSize);
			const auto requiredBytes = static_cast<GLsizeiptr>(byteSize);
			if (static_cast<GLsizeiptr>(currentSize) < requiredBytes)
			{
				const GLsizeiptr newSize = requiredBytes + requiredBytes / 2;
				glBufferData(GL_ARRAY_BUFFER, newSize, nullptr, GL_DYNAMIC_DRAW);
			}
			glBufferSubData(GL_ARRAY_BUFFER, 0, requiredBytes, data);
		}
	} // namespace

	void OpenGlRendererBackend::renderDisplacementArrows(
		const RendererStructureData &structure,
		const RendererWindowState::DisplacementComparisonState *displacementComparison,
		const RendererViewCamera &camera,
		const RendererGlobalRenderSettings &globalSettings,
		const glm::vec3 &sceneOffset)
	{
		if (displacementComparison == nullptr || !displacementComparison->visible)
			return;
		const StructureComparisonResult &result = displacementComparison->result;
		if (result.matches.empty() && result.unmatchedComparisonAtoms.empty())
			return;

		const auto isReferenceAtomVisible = [&](std::size_t referenceAtomIndex)
		{
			return !displacementComparison->onlyForVisibleAtoms ||
				(referenceAtomIndex < structure.atoms.size() && structure.atoms[referenceAtomIndex].visible);
		};

		float maxVisibleMagnitude = 0.0f;
		for (const AtomDisplacement &displacement : result.matches)
			if (displacement.magnitudeAngstrom <= displacementComparison->displayThresholdAngstrom &&
				isReferenceAtomVisible(displacement.referenceAtomIndex))
				maxVisibleMagnitude = std::max(maxVisibleMagnitude, displacement.magnitudeAngstrom);
		const float normalizationMax = displacementComparison->normalizeColorToVisibleMax
			? maxVisibleMagnitude
			: displacementComparison->fixedNormalizationMaxAngstrom;

		std::vector<ArrowSegment> segments;
		for (const AtomDisplacement &displacement : result.matches)
		{
			if (displacement.magnitudeAngstrom > displacementComparison->displayThresholdAngstrom)
				continue;
			if (!isReferenceAtomVisible(displacement.referenceAtomIndex))
				continue;

			// Minimum-image tip, not the raw comparison position - drawing raw positions can span the
			// whole cell for an atom that only moved a short distance across a periodic boundary (see
			// 2026-08-28 feedback). The wrapped point may sit just outside the cell box; that's the
			// intended "atom exits the cell" look.
			const glm::vec4 color = DisplacementArrowColor(
				displacement.magnitudeAngstrom, normalizationMax,
				displacementComparison->lowMagnitudeColor, displacementComparison->highMagnitudeColor);

			segments.push_back({displacement.referencePosition, displacement.comparisonPositionWrapped, color});
		}

		// ponytail: flat translucent color for every interstitial-like ghost marker, not a real
		// per-species AtomStyleTable/VacancyRenderStyle lookup - that table isn't threaded through
		// this call today (only StructureRendererDataBuilder reads it). Upgrade if ghost markers need
		// to read element color the same way real atoms do.
		constexpr glm::vec4 kGhostColor(0.75f, 0.75f, 0.8f, 0.45f);
		constexpr float kGhostRadius = 0.35f;
		std::vector<OpenGlAtomInstance> ghostInstances;
		ghostInstances.reserve(result.unmatchedComparisonAtoms.size());
		for (const UnmatchedComparisonAtom &ghost : result.unmatchedComparisonAtoms)
		{
			OpenGlAtomInstance instance;
			instance.positionRadius = glm::vec4(ghost.position, kGhostRadius);
			instance.color = kGhostColor;
			ghostInstances.push_back(instance);
		}

		if (segments.empty() && ghostInstances.empty())
			return;

		const glm::mat4 viewProjection = camera.ProjectionMatrix() * camera.ViewMatrix();

		renderArrowSegments(segments, camera, globalSettings, sceneOffset);

		if (!ghostInstances.empty())
		{
			const unsigned int program = m_ShaderLibrary.Program("atoms");
			if (program != 0)
			{
#if defined(TRACY_ENABLE)
				TracyGpuZone("Renderer.DisplacementGhosts");
#endif
				glUseProgram(program);
				const int viewProjectionLocation = m_ShaderLibrary.Uniform("atoms", "u_ViewProjection");
				if (viewProjectionLocation >= 0)
					glUniformMatrix4fv(viewProjectionLocation, 1, GL_FALSE, &viewProjection[0][0]);
				const int keyDirectionLocation = m_ShaderLibrary.Uniform("atoms", "u_KeyDirection");
				const int fillDirectionLocation = m_ShaderLibrary.Uniform("atoms", "u_FillDirection");
				const int backDirectionLocation = m_ShaderLibrary.Uniform("atoms", "u_BackDirection");
				const int ambientLocation = m_ShaderLibrary.Uniform("atoms", "u_AmbientIntensity");
				const int keyIntensityLocation = m_ShaderLibrary.Uniform("atoms", "u_KeyIntensity");
				const int fillIntensityLocation = m_ShaderLibrary.Uniform("atoms", "u_FillIntensity");
				const int backIntensityLocation = m_ShaderLibrary.Uniform("atoms", "u_BackIntensity");
				const int twoSidedLocation = m_ShaderLibrary.Uniform("atoms", "u_TwoSidedLighting");
				const int cameraPositionLocation = m_ShaderLibrary.Uniform("atoms", "u_CameraPosition");
				const int specularIntensityLocation = m_ShaderLibrary.Uniform("atoms", "u_SpecularIntensity");
				const int shininessLocation = m_ShaderLibrary.Uniform("atoms", "u_Shininess");
				const int saturationLocation = m_ShaderLibrary.Uniform("atoms", "u_Saturation");
				const int sceneOffsetLocation = m_ShaderLibrary.Uniform("atoms", "u_SceneOffset");
				if (keyDirectionLocation >= 0)
					glUniform3fv(keyDirectionLocation, 1, &globalSettings.lighting.keyDirection.x);
				if (fillDirectionLocation >= 0)
					glUniform3fv(fillDirectionLocation, 1, &globalSettings.lighting.fillDirection.x);
				if (backDirectionLocation >= 0)
					glUniform3fv(backDirectionLocation, 1, &globalSettings.lighting.backDirection.x);
				if (ambientLocation >= 0)
					glUniform1f(ambientLocation, globalSettings.lighting.ambientIntensity);
				if (keyIntensityLocation >= 0)
					glUniform1f(keyIntensityLocation, globalSettings.lighting.keyIntensity);
				if (fillIntensityLocation >= 0)
					glUniform1f(fillIntensityLocation, globalSettings.lighting.fillIntensity);
				if (backIntensityLocation >= 0)
					glUniform1f(backIntensityLocation, globalSettings.lighting.backIntensity);
				if (twoSidedLocation >= 0)
					glUniform1i(twoSidedLocation, globalSettings.lighting.twoSided ? 1 : 0);
				if (cameraPositionLocation >= 0)
				{
					const glm::vec3 cameraPosition = camera.Position();
					glUniform3fv(cameraPositionLocation, 1, &cameraPosition.x);
				}
				if (specularIntensityLocation >= 0)
					glUniform1f(specularIntensityLocation, globalSettings.lighting.specularIntensity);
				if (shininessLocation >= 0)
					glUniform1f(shininessLocation, globalSettings.lighting.shininess);
				if (saturationLocation >= 0)
					glUniform1f(saturationLocation, globalSettings.colorSaturation);
				if (sceneOffsetLocation >= 0)
					glUniform3fv(sceneOffsetLocation, 1, &sceneOffset.x);

				UploadInstanceBuffer(
					m_SphereMesh.instanceVbo, ghostInstances.data(),
					ghostInstances.size() * sizeof(OpenGlAtomInstance));
				glBindVertexArray(m_SphereMesh.vao);
				glDrawElementsInstanced(
					GL_TRIANGLES, m_SphereMesh.indexCount, GL_UNSIGNED_INT, nullptr,
					static_cast<int>(ghostInstances.size()));
				glBindVertexArray(0);
			}
		}
	}

	void OpenGlRendererBackend::renderArrowSegments(const std::vector<ArrowSegment> &segments,
		const RendererViewCamera &camera, const RendererGlobalRenderSettings &globalSettings,
		const glm::vec3 &sceneOffset)
	{
		std::vector<OpenGlBondInstance> shaftInstances, headInstances;
		for (const auto &segment : segments)
		{
			const glm::vec3 axis = segment.tip - segment.base;
			const float length = glm::length(axis);
			if (!std::isfinite(length) || length <= 0.0001f)
				continue;
			const glm::vec3 shaftEnd = segment.tip - axis / length * std::min(0.35f, 0.5f * length);
			OpenGlBondInstance shaft, head;
			shaft.model = buildBondTransform(segment.base, shaftEnd, 0.05f);
			head.model = buildBondTransform(shaftEnd, segment.tip, 0.11f);
			shaft.colorA = shaft.colorB = head.colorA = head.colorB = segment.color;
			shaftInstances.push_back(shaft);
			headInstances.push_back(head);
		}
		const glm::mat4 viewProjection = camera.ProjectionMatrix() * camera.ViewMatrix();
		if (!shaftInstances.empty() || !headInstances.empty())
		{
			const unsigned int program = m_ShaderLibrary.Program("bonds");
			if (program != 0)
			{
#if defined(TRACY_ENABLE)
				TracyGpuZone("Renderer.DisplacementArrows");
#endif
				glUseProgram(program);
				const int viewProjectionLocation = m_ShaderLibrary.Uniform("bonds", "u_ViewProjection");
				if (viewProjectionLocation >= 0)
					glUniformMatrix4fv(viewProjectionLocation, 1, GL_FALSE, &viewProjection[0][0]);
				const int keyDirectionLocation = m_ShaderLibrary.Uniform("bonds", "u_KeyDirection");
				const int fillDirectionLocation = m_ShaderLibrary.Uniform("bonds", "u_FillDirection");
				const int backDirectionLocation = m_ShaderLibrary.Uniform("bonds", "u_BackDirection");
				const int ambientLocation = m_ShaderLibrary.Uniform("bonds", "u_AmbientIntensity");
				const int keyIntensityLocation = m_ShaderLibrary.Uniform("bonds", "u_KeyIntensity");
				const int fillIntensityLocation = m_ShaderLibrary.Uniform("bonds", "u_FillIntensity");
				const int backIntensityLocation = m_ShaderLibrary.Uniform("bonds", "u_BackIntensity");
				const int twoSidedLocation = m_ShaderLibrary.Uniform("bonds", "u_TwoSidedLighting");
				const int cameraPositionLocation = m_ShaderLibrary.Uniform("bonds", "u_CameraPosition");
				const int specularIntensityLocation = m_ShaderLibrary.Uniform("bonds", "u_SpecularIntensity");
				const int shininessLocation = m_ShaderLibrary.Uniform("bonds", "u_Shininess");
				const int saturationLocation = m_ShaderLibrary.Uniform("bonds", "u_Saturation");
				const int specularScaleLocation = m_ShaderLibrary.Uniform("bonds", "u_SpecularScale");
				const int bondRadiusMultiplierLocation = m_ShaderLibrary.Uniform("bonds", "u_BondRadiusMultiplier");
				const int sceneOffsetLocation = m_ShaderLibrary.Uniform("bonds", "u_SceneOffset");
				const int outlineColorLocation = m_ShaderLibrary.Uniform("bonds", "u_OutlineColor");
				if (keyDirectionLocation >= 0)
					glUniform3fv(keyDirectionLocation, 1, &globalSettings.lighting.keyDirection.x);
				if (fillDirectionLocation >= 0)
					glUniform3fv(fillDirectionLocation, 1, &globalSettings.lighting.fillDirection.x);
				if (backDirectionLocation >= 0)
					glUniform3fv(backDirectionLocation, 1, &globalSettings.lighting.backDirection.x);
				if (ambientLocation >= 0)
					glUniform1f(ambientLocation, globalSettings.lighting.ambientIntensity);
				if (keyIntensityLocation >= 0)
					glUniform1f(keyIntensityLocation, globalSettings.lighting.keyIntensity);
				if (fillIntensityLocation >= 0)
					glUniform1f(fillIntensityLocation, globalSettings.lighting.fillIntensity);
				if (backIntensityLocation >= 0)
					glUniform1f(backIntensityLocation, globalSettings.lighting.backIntensity);
				if (twoSidedLocation >= 0)
					glUniform1i(twoSidedLocation, globalSettings.lighting.twoSided ? 1 : 0);
				if (cameraPositionLocation >= 0)
				{
					const glm::vec3 cameraPosition = camera.Position();
					glUniform3fv(cameraPositionLocation, 1, &cameraPosition.x);
				}
				if (specularIntensityLocation >= 0)
					glUniform1f(specularIntensityLocation, globalSettings.lighting.specularIntensity);
				if (shininessLocation >= 0)
					glUniform1f(shininessLocation, globalSettings.lighting.shininess);
				if (saturationLocation >= 0)
					glUniform1f(saturationLocation, globalSettings.colorSaturation);
				if (specularScaleLocation >= 0)
					glUniform1f(specularScaleLocation, 1.0f);
				// Arrow radii above are already absolute world units, not a bond base-radius x
				// multiplier - keep this draw's multiplier neutral regardless of the user's global
				// bond-thickness setting.
				if (bondRadiusMultiplierLocation >= 0)
					glUniform1f(bondRadiusMultiplierLocation, 1.0f);
				if (sceneOffsetLocation >= 0)
					glUniform3fv(sceneOffsetLocation, 1, &sceneOffset.x);
				if (outlineColorLocation >= 0)
					glUniform4fv(outlineColorLocation, 1, &globalSettings.viewport.selectionOutlineColor.x);

				if (!shaftInstances.empty())
				{
					UploadInstanceBuffer(
						m_CylinderMesh.instanceVbo, shaftInstances.data(),
						shaftInstances.size() * sizeof(OpenGlBondInstance));
					glBindVertexArray(m_CylinderMesh.vao);
					glDrawElementsInstanced(
						GL_TRIANGLES, m_CylinderMesh.indexCount, GL_UNSIGNED_INT, nullptr,
						static_cast<int>(shaftInstances.size()));
				}
				if (!headInstances.empty())
				{
					UploadInstanceBuffer(
						m_ConeMesh.instanceVbo, headInstances.data(),
						headInstances.size() * sizeof(OpenGlBondInstance));
					glBindVertexArray(m_ConeMesh.vao);
					glDrawElementsInstanced(
						GL_TRIANGLES, m_ConeMesh.indexCount, GL_UNSIGNED_INT, nullptr,
						static_cast<int>(headInstances.size()));
				}
				glBindVertexArray(0);
			}
		}
	}
}
