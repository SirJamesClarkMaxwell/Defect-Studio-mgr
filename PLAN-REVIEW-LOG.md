# Plan Review Log: Structure Creation UI Redesign

Act 1 (grill) complete 2026-09-10 — plan locked with the user. MAX_ROUNDS=5.

Superseded log for the previous plan (Structure Lifecycle Steps 10-11, 8 rounds, APPROVED)
is in git history at commit 86ef70e.

## Act 1 — decisions locked

| Question | Answer |
|----------|--------|
| Lattice vs basis | Manual: centering is a lattice property, basis table is the motif only |
| Generated atoms | Collapsible read-only list, collapsed by default |
| Preview timing | Session and panes open on first valid draft, not on Structure Hub hand-off |
| "Move to Structure Hub" | Submit-only: puts the draft on the Hub list, opens nothing |
| Mode naming | "Create New" everywhere; enum keeps FromScratch internally |
| Renderer shape | ONE window, one horizontal + one vertical toolbar, viewport split into panes |
| Toolbar scope | Acts on the active (last-clicked) pane; camera per pane |
| The three panes | Basis / unit cell / supercell — the invented "Analysis" pane is dropped |
| Pane layout | Basis + unit cell on top, supercell full width below |
| Supercell at 1x1x1 | Pane stays, shows the same content; layout never jumps |
| Show primitive cell | Stays as an overlay on the unit-cell pane |
| File selection | All four: NFD dialog, Project Tree selection, active viewport, drag-and-drop |
| Pane visibility checkboxes | Kept; hiding a pane stretches the rest |
| From Library | Prototype fills lattice and basis, both stay editable |
| Scope | All of it in task/18, one branch |

Act 2 (Codex adversarial review) not yet run.
