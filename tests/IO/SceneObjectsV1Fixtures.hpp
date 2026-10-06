#pragma once

namespace DefectStudio::Tests::SceneObjectsV1Fixtures
{
	inline constexpr const char *Line = R"YAML(formatVersion: 1
structures:
  - structureKey: k
    objects:
      - kind: SceneArrow
        arrowKind: Line
        points: [[0, 0, 0], [1, 0, 0]]
)YAML";

	inline constexpr const char *Arrow2DBillboard = R"YAML(formatVersion: 1
structures:
  - structureKey: k
    objects:
      - kind: SceneArrow
        arrowKind: Arrow2D
        orientation2D: Billboard
        points: [[0, 0, 0], [1, 0, 0]]
)YAML";

	inline constexpr const char *Arrow2DFixedPlane = R"YAML(formatVersion: 1
structures:
  - structureKey: k
    objects:
      - kind: SceneArrow
        arrowKind: Arrow2D
        orientation2D: FixedPlane
        fixedPlane: XZ
        points: [[0, 0, 0], [1, 0, 0]]
)YAML";

	inline constexpr const char *Arrow3D = R"YAML(formatVersion: 1
structures:
  - structureKey: k
    objects:
      - kind: SceneArrow
        arrowKind: Arrow3D
        points: [[0, 0, 0], [1, 0, 0]]
)YAML";

	inline constexpr const char *Quadratic = R"YAML(formatVersion: 1
structures:
  - structureKey: k
    objects:
      - kind: SceneArrow
        arrowKind: Arrow3D
        points: [[0, 0, 0], [1, 0, 0]]
        control_point: [0.5, 1, 0]
)YAML";

	inline constexpr const char *TipsGradientOutline = R"YAML(formatVersion: 1
structures:
  - structureKey: k
    objects:
      - kind: SceneArrow
        arrowKind: Arrow3D
        points: [[0, 0, 0], [1, 0, 0]]
        start_tip: Circle
        end_tip: Barbed
        style:
          shaftWidth: 0.2
          headWidth: 0.4
          headLength: 0.6
          useGradient: true
          gradientStart: [1, 0, 0]
          gradientFinish: [0, 0, 1]
          outlineWidth: 0.1
)YAML";

	inline constexpr const char *Anchored = R"YAML(formatVersion: 1
structures:
  - structureKey: k
    objects:
      - kind: SceneArrow
        arrowKind: Arrow3D
        points: [[0, 0, 0], [1, 0, 0]]
        startAnchorAtoms: [{index: 0, element: C, position: [0, 0, 0]}]
        endAnchorAtoms: [{index: 1, element: C, position: [1, 0, 0]}]
        atom_buffer: 1.25
)YAML";

	inline constexpr const char *AllTips = R"YAML(formatVersion: 1
structures:
  - structureKey: k
    objects:
      - {kind: SceneArrow, arrowKind: Arrow3D, points: [[0, 0, 0], [1, 0, 0]], start_tip: None, end_tip: None}
      - {kind: SceneArrow, arrowKind: Arrow3D, points: [[0, 0, 0], [1, 0, 0]], start_tip: Plain, end_tip: Plain}
      - {kind: SceneArrow, arrowKind: Arrow3D, points: [[0, 0, 0], [1, 0, 0]], start_tip: Barbed, end_tip: Barbed}
      - {kind: SceneArrow, arrowKind: Arrow3D, points: [[0, 0, 0], [1, 0, 0]], start_tip: Open, end_tip: Open}
      - {kind: SceneArrow, arrowKind: Arrow3D, points: [[0, 0, 0], [1, 0, 0]], start_tip: Bar, end_tip: Bar}
      - {kind: SceneArrow, arrowKind: Arrow3D, points: [[0, 0, 0], [1, 0, 0]], start_tip: Circle, end_tip: Circle}
)YAML";
}
