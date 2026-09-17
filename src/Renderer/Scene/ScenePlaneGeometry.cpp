#include "Core/dspch.hpp"

#include "Renderer/Scene/ScenePlaneGeometry.hpp"

#include <algorithm>
#include <cmath>

namespace DefectStudio
{
	namespace
	{
		// Eigen-decomposition of a 3x3 symmetric matrix by cyclic Jacobi rotations. glm has no
		// eigensolver and the analytic closed form for a symmetric 3x3 loses badly to cancellation
		// exactly where it matters here - three nearly collinear atoms - so this is the boring,
		// correct option. Eight sweeps is far past convergence for a matrix this small.
		struct Eigen
		{
			glm::vec3 values = glm::vec3(0.0f);
			glm::mat3 vectors = glm::mat3(1.0f);
		};

		[[nodiscard]] Eigen SymmetricEigen(glm::mat3 matrix)
		{
			Eigen result;
			for (int sweep = 0; sweep < 8; ++sweep)
			{
				float offDiagonal = 0.0f;
				for (int p = 0; p < 3; ++p)
					for (int q = p + 1; q < 3; ++q)
						offDiagonal += matrix[q][p] * matrix[q][p];
				if (offDiagonal < 1e-20f)
					break;

				for (int p = 0; p < 3; ++p)
				{
					for (int q = p + 1; q < 3; ++q)
					{
						if (std::abs(matrix[q][p]) < 1e-20f)
							continue;
						const float theta = (matrix[q][q] - matrix[p][p]) / (2.0f * matrix[q][p]);
						const float sign = theta >= 0.0f ? 1.0f : -1.0f;
						const float t = sign / (std::abs(theta) + std::sqrt(theta * theta + 1.0f));
						const float c = 1.0f / std::sqrt(t * t + 1.0f);
						const float s = t * c;

						glm::mat3 rotation(1.0f);
						rotation[p][p] = c;
						rotation[q][q] = c;
						rotation[q][p] = s;
						rotation[p][q] = -s;
						matrix = glm::transpose(rotation) * matrix * rotation;
						result.vectors = result.vectors * rotation;
					}
				}
			}
			result.values = glm::vec3(matrix[0][0], matrix[1][1], matrix[2][2]);
			return result;
		}

		// Any unit vector perpendicular to `axis`. Picks the world axis least aligned with it, so
		// the cross product never lands on a near-zero vector.
		[[nodiscard]] glm::vec3 AnyPerpendicular(const glm::vec3 &axis)
		{
			const glm::vec3 helper = std::abs(axis.x) < 0.9f ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
			return glm::normalize(glm::cross(axis, helper));
		}
	} // namespace

	std::optional<ScenePlaneFit> FitScenePlane(
		const std::vector<glm::vec3> &points, const glm::vec3 &viewDirection)
	{
		std::vector<glm::vec3> usable;
		usable.reserve(points.size());
		for (const glm::vec3 &point : points)
			if (std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z))
				usable.push_back(point);
		if (usable.empty())
			return std::nullopt;

		glm::vec3 centroid(0.0f);
		for (const glm::vec3 &point : usable)
			centroid += point;
		centroid /= static_cast<float>(usable.size());

		glm::mat3 covariance(0.0f);
		float spread = 0.0f;
		for (const glm::vec3 &point : usable)
		{
			const glm::vec3 offset = point - centroid;
			spread = std::max(spread, glm::length(offset));
			covariance += glm::outerProduct(offset, offset);
		}
		// One point, or several stacked on top of each other: there is nothing to orient a plane
		// by, and inventing one would be a plane the user did not ask for.
		if (spread <= 1e-5f)
			return std::nullopt;

		const Eigen eigen = SymmetricEigen(covariance);
		int smallest = 0;
		int largest = 0;
		for (int i = 1; i < 3; ++i)
		{
			if (eigen.values[i] < eigen.values[smallest])
				smallest = i;
			if (eigen.values[i] > eigen.values[largest])
				largest = i;
		}
		const int middle = 3 - smallest - largest;

		ScenePlaneFit fit;
		fit.center = centroid;

		const glm::vec3 lineDirection = glm::normalize(glm::vec3(eigen.vectors[largest]));
		// Two points, or points strung out along a line: the spread perpendicular to that line is
		// nothing, so the covariance cannot pick a plane and the answer is the one plane through
		// the points that the camera is looking straight at. Anything else is drawn edge-on.
		const bool underDetermined = eigen.values[middle] <= 1e-4f * eigen.values[largest];
		if (underDetermined)
		{
			const glm::vec3 towardViewer = -viewDirection;
			glm::vec3 normal = towardViewer - glm::dot(towardViewer, lineDirection) * lineDirection;
			fit.normal = glm::length(normal) > 1e-4f ? glm::normalize(normal) : AnyPerpendicular(lineDirection);
			fit.tangent = lineDirection;
		}
		else
		{
			fit.normal = glm::normalize(glm::vec3(eigen.vectors[smallest]));
			fit.tangent = lineDirection;
		}

		// Face the viewer. A plane presenting its back is the same plane, but the sign of `normal`
		// is what the quad's winding and the properties panel both read, so it may as well be the
		// one the user is looking at.
		if (glm::dot(fit.normal, viewDirection) > 0.0f)
			fit.normal = -fit.normal;

		// Re-orthogonalise: the two eigenvectors are perpendicular in exact arithmetic, not
		// necessarily after eight Jacobi sweeps in float.
		fit.tangent = fit.tangent - glm::dot(fit.tangent, fit.normal) * fit.normal;
		fit.tangent = glm::length(fit.tangent) > 1e-4f ? glm::normalize(fit.tangent) : AnyPerpendicular(fit.normal);

		const glm::vec3 bitangent = glm::cross(fit.normal, fit.tangent);
		glm::vec2 halfExtents(0.0f);
		for (const glm::vec3 &point : usable)
		{
			const glm::vec3 offset = point - fit.center;
			halfExtents.x = std::max(halfExtents.x, std::abs(glm::dot(offset, fit.tangent)));
			halfExtents.y = std::max(halfExtents.y, std::abs(glm::dot(offset, bitangent)));
		}
		// A margin so the quad frames what was picked instead of ending exactly at the outermost
		// atom's centre, and a floor so a plane through two points is a sheet rather than a ribbon.
		constexpr float kMargin = 1.35f;
		const float floorExtent = 0.35f * std::max(halfExtents.x, spread);
		fit.halfExtents = glm::vec2(
			std::max(halfExtents.x * kMargin, floorExtent), std::max(halfExtents.y * kMargin, floorExtent));
		return fit;
	}

	std::array<glm::vec3, 4> ScenePlaneCorners(const RendererWindowState::ScenePlane &plane)
	{
		const glm::vec3 bitangent = glm::cross(plane.normal, plane.tangent);
		const glm::vec3 alongTangent = plane.tangent * plane.halfExtents.x;
		const glm::vec3 alongBitangent = bitangent * plane.halfExtents.y;
		return {
			plane.center - alongTangent - alongBitangent,
			plane.center + alongTangent - alongBitangent,
			plane.center + alongTangent + alongBitangent,
			plane.center - alongTangent + alongBitangent};
	}

	RendererWindowState::ScenePlane MakeScenePlane(const ScenePlaneFit &fit)
	{
		RendererWindowState::ScenePlane plane;
		plane.center = fit.center;
		plane.normal = fit.normal;
		plane.tangent = fit.tangent;
		plane.halfExtents = fit.halfExtents;
		// id stays unset: the caller allocates it from the window's SceneRegistry, the same way
		// every other scene object is created.
		return plane;
	}
} // namespace DefectStudio
