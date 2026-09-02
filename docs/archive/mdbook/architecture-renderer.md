# Renderer Architecture

The renderer layer is responsible for viewport drawing and GPU execution boundaries.

## Current Baseline

Implemented:

- GLFW window and OpenGL context bootstrap.
- GLAD loader and ImGui integration.
- Basic frame loop with Tracy CPU instrumentation.

## Planned Expansion

From TODO:

- Shader library and reload path.
- FBO-based viewport and offscreen pipeline.
- Instanced rendering for atoms and bonds.
- OpenGL 4.3 compute infrastructure for volumetrics and orbital projection.
- GPU profiling integration and request and commit execution model.

## Safety Boundary

Rendering code paths are expected to remain exception-free in critical loops.
