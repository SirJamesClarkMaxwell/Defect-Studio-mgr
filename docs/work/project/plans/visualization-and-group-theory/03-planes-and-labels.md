# 3. Planes and scene labels

Branch: `task/21-planes-and-labels` · Source: locked plan point 4

## Scope

- Support point+normal, three-point, and Miller-index construction modes, converging to one internal
  representation.
- Support finite, cell-clipped, and visually infinite modes.
- Put size, position, rotation, cropping, border, opacity, grid, stroke, and related controls in the
  existing Properties Panel.
- Use the same object/style/provenance model for manual planes and analysis-generated symmetry
  planes.
- Render LaTeX/Markdown both in panels/tables and in scene labels. Scene labels are camera-facing by
  default, with optional world/plane orientation.

## Note on label text

Labels like `³A₂` are expressible directly in Unicode. Start with a superscript/subscript mapping
over the existing font path; a Markdown/LaTeX parser is workstream 10 and is not a prerequisite
here.
