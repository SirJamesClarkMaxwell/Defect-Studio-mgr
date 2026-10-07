# CHGCAR density objects – follow-ups (2026-10-07)

Steps 1 and 2 of the FNV/CHGCAR plan (density loading + isosurface objects with an N-panel section)
landed on `task/83-chgcar-density`. The user tested them on GeV q-1 / q+1 and everything worked.
These are their requests for the next round, recorded before work starts. Point 4 is open for
discussion.

1. **Load the whole CHGCAR at once.** Load the total density first, then immediately start loading
   the remaining components (magnetization, spin up, spin down) in the background, so switching
   the component in the N panel does not wait another ~13 s.
2. **Cache loaded results.** Do not re-read what was already read: key on (CHGCAR path, mtime,
   component, reference path). Today every component change, every reopened project and every
   new object for the same file parses the file again. Parsing the file once and producing all
   blocks in one job would also cover point 1.
3. **`regenerate-bonds` command.** Generate bonds for all atoms (RegenerateAutoBonds over the whole
   structure), but show only the bonds whose atoms are both visible. Context from the logs: the
   user selected all 511 atoms and pressed `renderer.bonds.connect`, which connects exactly 2
   atoms. A regenerate command does not exist yet; auto bonds are only rebuilt on open or after a
   Bond Settings change.
4. **Show atoms only inside a region (to discuss).** The region is a sphere, cylinder or box,
   centred on a chosen atom (or a point) with an editable radius or dimensions, so the view of a
   defect can be set up quickly. Open questions:
   - Is the region a scene object with its own N-panel section (centre/anchor atom, shape,
     dimensions)?
   - Does it hide atoms through visibility or through a render-time clip?
   - How does it interact with the outliner eye/camera columns?
   - Should bonds and densities be clipped too?

Also observed:
- The isosurface iso slider floods the log with OpenGL "Buffer performance warning ... copied
  from VIDEO memory to HOST memory". The cause is the counter-SSBO readback in
  `OpenGlRendererBackend::dispatchIsosurfaceCompute`: the counter is allocated as
  GL_DYNAMIC_COPY and read back on every re-march. Fix it by giving the counter a read-friendly
  usage, or by reading it back through a separate small buffer.
- Fixed already: a crash on paths with non-cp1252 characters ("Praca-Inżynierska"). The density
  code now uses `Path::Utf8()` / `Path::FromUtf8()`. Other code still uses `Path::String()` and
  can hit the same crash.
