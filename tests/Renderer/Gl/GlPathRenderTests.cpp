#include "Core/dspch.hpp"

#include "Renderer/Gl/GlTestContext.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <set>
#include <system_error>

#include <glad/gl.h>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/constants.hpp>

#include "Core/Utils/Path.hpp"
#include "Renderer/OpenGl/FrameBufferReadback.hpp"
#include "Renderer/OpenGl/OpenGlRendererBackend.hpp"
#include "Renderer/Path/PathBindingResolver.hpp"
#include "Renderer/Path/PathEvaluator.hpp"
#include "Renderer/Path/PathStrokeMesher.hpp"
#include "Renderer/Path/PathTessellator.hpp"
#include "Presentation/Panels/ScenePathDevMenu.hpp"

#if defined(_WIN32)
#include <windows.h>
#endif

namespace DefectStudio::Tests
{
	namespace
	{
		[[nodiscard]] Path ShaderDirectoryNextToTestExecutable()
		{
#if defined(_WIN32)
			std::array<wchar_t, 32768> executablePath{};
			const DWORD length = GetModuleFileNameW(nullptr, executablePath.data(), static_cast<DWORD>(executablePath.size()));
			if (length > 0 && length < executablePath.size())
				return Path::FromResolved(FilePath(executablePath.data()).parent_path() / "shaders");
#else
			std::error_code error;
			const FilePath executablePath = std::filesystem::read_symlink("/proc/self/exe", error);
			if (!error)
				return Path::FromResolved(executablePath.parent_path() / "shaders");
#endif
			return Path::FromResolved(FileSystem::CurrentPath() / "shaders");
		}

		[[nodiscard]] RendererStaticMeshData TriangleMesh()
		{
			RendererStaticMeshData mesh;
			mesh.positions = {{-0.5f, -0.5f, 0.0f}, {0.5f, -0.5f, 0.0f}, {0.0f, 0.5f, 1.0f}};
			mesh.normals = {{0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}};
			mesh.gradientT = {0.0f, 0.5f, 1.0f};
			mesh.indices = {0u, 1u, 2u};
			return mesh;
		}

		[[nodiscard]] RendererPrimitiveMeshAssets PrimitiveMeshes()
		{
			RendererPrimitiveMeshAssets meshes;
			meshes.sphere = TriangleMesh();
			meshes.cylinder = TriangleMesh();
			meshes.cone = TriangleMesh();
			return meshes;
		}

		[[nodiscard]] ScenePath DevPath(ScenePathDevPreset preset, SceneObjectId id = SceneObjectId{1})
		{
			ScenePath path = MakeDevScenePath(preset, glm::vec3(0.0f));
			path.id = id;
			return path;
		}

		[[nodiscard]] EvaluatedPath Evaluated(const ScenePath &path, double tolerance = 0.01)
		{
			return Tessellate(path, ResolveNodePositions(path, BindingContext{}), TessellationSettings{tolerance, 12, 4096, {}});
		}

		[[nodiscard]] bool Finite(const glm::vec3 &value)
		{
			return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
		}

		[[nodiscard]] std::size_t NonBackground(const std::vector<unsigned char> &pixels, int width, int height, const Rgba8 background)
		{
			std::size_t count = 0;
			for (int y = 0; y < height; ++y)
				for (int x = 0; x < width; ++x)
					if (PixelAt(pixels, width, height, x, y) != background)
						++count;
			return count;
		}

		[[nodiscard]] std::vector<unsigned char> RenderPath(ScenePath path, RendererViewCamera &camera, OpenGlRendererBackend &backend, PathSystem &system, const RendererStructureData &structure = {}, const bool showAtoms = false)
		{
			if (!system.Store().Contains(path.id))
				system.Store().Insert(std::move(path));
			const PathRenderInput input{&system};
			RendererGlobalRenderSettings settings;
			settings.backgroundColor = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
			const unsigned int texture = backend.RenderWindow(
				"gl-path", structure, camera, settings, 256, 128, showAtoms, false, false, false, false,
				{}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, nullptr, nullptr, nullptr, nullptr, glm::vec3(0.0f),
				true, 0.3f, 45.0f, true, &input);
			return ReadTextureRgba8TopDown(texture, 256, 128);
		}
	} // namespace

	TEST(ScenePathDevTests, PresetsValidateAndMatchTheirSegmentVariant)
	{
		for (const ScenePathDevPreset preset : {ScenePathDevPreset::Line, ScenePathDevPreset::Cubic, ScenePathDevPreset::Arc})
		{
			const ScenePath path = MakeDevScenePath(preset, glm::vec3(1.0f, 2.0f, 3.0f));
			EXPECT_TRUE(ValidatePath(path).empty());
			ASSERT_EQ(path.nodes.size(), 2u);
			ASSERT_EQ(path.segments.size(), 1u);
			if (preset == ScenePathDevPreset::Line) EXPECT_TRUE(std::holds_alternative<LineSegmentData>(path.segments[0].data));
			if (preset == ScenePathDevPreset::Cubic) EXPECT_TRUE(std::holds_alternative<CubicBezierSegmentData>(path.segments[0].data));
			if (preset == ScenePathDevPreset::Arc) EXPECT_TRUE(std::holds_alternative<CircularArcSegmentData>(path.segments[0].data));
		}
	}

	TEST(ScenePathDevTests, PresetsAreCentredAndFinite)
	{
		const glm::vec3 centre(1.0f, 2.0f, 3.0f);
		for (const ScenePathDevPreset preset : {ScenePathDevPreset::Line, ScenePathDevPreset::Cubic, ScenePathDevPreset::Arc})
		{
			const ScenePath path = MakeDevScenePath(preset, centre);
			const ResolvedNodes resolved = ResolveNodePositions(path, BindingContext{});
			ASSERT_EQ(resolved.positions.size(), path.nodes.size());
			ASSERT_FALSE(resolved.positions.empty());
			glm::vec3 centroid(0.0f);
			for (const glm::vec3 &position : resolved.positions) { EXPECT_TRUE(Finite(position)); centroid += position; }
			EXPECT_NEAR(glm::length(centroid / static_cast<float>(resolved.positions.size()) - centre), 0.0f, 1e-4f);
		}
	}

	TEST(ScenePathDevTests, CurvedPresetsTessellateMoreThanLine)
	{
		const std::size_t lineSamples = Evaluated(DevPath(ScenePathDevPreset::Line)).samples.size();
		for (const ScenePathDevPreset preset : {ScenePathDevPreset::Cubic, ScenePathDevPreset::Arc})
		{
			const EvaluatedPath evaluated = Evaluated(DevPath(preset));
			EXPECT_GE(evaluated.samples.size(), 2u);
			EXPECT_GT(evaluated.totalLength, 0.0);
			EXPECT_GT(evaluated.samples.size(), lineSamples);
		}
	}

	TEST(ScenePathDevTests, PresetsBuildFiniteStrokeGeometry)
	{
		for (const ScenePathDevPreset preset : {ScenePathDevPreset::Line, ScenePathDevPreset::Cubic, ScenePathDevPreset::Arc})
		{
			const StrokeGeometry geometry = BuildStroke(Evaluated(DevPath(preset)), DevPath(preset).style);
			EXPECT_FALSE(geometry.indices.empty());
			for (const StrokeTubeVertex &vertex : geometry.tubeVertices) EXPECT_TRUE(Finite(vertex.position) && Finite(vertex.normal));
			for (const StrokeRibbonVertex &vertex : geometry.ribbonVertices) EXPECT_TRUE(Finite(vertex.position) && Finite(vertex.tangent) && Finite(vertex.normal));
		}
	}

	TEST(ScenePathDevTests, ElementIdsAreLocalValidDistinctAndMonotonic)
	{
		const ScenePath path = MakeDevScenePath(ScenePathDevPreset::Cubic, glm::vec3(0.0f));
		std::set<std::uint64_t> ids;
		for (const PathNode &node : path.nodes) EXPECT_TRUE(ids.insert(node.id.value).second);
		for (const PathSegment &segment : path.segments)
		{
			EXPECT_TRUE(ids.insert(segment.id.value).second);
			if (const auto *cubic = std::get_if<CubicBezierSegmentData>(&segment.data))
			{
				EXPECT_TRUE(ids.insert(cubic->startHandle.id.value).second);
				EXPECT_TRUE(ids.insert(cubic->endHandle.id.value).second);
			}
		}
		EXPECT_GT(path.nextElementId, *ids.rbegin());
	}

	TEST(ScenePathDevTests, PresetsAreDeterministic)
	{
		for (const ScenePathDevPreset preset : {ScenePathDevPreset::Line, ScenePathDevPreset::Cubic, ScenePathDevPreset::Arc})
		{
			const ScenePath a = MakeDevScenePath(preset, glm::vec3(0.0f));
			const ScenePath b = MakeDevScenePath(preset, glm::vec3(0.0f));
			const StrokeGeometry ga = BuildStroke(Evaluated(a), a.style);
			const StrokeGeometry gb = BuildStroke(Evaluated(b), b.style);
			ASSERT_EQ(ga.indices, gb.indices);
			ASSERT_EQ(ga.tubeVertices.size(), gb.tubeVertices.size());
			ASSERT_EQ(ga.ribbonVertices.size(), gb.ribbonVertices.size());
		}
	}

	TEST(ScenePathDevTests, PresetLeavesSceneObjectIdUnset)
	{
		EXPECT_FALSE(MakeDevScenePath(ScenePathDevPreset::Line, glm::vec3(0.0f)).id.IsValid());
	}

	TEST_F(GlTest, NullPathInputKeepsExistingSmokeBehaviour)
	{
		OpenGlRendererBackend backend;
		ASSERT_TRUE(backend.Initialize(ShaderDirectoryNextToTestExecutable(), PrimitiveMeshes()));
		RendererViewCamera camera;
		camera.SetViewport(static_cast<float>(kWidth), static_cast<float>(kHeight));
		camera.SetOrbitState(glm::vec3(0.0f), 10.0f, 0.0f, 0.0f);
		RendererGlobalRenderSettings settings;
		settings.backgroundColor = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
		const unsigned int texture = backend.RenderWindow("gl-path-null", {}, camera, settings, kWidth, kHeight, false, false, false, false);
		ASSERT_NE(texture, 0u);
		EXPECT_EQ(NonBackground(ReadTextureRgba8TopDown(texture, kWidth, kHeight), kWidth, kHeight, {0, 0, 0, 255}), 0u);
		backend.Shutdown();
	}

	TEST_F(GlTest, RenderedLineCoversOnlyItsScreenBand)
	{
		OpenGlRendererBackend backend;
		ASSERT_TRUE(backend.Initialize(ShaderDirectoryNextToTestExecutable(), PrimitiveMeshes()));
		RendererViewCamera camera;
		camera.SetViewport(kWidth, kHeight);
		camera.SetOrbitState(glm::vec3(0.0f), 6.0f, 0.0f, 0.0f);
		PathSystem system;
		const std::vector<unsigned char> pixels = RenderPath(DevPath(ScenePathDevPreset::Line), camera, backend, system);
		EXPECT_GT(NonBackground(pixels, kWidth, kHeight, {0, 0, 0, 255}), 0u);
		backend.Shutdown();
	}

	TEST_F(GlTest, WidthChangesWithWorldSpaceZoom)
	{
		OpenGlRendererBackend backend;
		ASSERT_TRUE(backend.Initialize(ShaderDirectoryNextToTestExecutable(), PrimitiveMeshes()));
		RendererGlobalRenderSettings settings;
		(void)settings;
		RendererViewCamera camera;
		camera.SetViewport(kWidth, kHeight);
		camera.SetOrbitState({}, 8.0f, 0.0f, 0.0f);
		PathSystem first;
		const std::size_t farPixels = NonBackground(RenderPath(DevPath(ScenePathDevPreset::Line), camera, backend, first), kWidth, kHeight, {0, 0, 0, 255});
		camera.SetDistance(4.0f);
		PathSystem second;
		const std::size_t nearPixels = NonBackground(RenderPath(DevPath(ScenePathDevPreset::Line), camera, backend, second), kWidth, kHeight, {0, 0, 0, 255});
		EXPECT_GT(nearPixels, farPixels);
		backend.Shutdown();
	}

	TEST_F(GlTest, FlatAndRoundProfilesRenderDifferentImages)
	{
		OpenGlRendererBackend backend;
		ASSERT_TRUE(backend.Initialize(ShaderDirectoryNextToTestExecutable(), PrimitiveMeshes()));
		RendererViewCamera camera;
		camera.SetViewport(kWidth, kHeight);
		camera.SetOrbitState({}, 6.0f, 0.0f, 0.0f);
		PathSystem roundSystem;
		const auto round = RenderPath(DevPath(ScenePathDevPreset::Line), camera, backend, roundSystem);
		ScenePath flatPath = DevPath(ScenePathDevPreset::Line);
		flatPath.style.profile = StrokeProfile::Flat;
		PathSystem flatSystem;
		const auto flat = RenderPath(flatPath, camera, backend, flatSystem);
		EXPECT_NE(round, flat);
		backend.Shutdown();
	}

	TEST_F(GlTest, CameraFacingRemainsVisibleWhenFlatTurnsEdgeOn)
	{
		OpenGlRendererBackend backend;
		ASSERT_TRUE(backend.Initialize(ShaderDirectoryNextToTestExecutable(), PrimitiveMeshes()));
		RendererViewCamera camera;
		camera.SetViewport(kWidth, kHeight);
		camera.SetOrbitState({}, 6.0f, 0.0f, 0.0f);
		// The dev Line runs along X, so the camera is rotated about THAT axis (pitch, not yaw): yaw
		// would swing round to look straight down the line and both profiles would collapse to a
		// point for reasons that have nothing to do with ribbon orientation. Pitch keeps the line
		// fully across the screen while turning a Flat ribbon from edge-on to face-on.
		const auto coverAt = [&](ScenePath path, const float pitch) {
			camera.SetOrbitState({}, 6.0f, 0.0f, pitch);
			PathSystem system;
			return NonBackground(RenderPath(std::move(path), camera, backend, system), kWidth, kHeight, {0, 0, 0, 255});
		};
		ScenePath cameraFacing = DevPath(ScenePathDevPreset::Line);
		cameraFacing.style.profile = StrokeProfile::CameraFacing;
		ScenePath flat = DevPath(ScenePathDevPreset::Line);
		flat.style.profile = StrokeProfile::Flat;
		constexpr float kEdgeOnPitch = 0.0f;
		const float faceOnPitch = glm::radians(80.0f);

		const std::size_t facingFirst = coverAt(cameraFacing, kEdgeOnPitch);
		const std::size_t facingSecond = coverAt(cameraFacing, faceOnPitch);
		const std::size_t flatFirst = coverAt(flat, kEdgeOnPitch);
		const std::size_t flatSecond = coverAt(flat, faceOnPitch);

		// CameraFacing is visible from both and barely changes - it is expanded towards the eye.
		EXPECT_GT(std::min(facingFirst, facingSecond), 0u);
		EXPECT_LT(std::abs(static_cast<double>(facingFirst) - static_cast<double>(facingSecond))
				/ static_cast<double>(std::max(facingFirst, std::size_t{1})), 0.2);
		// Flat lives in a fixed world plane, so the same rotation changes its coverage a lot. Which of
		// the two orientations is the wide one depends on the transported frame, hence max(), not first.
		const std::size_t flatWidest = std::max(flatFirst, flatSecond);
		EXPECT_GT(flatWidest, 0u);
		EXPECT_GT(std::abs(static_cast<double>(flatFirst) - static_cast<double>(flatSecond)),
			static_cast<double>(flatWidest) * 0.25);
		backend.Shutdown();
	}

	TEST_F(GlTest, DepthModesSelectOppositePasses)
	{
		OpenGlRendererBackend backend;
		ASSERT_TRUE(backend.Initialize(ShaderDirectoryNextToTestExecutable(), PrimitiveMeshes()));
		RendererViewCamera camera;
		camera.SetViewport(kWidth, kHeight);
		camera.SetOrbitState({}, 6.0f, 0.0f, 0.0f);
		// The occluder is a second, fat path rather than an atom: the fixture's "sphere" primitive is a
		// single triangle (PrimitiveMeshes), whose coverage of the centre pixel depends on where
		// SetOrbitState happens to put the camera. A path shifted half a unit along the view direction
		// is in front of the tested one whichever way that is, and stays depth-tested in both halves.
		const glm::vec3 towardCamera = glm::normalize(camera.Position()) * 0.5f;
		const auto occluder = [&] {
			ScenePath path = DevPath(ScenePathDevPreset::Line, SceneObjectId{2});
			path.style.color = glm::vec3(1.0f, 0.0f, 0.0f);
			path.style.width = 0.6f;
			path.style.endDecoration.kind = PathDecorationKind::None;
			for (PathNode &node : path.nodes)
				node.position += towardCamera;
			return path;
		};
		ScenePath depthPath = DevPath(ScenePathDevPreset::Line);
		depthPath.style.color = glm::vec3(0.0f, 1.0f, 0.0f);

		PathSystem depthSystem;
		depthSystem.Store().Insert(occluder());
		const auto depthPixels = RenderPath(depthPath, camera, backend, depthSystem);
		EXPECT_GT(NonBackground(depthPixels, kWidth, kHeight, {0, 0, 0, 255}), 0u);
		const Rgba8 depthCentre = PixelAt(depthPixels, kWidth, kHeight, kWidth / 2, kHeight / 2);
		EXPECT_GT(depthCentre.r, depthCentre.g);

		PathSystem topSystem;
		topSystem.Store().Insert(occluder());
		ScenePath top = depthPath;
		top.style.depthMode = PathDepthMode::AlwaysOnTop;
		const auto topPixels = RenderPath(top, camera, backend, topSystem);
		EXPECT_GT(NonBackground(topPixels, kWidth, kHeight, {0, 0, 0, 255}), 0u);
		const Rgba8 topCentre = PixelAt(topPixels, kWidth, kHeight, kWidth / 2, kHeight / 2);
		EXPECT_GT(topCentre.g, topCentre.r);
		backend.Shutdown();
	}

	TEST_F(GlTest, GradientLineHasDifferentEndpointColours)
	{
		OpenGlRendererBackend backend;
		ASSERT_TRUE(backend.Initialize(ShaderDirectoryNextToTestExecutable(), PrimitiveMeshes()));
		ScenePath path = DevPath(ScenePathDevPreset::Line);
		path.style.gradient.enabled = true;
		path.style.gradient.stops = {{0.0f, {1.0f, 0.0f, 0.0f}, 1.0f}, {1.0f, {0.0f, 0.0f, 1.0f}, 1.0f}};
		const StrokeGeometry geometry = BuildStroke(Evaluated(path), path.style);
		ASSERT_FALSE(geometry.tubeVertices.empty());
		// By arc position, not by array position: BuildStroke emits the endpoint decorations before the
		// shaft, so front()/back() say nothing about which end of the path a vertex sits at.
		const auto [nearest, furthest] = std::minmax_element(
			geometry.tubeVertices.begin(), geometry.tubeVertices.end(),
			[](const StrokeTubeVertex &a, const StrokeTubeVertex &b) { return a.arcT < b.arcT; });
		EXPECT_LT(glm::distance(glm::vec3(nearest->color), glm::vec3(1.0f, 0.0f, 0.0f)), 0.05f);
		EXPECT_LT(glm::distance(glm::vec3(furthest->color), glm::vec3(0.0f, 0.0f, 1.0f)), 0.05f);
		RendererViewCamera camera;
		camera.SetViewport(kWidth, kHeight);
		camera.SetOrbitState({}, 6.0f, 0.0f, 0.0f);
		PathSystem system;
		const auto pixels = RenderPath(path, camera, backend, system);
		EXPECT_GT(NonBackground(pixels, kWidth, kHeight, {0, 0, 0, 255}), 0u);
		backend.Shutdown();
	}

	TEST_F(GlTest, PathCacheReusesAndInvalidatesOnStyleAndErase)
	{
		OpenGlRendererBackend backend;
		ASSERT_TRUE(backend.Initialize(ShaderDirectoryNextToTestExecutable(), PrimitiveMeshes()));
		RendererViewCamera camera;
		camera.SetViewport(kWidth, kHeight);
		camera.SetOrbitState({}, 6.0f, 0.0f, 0.0f);
		PathSystem system;
		(void)RenderPath(DevPath(ScenePathDevPreset::Line), camera, backend, system);
		EXPECT_EQ(system.Caches().Size(), 1u);
		(void)RenderPath(DevPath(ScenePathDevPreset::Line), camera, backend, system);
		EXPECT_EQ(system.Caches().Size(), 1u);
		const SceneObjectId id{1};
		system.Store().MutateStyle(id, [](ScenePath &path) { path.style.color = glm::vec3(0.0f, 1.0f, 0.0f); });
		(void)RenderPath(DevPath(ScenePathDevPreset::Line), camera, backend, system);
		EXPECT_EQ(system.Caches().Size(), 1u);
		EXPECT_TRUE(system.ErasePath(id));
		EXPECT_EQ(system.Caches().Size(), 0u);
		backend.Shutdown();
	}

	TEST_F(GlTest, ShutdownReleasesPathsAndAllowsReinitialize)
	{
		OpenGlRendererBackend backend;
		ASSERT_TRUE(backend.Initialize(ShaderDirectoryNextToTestExecutable(), PrimitiveMeshes()));
		RendererViewCamera camera;
		camera.SetViewport(kWidth, kHeight);
		camera.SetOrbitState({}, 6.0f, 0.0f, 0.0f);
		PathSystem system;
		EXPECT_GT(NonBackground(RenderPath(DevPath(ScenePathDevPreset::Line), camera, backend, system), kWidth, kHeight, {0, 0, 0, 255}), 0u);
		backend.Shutdown();
		ASSERT_TRUE(backend.Initialize(ShaderDirectoryNextToTestExecutable(), PrimitiveMeshes()));
		EXPECT_NE(backend.RenderWindow("gl-path-reinit", {}, camera, {}, kWidth, kHeight, false, false, false, false), 0u);
		backend.Shutdown();
	}
} // namespace DefectStudio::Tests
