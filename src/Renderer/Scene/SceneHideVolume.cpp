#include "Core/dspch.hpp"

#include "Renderer/Scene/SceneHideVolume.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_inverse.hpp>

#include "Renderer/RendererWindowState.hpp"

namespace DefectStudio
{
	namespace
	{
		constexpr float kSingularDeterminantEpsilon = 1.0e-6f;

		struct PreparedHideVolume
		{
			glm::vec3 center = glm::vec3(0.0f);
			glm::mat3 worldToFrame = glm::mat3(1.0f);
			bool valid = true;
		};

		PreparedHideVolume PrepareHideVolume(
			const SceneHideVolume &volume, const glm::mat3 &lattice, const RendererStructureData &structure)
		{
			PreparedHideVolume prepared;
			prepared.center = ResolveHideVolumeCenter(volume, structure).value_or(volume.center);

			if (volume.frame == HideVolumeFrame::Fractional)
			{
				if (std::abs(glm::determinant(lattice)) <= kSingularDeterminantEpsilon)
				{
					prepared.valid = false;
					return prepared;
				}
				prepared.worldToFrame = glm::inverse(lattice);
			}

			return prepared;
		}

		bool PointInPreparedHideVolume(
			const SceneHideVolume &volume, glm::vec3 worldPoint, const PreparedHideVolume &prepared)
		{
			if (!prepared.valid)
				return false;

			const glm::vec3 framePoint = prepared.worldToFrame * worldPoint;
			const glm::vec3 localPoint = glm::transpose(volume.orientation) * (framePoint - prepared.center);
			bool inside = false;
			switch (volume.kind)
			{
			case HideVolumeKind::Sphere:
				inside = glm::dot(localPoint, localPoint) <= volume.halfExtents.x * volume.halfExtents.x;
				break;
			case HideVolumeKind::Box:
				inside = std::abs(localPoint.x) <= volume.halfExtents.x &&
					std::abs(localPoint.y) <= volume.halfExtents.y &&
					std::abs(localPoint.z) <= volume.halfExtents.z;
				break;
			case HideVolumeKind::Cylinder:
				inside = std::abs(localPoint.z) <= volume.halfExtents.z &&
					localPoint.x * localPoint.x + localPoint.y * localPoint.y <=
						volume.halfExtents.x * volume.halfExtents.x;
				break;
			}

			return inside != volume.invert;
		}

		// unit shape -> world is p -> frameToWorld * (center + linear * p).
		struct UnitFrame
		{
			glm::mat3 frameToWorld = glm::mat3(1.0f);
			glm::vec3 center = glm::vec3(0.0f);
			glm::mat3 linear = glm::mat3(1.0f);
		};

		// nullopt in exactly the cases BuildHideVolumeTriangles returns nothing.
		std::optional<UnitFrame> MakeUnitFrame(const SceneHideVolume &volume, const RendererStructureData &structure)
		{
			const PreparedHideVolume prepared = PrepareHideVolume(volume, structure.lattice, structure);
			const glm::vec3 &extents = volume.halfExtents;
			if (!prepared.valid || !(extents.x > 0.0f) ||
				(volume.kind == HideVolumeKind::Box && !(extents.y > 0.0f)) ||
				(volume.kind != HideVolumeKind::Sphere && !(extents.z > 0.0f)))
				return std::nullopt;
			const glm::vec3 scale = volume.kind == HideVolumeKind::Sphere ? glm::vec3(extents.x) :
				volume.kind == HideVolumeKind::Cylinder ? glm::vec3(extents.x, extents.x, extents.z) : extents;
			UnitFrame frame;
			frame.frameToWorld = volume.frame == HideVolumeFrame::Fractional ? structure.lattice : glm::mat3(1.0f);
			frame.center = prepared.center;
			frame.linear = volume.orientation * glm::mat3(scale.x, 0, 0, 0, scale.y, 0, 0, 0, scale.z);
			return frame;
		}
	} // namespace

	std::optional<glm::vec3> ResolveHideVolumeCenter(
		const SceneHideVolume &volume, const RendererStructureData &structure)
	{
		if (volume.frame != HideVolumeFrame::Anchored || volume.anchorAtoms.empty())
			return std::nullopt;

		glm::vec3 center(0.0f);
		std::size_t resolvedCount = 0;
		for (const std::size_t atomIndex : volume.anchorAtoms)
		{
			if (atomIndex < structure.atoms.size())
			{
				center += structure.atoms[atomIndex].cartesianPosition;
				++resolvedCount;
			}
		}

		if (resolvedCount == 0)
			return std::nullopt;
		return center / static_cast<float>(resolvedCount);
	}

	bool PointInHideVolume(
		const SceneHideVolume &volume,
		glm::vec3 worldPoint,
		const glm::mat3 &lattice,
		const RendererStructureData &structure)
	{
		return PointInPreparedHideVolume(volume, worldPoint, PrepareHideVolume(volume, lattice, structure));
	}

	std::vector<std::size_t> AtomsCoveredByHideVolumes(
		const RendererStructureData &structure, std::span<const SceneHideVolume> volumes)
	{
		std::vector<bool> covered(structure.atoms.size(), false);
		for (const SceneHideVolume &volume : volumes)
		{
			if (!volume.active)
				continue;

			const PreparedHideVolume prepared = PrepareHideVolume(volume, structure.lattice, structure);
			for (std::size_t atomIndex = 0; atomIndex < structure.atoms.size(); ++atomIndex)
			{
				if (!covered[atomIndex])
					covered[atomIndex] = PointInPreparedHideVolume(
						volume, structure.atoms[atomIndex].cartesianPosition, prepared);
			}
		}

		std::vector<std::size_t> result;
		for (std::size_t atomIndex = 0; atomIndex < covered.size(); ++atomIndex)
		{
			if (covered[atomIndex])
				result.push_back(atomIndex);
		}
		return result;
	}

	glm::vec3 HideVolumeWorldCenter(const SceneHideVolume &volume, const RendererStructureData &structure)
	{
		const glm::vec3 center = ResolveHideVolumeCenter(volume, structure).value_or(volume.center);
		return volume.frame == HideVolumeFrame::Fractional ? structure.lattice * center : center;
	}

	std::vector<glm::vec3> BuildHideVolumeTriangles(
		const SceneHideVolume &volume, const RendererStructureData &structure)
	{
		const PreparedHideVolume prepared = PrepareHideVolume(volume, structure.lattice, structure);
		const glm::vec3 &extents = volume.halfExtents;
		if (!prepared.valid || !(extents.x > 0.0f) ||
			(volume.kind == HideVolumeKind::Box && !(extents.y > 0.0f)) ||
			(volume.kind != HideVolumeKind::Sphere && !(extents.z > 0.0f)))
			return {};

		const glm::mat3 frameToWorld = volume.frame == HideVolumeFrame::Fractional ? structure.lattice : glm::mat3(1.0f);
		const glm::vec3 scale = volume.kind == HideVolumeKind::Sphere ? glm::vec3(extents.x) :
			volume.kind == HideVolumeKind::Cylinder ? glm::vec3(extents.x, extents.x, extents.z) : extents;
		const glm::mat3 transform = frameToWorld * volume.orientation;
		const bool reflected = glm::determinant(transform) < 0.0f;
		std::vector<glm::vec3> mesh;
		const auto world = [&](const glm::vec3 &local) {
			return frameToWorld * (prepared.center + volume.orientation * (scale * local));
		};
		const auto triangle = [&](const glm::vec3 &a, const glm::vec3 &b, const glm::vec3 &c) {
			mesh.push_back(world(a));
			mesh.push_back(world(reflected ? c : b));
			mesh.push_back(world(reflected ? b : c));
		};
		if (volume.kind == HideVolumeKind::Sphere)
		{
			constexpr int slices = 24, stacks = 12;
			mesh.reserve(slices * (stacks - 1) * 6);
			const auto point = [](int slice, int stack) {
				if (stack == 0) return glm::vec3(0, 0, 1);
				if (stack == stacks) return glm::vec3(0, 0, -1);
				const float theta = glm::two_pi<float>() * static_cast<float>(slice) / slices;
				const float phi = glm::pi<float>() * static_cast<float>(stack) / stacks;
				return glm::vec3(std::sin(phi) * std::cos(theta), std::sin(phi) * std::sin(theta), std::cos(phi));
			};
			for (int stack = 0; stack < stacks; ++stack)
				for (int slice = 0; slice < slices; ++slice)
				{
					const auto a = point(slice, stack), b = point(slice, stack + 1);
					const auto c = point(slice + 1, stack + 1), d = point(slice + 1, stack);
					if (stack != stacks - 1) triangle(a, b, c);
					if (stack != 0) triangle(a, c, d);
				}
		}
		else if (volume.kind == HideVolumeKind::Box)
		{
			mesh.reserve(36);
			for (int axis = 0; axis < 3; ++axis)
				for (const float side : {-1.0f, 1.0f})
				{
					glm::vec3 normal(0), u(0), v(0);
					normal[axis] = side;
					u[(axis + 1) % 3] = 1.0f;
					v[(axis + 2) % 3] = side;
					const auto a = normal - u - v, b = normal + u - v;
					const auto c = normal + u + v, d = normal - u + v;
					triangle(a, b, c);
					triangle(a, c, d);
				}
		}
		else if (volume.kind == HideVolumeKind::Cylinder)
		{
			constexpr int segments = 32;
			mesh.reserve(segments * 12);
			for (int segment = 0; segment < segments; ++segment)
			{
				const float a = glm::two_pi<float>() * static_cast<float>(segment) / segments;
				const float b = glm::two_pi<float>() * static_cast<float>(segment + 1) / segments;
				const glm::vec3 lowerA(std::cos(a), std::sin(a), -1), lowerB(std::cos(b), std::sin(b), -1);
				const glm::vec3 upperA(std::cos(a), std::sin(a), 1), upperB(std::cos(b), std::sin(b), 1);
				triangle(lowerA, lowerB, upperB);
				triangle(lowerA, upperB, upperA);
				triangle(glm::vec3(0, 0, -1), lowerB, lowerA);
				triangle(glm::vec3(0, 0, 1), upperA, upperB);
			}
		}
		return mesh;
	}

	void ApplyHideVolumeMaskToWindowState(RendererWindowState &windowState)
	{
		RendererStructureData &structure = windowState.structure;
		std::vector<bool> covered(structure.atoms.size(), false);
		for (const std::size_t atomIndex : AtomsCoveredByHideVolumes(structure, windowState.sceneHideVolumes))
		{
			covered[atomIndex] = true;
			structure.atoms[atomIndex].visible = false;
			structure.atoms[atomIndex].renderable = false;
		}
		for (RendererBondData &bond : structure.bonds)
		{
			const bool endpointCovered = (bond.firstAtomIndex < covered.size() && covered[bond.firstAtomIndex]) ||
				(bond.secondAtomIndex < covered.size() && covered[bond.secondAtomIndex]);
			if (!endpointCovered)
				continue;
			bond.visible = false;
			bond.renderable = false;
		}
	}

	bool PointCoveredByHideVolumes(
		std::span<const SceneHideVolume> volumes, const glm::vec3 worldPoint, const RendererStructureData &structure)
	{
		return std::any_of(volumes.begin(), volumes.end(), [&](const SceneHideVolume &volume) {
			return volume.active && PointInHideVolume(volume, worldPoint, structure.lattice, structure);
		});
	}

	std::optional<glm::mat4> HideVolumeWorldToUnit(const SceneHideVolume &volume, const RendererStructureData &structure)
	{
		const std::optional<UnitFrame> frame = MakeUnitFrame(volume, structure);
		if (!frame)
			return std::nullopt;
		// unit -> world is p -> frameToWorld * (center + linear * p); invert the affine map.
		// MakeUnitFrame already refused a singular lattice and non-positive extents.
		glm::mat4 forward(frame->frameToWorld * frame->linear);
		forward[3] = glm::vec4(frame->frameToWorld * frame->center, 1.0f);
		return glm::inverse(forward);
	}

	std::vector<glm::vec3> BuildHideVolumeEdges(const SceneHideVolume &volume, const RendererStructureData &structure)
	{
		const std::optional<UnitFrame> frame = MakeUnitFrame(volume, structure);
		if (!frame)
			return {};
		std::vector<glm::vec3> lines;
		const auto world = [&](const glm::vec3 &unit) { return frame->frameToWorld * (frame->center + frame->linear * unit); };
		const auto segment = [&](const glm::vec3 &a, const glm::vec3 &b) {
			lines.push_back(world(a));
			lines.push_back(world(b));
		};
		// A unit circle in the plane spanned by unit axes `u` and `v`, lifted by `offset`.
		const auto circle = [&](const glm::vec3 &u, const glm::vec3 &v, const glm::vec3 &offset) {
			constexpr int segments = 48;
			for (int index = 0; index < segments; ++index)
			{
				const float a = glm::two_pi<float>() * static_cast<float>(index) / segments;
				const float b = glm::two_pi<float>() * static_cast<float>(index + 1) / segments;
				segment(offset + std::cos(a) * u + std::sin(a) * v, offset + std::cos(b) * u + std::sin(b) * v);
			}
		};
		const glm::vec3 x(1, 0, 0), y(0, 1, 0), z(0, 0, 1);
		switch (volume.kind)
		{
		case HideVolumeKind::Sphere:
			circle(x, y, glm::vec3(0.0f));
			circle(y, z, glm::vec3(0.0f));
			circle(z, x, glm::vec3(0.0f));
			break;
		case HideVolumeKind::Box:
			for (int axis = 0; axis < 3; ++axis)
				for (const float s1 : {-1.0f, 1.0f})
					for (const float s2 : {-1.0f, 1.0f})
					{
						glm::vec3 a(0.0f);
						a[(axis + 1) % 3] = s1;
						a[(axis + 2) % 3] = s2;
						glm::vec3 b = a;
						a[axis] = -1.0f;
						b[axis] = 1.0f;
						segment(a, b);
					}
			break;
		case HideVolumeKind::Cylinder:
			circle(x, y, -z);
			circle(x, y, z);
			for (const glm::vec3 &rim : {x, -x, y, -y})
				segment(rim - z, rim + z);
			break;
		}
		return lines;
	}
} // namespace DefectStudio
