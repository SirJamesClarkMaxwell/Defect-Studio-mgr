# Task 41 S4: GL test harness and shared framebuffer readback

## Goal
Structural OpenGL assertions become possible in the test suite: a hidden-window GL context created and
torn down per test, the production `OpenGlRendererBackend` initialised against the real shader tree, and
a colour texture read back as top-down RGBA8 through the same helper the PNG exporter uses. Independent
of S1-S3; S7 (the path renderer) is what will consume it.

## Files to create
- `src/Renderer/OpenGl/FrameBufferReadback.cpp`
- `tests/Renderer/Gl/GlTestContext.cpp`
- `tests/Renderer/Gl/GlSmokeTests.cpp`

## Files you may change
- `src/Renderer/OpenGl/OpenGlRendererBackend.cpp` - ONLY inside `CaptureWindowToPng`: replace the
  inline `glReadPixels` + manual row-flip loop (around line 3441-3456) with a call to
  `ReadRgba8TopDown(leftPx, bottomPx, width, height)`, and add the include. The framebuffer
  `Bind()`/`Unbind()` stay in the caller. The crop arithmetic, the `stbi_write_png` call and every
  error string stay byte-identical - this refactor must not change one exported pixel.

## Files that must NOT be touched
- `src/Renderer/OpenGl/FrameBufferReadback.hpp`, `tests/Renderer/Gl/GlTestContext.hpp` - the contract.
  If a signature is wrong, STOP and say which and why; do not edit it.
- `premake5.lua` - already updated; the test target links GLFW/GLAD/opengl32 and copies the shader tree
  to `<test exe dir>/shaders`.
- Anything under `src/Renderer/Path/`, `src/Presentation/`, `src/App/`, `src/Domain/`, `src/IO/`.
- Every other part of `OpenGlRendererBackend.cpp`.

## Acceptance criteria
1. `ReadRgba8TopDown` returns `width * height * 4` bytes with row 0 being the TOP row of the requested
   rectangle, and an empty vector for a non-positive width or height.
2. `CaptureWindowToPng` calls it and produces the same PNG as before (verified by me, by exporting the
   same scene before and after and comparing bytes).
3. `GlTestContext(width, height)` leaves a current GL context with GLAD loaded, or `IsValid() == false`
   with a non-empty `FailureReason()`. It never aborts, never throws, never leaks a window or a GLFW
   init on the failure path, and tears down in reverse order of creation.
4. The context is hidden: `GLFW_VISIBLE` off. No multisampling, no sRGB framebuffer, and it requests the
   same GL version/profile the application requests (read `src/App/` or `src/Presentation/` for the
   window hints the app uses and match them).
5. Two `GlTestContext` objects are never alive at once: the constructor serialises on a process-wide
   lock released by the destructor. Creating, destroying and re-creating a context in the same process
   works (a test that does exactly this must pass).
6. `Description()` returns a single line containing `GL_VENDOR`, `GL_RENDERER` and `GL_VERSION`; the
   fixture logs it once so a CI failure says which driver produced it.
7. `GlTest::SetUp` creates a `kWidth` x `kHeight` context. On Windows an invalid context is a `FAIL`
   carrying `FailureReason()`; elsewhere it is a `GTEST_SKIP` carrying the same reason.
8. `ReadTextureRgba8TopDown` attaches the texture to a scratch framebuffer, reads through
   `ReadRgba8TopDown`, deletes the scratch framebuffer, restores the previously bound framebuffer, and
   returns an empty vector for texture 0 or a non-positive size.
9. `PixelAt` addresses with y == 0 at the top and returns `{0,0,0,0}` for out-of-range coordinates or a
   buffer smaller than `width * height * 4`.
10. Smoke test: `OpenGlRendererBackend::Initialize` succeeds with the shader directory next to the test
    executable (`<exe dir>/shaders`; resolve it from the executable path, do not hardcode a build
    configuration) and with primitive meshes built in the test itself (a single triangle for each of
    sphere/cylinder/cone is enough - the empty-structure render does not draw them). A failure reports
    `Result::Error().technicalDetails`.
11. Smoke test: rendering an empty `RendererStructureData` through `RenderWindow` with a fixed camera
    and a known background colour returns a non-zero texture, and all four corner pixels of the readback
    equal that background colour exactly (alpha included).
12. Smoke test: readback orientation. Build an asymmetric fixture that is unambiguous top-to-bottom -
    render, or upload, an image whose top half and bottom half differ - and assert the TOP row of the
    top-down readback is the half that is visually on top. A transposed or flipped readback must fail
    this test.
13. `Shutdown()` runs before the context is destroyed in every smoke test (the backend owns GL objects;
    destroying the context first is a use-after-free on the driver side).
14. Full Release test suite green except the two permanent skips
    (`ConPtyProcessTests.RunsCommandAndProducesOutput`,
    `BridgeRoundtripDemoTests.PythonImportsNanobindModuleWhenAvailable`).

## Constraints
- Layer: `Renderer` and `tests` only. No exceptions (`Renderer` is the documented exception-free zone);
  failures are `Result<T>` / recorded reasons / gtest assertions.
- No `std::thread`. The GL context is main-thread only.
- Include GLAD before GLFW, exactly as the existing renderer sources do. Read
  `src/Renderer/OpenGl/OpenGlRendererBackend.cpp` for the include order that works in this build.
- `.cpp` files stay under ~500 lines.
- Follow existing style: tabs, `#include "Core/dspch.hpp"` first, anonymous namespace for helpers,
  `[[nodiscard]]`.
- Tests: GoogleTest, namespace `DefectStudio::Tests`.
- Do not add a second mechanism for anything that already exists: the renderer already has an
  `OpenGlFrameBuffer` class - read it before writing scratch-framebuffer code, and reuse it if it fits.
