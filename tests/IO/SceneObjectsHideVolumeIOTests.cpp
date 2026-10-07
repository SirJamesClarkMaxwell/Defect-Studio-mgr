#include <gtest/gtest.h>

#include <string>

#include "IO/SceneObjectsIO.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		PersistedSceneHideVolume Volume(const char *kind, const char *frame)
		{
			PersistedSceneHideVolume volume;
			volume.persistKey = "0123456789abcdef0123456789abcdef";
			volume.kind = kind;
			volume.frame = frame;
			volume.center = glm::vec3(1.5f, -2.25f, 3.0f);
			volume.rotationEuler = glm::vec3(10.0f, 296.0f, -34.0f);
			volume.halfExtents = glm::vec3(2.0f, 3.5f, 5.0f);
			volume.invert = true;
			volume.name = "diament NV";
			volume.color = glm::vec3(0.1f, 0.2f, 0.3f);
			volume.alpha = 0.42f;
			volume.visible = false;
			volume.renderable = false;
			return volume;
		}

		const PersistedSceneHideVolume *FirstVolume(const SceneObjectsFile &file)
		{
			if (file.structures.empty() || file.structures[0].objects.empty())
			{
				return nullptr;
			}
			return std::get_if<PersistedSceneHideVolume>(&file.structures[0].objects[0]);
		}

		SceneObjectsFile RoundTrip(const PersistedSceneHideVolume &volume, std::vector<StructuredError> &warnings)
		{
			SceneObjectsFile file;
			PersistedStructureSceneObjects entry;
			entry.structureKey = "structures/NV/POSCAR";
			entry.objects.emplace_back(volume);
			file.structures.push_back(entry);

			SceneObjectsFile loaded;
			std::string error;
			EXPECT_TRUE(SceneObjectsIO::Parse(SceneObjectsIO::Serialize(file), loaded, warnings, error)) << error;
			return loaded;
		}
	} // namespace

	TEST(SceneObjectsHideVolumeIOTests, EveryKindAndFrameRoundTrips)
	{
		for (const char *kind : {"Sphere", "Box", "Cylinder"})
		{
			for (const char *frame : {"Anchored", "Fractional"})
			{
				const PersistedSceneHideVolume original = Volume(kind, frame);
				std::vector<StructuredError> warnings;
				const SceneObjectsFile loaded = RoundTrip(original, warnings);

				EXPECT_TRUE(warnings.empty()) << kind << " " << frame;
				const PersistedSceneHideVolume *parsed = FirstVolume(loaded);
				ASSERT_NE(parsed, nullptr) << kind << " " << frame;
				EXPECT_EQ(parsed->kind, original.kind);
				EXPECT_EQ(parsed->frame, original.frame);
				EXPECT_EQ(parsed->persistKey, original.persistKey);
				EXPECT_EQ(parsed->center, original.center);
				EXPECT_EQ(parsed->rotationEuler, original.rotationEuler);
				EXPECT_EQ(parsed->halfExtents, original.halfExtents);
				EXPECT_EQ(parsed->invert, original.invert);
				EXPECT_EQ(parsed->name, original.name);
				EXPECT_EQ(parsed->color, original.color);
				EXPECT_NEAR(parsed->alpha, original.alpha, 1e-6f);
				EXPECT_EQ(parsed->visible, original.visible);
				EXPECT_EQ(parsed->renderable, original.renderable);
			}
		}
	}

	TEST(SceneObjectsHideVolumeIOTests, AnchorAtomsRoundTripLikeAPlaneAnchor)
	{
		PersistedSceneHideVolume original = Volume("Sphere", "Anchored");
		original.anchorAtoms = {
			PersistedAtomRef{510, "C", glm::vec3(8.9175f, 8.9175f, 7.134f)},
			PersistedAtomRef{507, "N", glm::vec3(8.9175f, 7.134f, 8.9175f)},
		};

		std::vector<StructuredError> warnings;
		// Named, not a temporary: FirstVolume hands back a pointer into the file it was given.
		const SceneObjectsFile loaded = RoundTrip(original, warnings);
		const PersistedSceneHideVolume *parsed = FirstVolume(loaded);
		ASSERT_NE(parsed, nullptr);
		ASSERT_EQ(parsed->anchorAtoms.size(), 2u);
		EXPECT_EQ(parsed->anchorAtoms[0].index, 510u);
		EXPECT_EQ(parsed->anchorAtoms[0].element, "C");
		EXPECT_EQ(parsed->anchorAtoms[0].position, original.anchorAtoms[0].position);
		EXPECT_EQ(parsed->anchorAtoms[1].index, 507u);
		EXPECT_EQ(parsed->anchorAtoms[1].element, "N");
	}

	TEST(SceneObjectsHideVolumeIOTests, AVolumeWithNoHideVolumesLeavesTheFormatVersionAlone)
	{
		// The additive rule the vacancies and projectObjects entries already follow: a new kind must
		// not bump the format version, or an older build refuses a file that has nothing new in it.
		SceneObjectsFile file;
		PersistedStructureSceneObjects entry;
		entry.structureKey = "structures/NV/POSCAR";
		file.structures.push_back(entry);

		SceneObjectsFile loaded;
		std::vector<StructuredError> warnings;
		std::string error;
		ASSERT_TRUE(SceneObjectsIO::Parse(SceneObjectsIO::Serialize(file), loaded, warnings, error)) << error;
		EXPECT_EQ(loaded.formatVersion, SceneObjectsIO::kFormatVersion);
		EXPECT_EQ(SceneObjectsIO::kFormatVersion, 2);
	}

	TEST(SceneObjectsHideVolumeIOTests, UnknownKindOrFrameSkipsTheEntryWithAWarning)
	{
		// Same failure mode as every other scene object: one bad entry is skipped and warned about,
		// the rest of the file still loads. A volume whose kind nobody recognises must not be loaded
		// as a sphere - it would hide the wrong atoms silently.
		for (const PersistedSceneHideVolume bad : {Volume("Torus", "Anchored"), Volume("Sphere", "Galactic")})
		{
			std::vector<StructuredError> warnings;
			const SceneObjectsFile loaded = RoundTrip(bad, warnings);

			EXPECT_FALSE(warnings.empty()) << bad.kind << " " << bad.frame;
			ASSERT_EQ(loaded.structures.size(), 1u);
			EXPECT_TRUE(loaded.structures[0].objects.empty());
		}
	}
} // namespace DefectStudio::Tests
