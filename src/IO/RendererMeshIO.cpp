#include "Core/dspch.hpp"

#include "IO/RendererMeshIO.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <sstream>
#include <unordered_map>

#include "IO/TextFileIO.hpp"

namespace DefectStudio
{
	struct ObjVertexKey
	{
		int positionIndex = -1;
		int normalIndex = -1;

		[[nodiscard]] bool operator==(const ObjVertexKey &other) const
		{
			return positionIndex == other.positionIndex && normalIndex == other.normalIndex;
		}
	};

	struct ObjVertexKeyHasher
	{
		[[nodiscard]] std::size_t operator()(const ObjVertexKey &key) const noexcept
		{
			const std::size_t left = static_cast<std::size_t>(static_cast<std::uint32_t>(key.positionIndex + 131));
			const std::size_t right = static_cast<std::size_t>(static_cast<std::uint32_t>(key.normalIndex + 257));
			return (left * 73856093u) ^ (right * 19349663u);
		}
	};

	struct ObjFaceVertex
	{
		int positionIndex = -1;
		int normalIndex = -1;
	};

	namespace
	{
		[[nodiscard]] bool ParseIntToken(std::string_view token, int &outValue)
		{
			const char *begin = token.data();
			const char *end = token.data() + token.size();
			int value = 0;
			const std::from_chars_result parseResult = std::from_chars(begin, end, value);
			if (parseResult.ec != std::errc() || parseResult.ptr != end)
				return false;
			outValue = value;
			return true;
		}

		[[nodiscard]] bool ParseFloatToken(std::string_view token, float &outValue)
		{
			const char *begin = token.data();
			const char *end = token.data() + token.size();
			float value = 0.0f;
			const std::from_chars_result parseResult = std::from_chars(begin, end, value);
			if (parseResult.ec != std::errc() || parseResult.ptr != end)
				return false;
			outValue = value;
			return true;
		}

		[[nodiscard]] bool ResolveObjIndex(int rawIndex, int count, int &outIndex)
		{
			if (count <= 0)
				return false;
			if (rawIndex > 0)
			{
				const int zeroBased = rawIndex - 1;
				if (zeroBased < 0 || zeroBased >= count)
					return false;
				outIndex = zeroBased;
				return true;
			}

			if (rawIndex < 0)
			{
				const int fromBack = count + rawIndex;
				if (fromBack < 0 || fromBack >= count)
					return false;
				outIndex = fromBack;
				return true;
			}

			return false;
		}

		[[nodiscard]] bool ParseFaceVertexToken(
			std::string_view token,
			ObjFaceVertex &outVertex)
		{
			outVertex = ObjFaceVertex{};
			if (token.empty())
				return false;

			const std::size_t firstSlash = token.find('/');
			if (firstSlash == std::string_view::npos)
			{
				return ParseIntToken(token, outVertex.positionIndex);
			}

			const std::size_t secondSlash = token.find('/', firstSlash + 1);
			const std::string_view positionToken = token.substr(0, firstSlash);
			if (!ParseIntToken(positionToken, outVertex.positionIndex))
				return false;

			if (secondSlash == std::string_view::npos)
				return true;

			const std::string_view normalToken = token.substr(secondSlash + 1);
			if (normalToken.empty())
				return true;

			return ParseIntToken(normalToken, outVertex.normalIndex);
		}

		[[nodiscard]] glm::vec3 SafeNormalize(const glm::vec3 &value)
		{
			const float length = glm::length(value);
			if (!std::isfinite(length) || length <= 0.00001f)
				return glm::vec3(0.0f, 1.0f, 0.0f);
			return value / length;
		}
	} // namespace

	bool RendererMeshIO::LoadObjFromFile(
		const Path &path,
		RendererStaticMeshData &outMesh,
		std::string &outError,
		bool generateGradientFromZ)
	{
		std::string text;
		if (!TextFileIO::Load(path, text, outError))
			return false;
		return ParseObj(text, outMesh, outError, generateGradientFromZ);
	}

	bool RendererMeshIO::ParseObj(
		std::string_view objText,
		RendererStaticMeshData &outMesh,
		std::string &outError,
		bool generateGradientFromZ)
	{
		std::vector<glm::vec3> rawPositions;
		std::vector<glm::vec3> rawNormals;
		std::unordered_map<ObjVertexKey, std::uint32_t, ObjVertexKeyHasher> vertexMap;

		outMesh.positions.clear();
		outMesh.normals.clear();
		outMesh.gradientT.clear();
		outMesh.indices.clear();

		std::istringstream stream{std::string(objText)};
		std::string line;
		int lineNumber = 0;
		while (std::getline(stream, line))
		{
			++lineNumber;
			const std::size_t commentPosition = line.find('#');
			if (commentPosition != std::string::npos)
				line = line.substr(0, commentPosition);
			if (line.empty())
				continue;

			std::istringstream lineStream(line);
			std::string keyword;
			lineStream >> keyword;
			if (keyword == "v")
			{
				std::string xToken;
				std::string yToken;
				std::string zToken;
				lineStream >> xToken >> yToken >> zToken;
				float x = 0.0f;
				float y = 0.0f;
				float z = 0.0f;
				if (!ParseFloatToken(xToken, x) || !ParseFloatToken(yToken, y) || !ParseFloatToken(zToken, z))
				{
					outError = "Invalid OBJ vertex at line " + std::to_string(lineNumber);
					return false;
				}
				rawPositions.emplace_back(x, y, z);
				continue;
			}

			if (keyword == "vn")
			{
				std::string xToken;
				std::string yToken;
				std::string zToken;
				lineStream >> xToken >> yToken >> zToken;
				float x = 0.0f;
				float y = 0.0f;
				float z = 0.0f;
				if (!ParseFloatToken(xToken, x) || !ParseFloatToken(yToken, y) || !ParseFloatToken(zToken, z))
				{
					outError = "Invalid OBJ normal at line " + std::to_string(lineNumber);
					return false;
				}
				rawNormals.emplace_back(x, y, z);
				continue;
			}

			if (keyword != "f")
				continue;

			std::vector<ObjFaceVertex> faceVertices;
			std::string token;
			while (lineStream >> token)
			{
				ObjFaceVertex faceVertex;
				if (!ParseFaceVertexToken(token, faceVertex))
				{
					outError = "Invalid OBJ face token '" + token + "' at line " + std::to_string(lineNumber);
					return false;
				}

				int resolvedPositionIndex = -1;
				if (!ResolveObjIndex(faceVertex.positionIndex, static_cast<int>(rawPositions.size()), resolvedPositionIndex))
				{
					outError = "OBJ face position index out of range at line " + std::to_string(lineNumber);
					return false;
				}

				int resolvedNormalIndex = -1;
				if (faceVertex.normalIndex != -1)
				{
					if (!ResolveObjIndex(faceVertex.normalIndex, static_cast<int>(rawNormals.size()), resolvedNormalIndex))
					{
						outError = "OBJ face normal index out of range at line " + std::to_string(lineNumber);
						return false;
					}
				}

				faceVertices.push_back(ObjFaceVertex{resolvedPositionIndex, resolvedNormalIndex});
			}

			if (faceVertices.size() < 3)
				continue;

			for (std::size_t index = 1; index + 1 < faceVertices.size(); ++index)
			{
				const ObjFaceVertex triangle[3] = {
					faceVertices[0],
					faceVertices[index],
					faceVertices[index + 1]};

				for (const ObjFaceVertex &vertex : triangle)
				{
					const ObjVertexKey key{vertex.positionIndex, vertex.normalIndex};
					const auto found = vertexMap.find(key);
					if (found != vertexMap.end())
					{
						outMesh.indices.push_back(found->second);
						continue;
					}

					const glm::vec3 position = rawPositions[static_cast<std::size_t>(vertex.positionIndex)];
					const glm::vec3 normal = vertex.normalIndex >= 0
						? SafeNormalize(rawNormals[static_cast<std::size_t>(vertex.normalIndex)])
						: SafeNormalize(position);
					const std::uint32_t newIndex = static_cast<std::uint32_t>(outMesh.positions.size());
					outMesh.positions.push_back(position);
					outMesh.normals.push_back(normal);
					vertexMap.emplace(key, newIndex);
					outMesh.indices.push_back(newIndex);
				}
			}
		}

		if (outMesh.positions.empty() || outMesh.indices.empty())
		{
			outError = "OBJ mesh has no triangulated geometry";
			return false;
		}

		if (generateGradientFromZ)
		{
			float minZ = outMesh.positions.front().z;
			float maxZ = outMesh.positions.front().z;
			for (const glm::vec3 &position : outMesh.positions)
			{
				minZ = std::min(minZ, position.z);
				maxZ = std::max(maxZ, position.z);
			}

			outMesh.gradientT.resize(outMesh.positions.size(), 0.0f);
			const float span = maxZ - minZ;
			if (span > 0.00001f)
			{
				for (std::size_t index = 0; index < outMesh.positions.size(); ++index)
				{
					const float value = (outMesh.positions[index].z - minZ) / span;
					outMesh.gradientT[index] = std::clamp(value, 0.0f, 1.0f);
				}
			}
		}

		return true;
	}
} // namespace DefectStudio
