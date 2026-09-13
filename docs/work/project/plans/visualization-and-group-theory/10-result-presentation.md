# 10. Rich result presentation

Source: locked plan point 10

## Scope

- Implement a controlled Markdown subset with inline/block LaTeX, headings, lists, tables, code, and
  mathematical superscripts/subscripts.
- Render labels such as `³A₂` clearly in panels, tables, and scene annotations.
- Reuse existing font/renderer infrastructure and avoid a full notebook implementation.
- Keep computed mathematics separate from visual display parameters.

## Sizing note

This is the single largest non-physics cost in the plan. Term labels (`³A₂`, `¹A₁`) are plain
Unicode and need only a superscript/subscript mapping over the existing font — that covers most of
the visible value. Build the parser only once a concrete result genuinely cannot be displayed
without it.
