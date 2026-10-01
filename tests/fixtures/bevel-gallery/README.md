# Bevel diagnostic gallery

A Defect Studio project whose structure-free **Project Scene** holds one bevelled Flat ribbon per
(endpoint decoration x bevel setting). It is the shared artifact for reviewing and fixing decoration
bevel geometry: open it, look, change the mesher, look again.

```
DefectStudio --project=tests/fixtures/bevel-gallery
```

(`--project=` and the Project Scene come from task 49; run from the repo root or pass an absolute
path.)

## Layout

- **Rows** (top to bottom, y = 0, -1.6, -3.2, ...): `None` (control), `Arrow`, `Stealth`, `Latex`,
  `OpenArrow`, `Bar`, `Circle`, `Square`, `Diamond`, `Kite` - the decoration on the path's END.
- **Columns** (left to right, x = 0, 3.2, 6.4, ...):

  | # | name | ribbon_bevel | segments | shape | path |
  |---|---|---|---|---|---|
  | 0 | sharp (reference) | 0 | 1 | 0.5 | straight |
  | 1 | chamfer 0.02 | 0.02 | 1 | 0.5 | straight |
  | 2 | 0.04 x3 | 0.04 | 3 | 0.5 | straight |
  | 3 | 0.06 x8 round | 0.06 | 8 | 0.5 | straight |
  | 4 | 0.04 x4 shape 1.0 | 0.04 | 4 | 1.0 | straight |
  | 5 | 0.04 x3 arc | 0.04 | 3 | 0.5 | 90 degree arc |

Every path: Flat profile in the XY plane (`ribbon_normal [0, 0, 1]`), width 0.3, thickness 0.15,
decoration scale 1. Outliner names read `<decoration> | <column>`; persist keys
`bevel-gallery-rRR-cCC`.

## Regenerating

The scene is generated - do not hand-edit `scene_objects.yaml`. Change `ROWS` / `COLUMNS` in
`scripts/python/make_bevel_gallery.py`, then:

```
python scripts/python/make_bevel_gallery.py
```

Saving the project from the app rewrites `scene_objects.yaml` in the app's own formatting; that is
fine for a scratch copy, but commit the generator's output, not an app save, so diffs stay readable.

## Related

- `docs/superpowers/specs/2026-09-30-project-scene-persistence-design.md` - why the gallery lives in
  a project.
- `docs/superpowers/plans/2026-09-28-decoration-bevel-corner-repair.md` - the bevel work it serves.
- Known red tests before any bevel fix: 5 in `PathStrokeMesherTests` (bevel/decoration surfaces).
