#include "Core/dspch.hpp"

#include "Presentation/Panels/ViewportGizmo.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <ImGuizmo.h>

#include "Core/Commands/CommandRegistry.hpp"
#include "Core/Logging/Logger.hpp"
#include "Renderer/Commands/RendererAtomEditCommands.hpp"
#include "Renderer/RendererViewCamera.hpp"
#include "Renderer/RendererWindowState.hpp"
#include "Renderer/Scene/SelectionHitTest.hpp"

namespace DefectStudio
{
	namespace
	{
		// Parses a Blender-style typed numeric override ("-3.5" while it's still being typed, possibly
		// just "-" or "." mid-entry) - unparsable/partial input reads as 0 rather than erroring, since
		// the caller applies this every frame while the buffer is non-empty.
		[[nodiscard]] float ParseTypedNumber(const std::string &buffer)
		{
			float value = 0.0f;
			std::from_chars(buffer.data(), buffer.data() + buffer.size(), value);
			return value;
		}

		// Captures Blender-style numeric-override keystrokes (digits, sign, decimal point, Backspace)
		// into windowState.fallbackNumericInput while a locked-axis fallback drag is active - shared by
		// both the rotate axis-locked path and the translate/scale path below since the key handling is
		// identical, only what the resulting number MEANS differs per call site.
		void CaptureGizmoNumericInput(RendererWindowState &windowState)
		{
			for (int digit = 0; digit < 10; ++digit)
			{
				if (ImGui::IsKeyPressed(static_cast<ImGuiKey>(ImGuiKey_0 + digit), false) ||
					ImGui::IsKeyPressed(static_cast<ImGuiKey>(ImGuiKey_Keypad0 + digit), false))
					windowState.fallbackNumericInput += static_cast<char>('0' + digit);
			}
			if ((ImGui::IsKeyPressed(ImGuiKey_Minus, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract, false)) &&
				windowState.fallbackNumericInput.find('-') == std::string::npos)
				windowState.fallbackNumericInput.insert(windowState.fallbackNumericInput.begin(), '-');
			if ((ImGui::IsKeyPressed(ImGuiKey_Period, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadDecimal, false)) &&
				windowState.fallbackNumericInput.find('.') == std::string::npos)
				windowState.fallbackNumericInput += '.';
			if (ImGui::IsKeyPressed(ImGuiKey_Backspace, false) && !windowState.fallbackNumericInput.empty())
				windowState.fallbackNumericInput.pop_back();
		}

		// Mean of a frozen position snapshot - used by the numeric-override rotate/scale paths below
		// to get a pivot that stays fixed for the whole typed-number entry instead of the live
		// per-frame selection centroid (see call sites: mixing a frozen snapshot with a pivot that is
		// itself recomputed from the snapshot's own output each frame is a feedback loop, and for
		// rotation in particular it's a DIVERGING one - the per-frame pivot error e satisfies
		// e(t+1) = (I-R)*e(t), and |I-R| = 2*sin(angle/2) exceeds 1 for any angle over 60 degrees, so
		// a typed 90 degree rotation doubled its own error roughly every frame and threw the selection
		// off-screen within a few dozen frames).
		[[nodiscard]] glm::vec3 MeanPosition(const std::vector<glm::vec3> &positions)
		{
			if (positions.empty())
				return glm::vec3(0.0f);
			glm::vec3 sum(0.0f);
			for (const glm::vec3 &position : positions)
				sum += position;
			return sum / static_cast<float>(positions.size());
		}

		// Whether ANY atom's sphere is under screenPos (same ray/pick-radius as HandleAtomPick, read-only
		// - no selection change). Used to give plain atom-click priority over the gizmo's axis pick band:
		// in a crystal lattice, a bonded neighbour very often sits almost exactly along a world axis from
		// the selected atom, right where the gizmo's own pick band lives - without this check, clicking
		// that neighbour to extend the selection (e.g. to build a 2-atom bond-length measurement) grabs
		// the gizmo instead of selecting it.
		[[nodiscard]] bool IsAtomUnderScreenPosition(
			const RendererWindowState &windowState, const ImVec2 &imageOrigin, const glm::vec2 &screenPos)
		{
			if (!windowState.camera || windowState.viewportSize.x <= 0.0f || windowState.viewportSize.y <= 0.0f)
				return false;

			const float relX = screenPos.x - imageOrigin.x;
			const float relY = screenPos.y - imageOrigin.y;
			if (relX < 0.0f || relY < 0.0f || relX >= windowState.viewportSize.x || relY >= windowState.viewportSize.y)
				return false;

			const float ndcX = (2.0f * relX / windowState.viewportSize.x) - 1.0f;
			const float ndcY = -((2.0f * relY / windowState.viewportSize.y) - 1.0f);

			const glm::mat4 invVP = glm::inverse(windowState.camera->ProjectionMatrix() * windowState.camera->ViewMatrix());
			const glm::vec4 nearH = invVP * glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
			const glm::vec4 farH = invVP * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
			const glm::vec3 rayOrigin = glm::vec3(nearH) / nearH.w;
			const glm::vec3 rayDir = glm::normalize(glm::vec3(farH) / farH.w - rayOrigin);

			for (const RendererAtomData &atom : windowState.structure.atoms)
			{
				if (!atom.visible)
					continue;
				const glm::vec3 oc = rayOrigin - atom.cartesianPosition;
				const float a = glm::dot(rayDir, rayDir);
				const float b = 2.0f * glm::dot(oc, rayDir);
				const float pickRadius = atom.radius * 1.35f;
				const float c = glm::dot(oc, oc) - pickRadius * pickRadius;
				const float disc = b * b - 4.0f * a * c;
				if (disc < 0.0f)
					continue;
				const float t = (-b - std::sqrt(disc)) / (2.0f * a);
				if (t > 0.001f)
					return true;
			}
			return false;
		}

		// Same idea as IsAtomUnderScreenPosition, for bonds - a selected atom's own gizmo axis pick band
		// (kPickMaxDistance in RenderTransformGizmo) very often overlaps a bonded neighbour's bond line
		// too, not just the neighbour atom itself, so this needs the same priority override to keep a
		// deliberate bond click from being swallowed by the gizmo.
		[[nodiscard]] bool IsBondUnderScreenPosition(
			const RendererWindowState &windowState, const ImVec2 &imageOrigin, const glm::vec2 &screenPos)
		{
			if (!windowState.camera || windowState.viewportSize.x <= 0.0f || windowState.viewportSize.y <= 0.0f)
				return false;

			const float relX = screenPos.x - imageOrigin.x;
			const float relY = screenPos.y - imageOrigin.y;
			if (relX < 0.0f || relY < 0.0f || relX >= windowState.viewportSize.x || relY >= windowState.viewportSize.y)
				return false;

			const float ndcX = (2.0f * relX / windowState.viewportSize.x) - 1.0f;
			const float ndcY = -((2.0f * relY / windowState.viewportSize.y) - 1.0f);

			const glm::mat4 invVP = glm::inverse(windowState.camera->ProjectionMatrix() * windowState.camera->ViewMatrix());
			const glm::vec4 nearH = invVP * glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
			const glm::vec4 farH = invVP * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
			const glm::vec3 rayOrigin = glm::vec3(nearH) / nearH.w;
			const glm::vec3 rayDir = glm::normalize(glm::vec3(farH) / farH.w - rayOrigin);

			for (const RendererBondData &bond : windowState.structure.bonds)
			{
				if (!bond.visible || bond.firstAtomIndex >= windowState.structure.atoms.size() ||
					bond.secondAtomIndex >= windowState.structure.atoms.size())
					continue;
				const RendererAtomData &firstAtom = windowState.structure.atoms[bond.firstAtomIndex];
				const RendererAtomData &secondAtom = windowState.structure.atoms[bond.secondAtomIndex];
				if (!firstAtom.visible || !secondAtom.visible)
					continue;

				float t = 0.0f;
				glm::vec3 closestOnSegment(0.0f);
				SelectionHitTest::ClosestPointsRaySegment(
					rayOrigin, rayDir, firstAtom.cartesianPosition, secondAtom.cartesianPosition + bond.secondAtomPeriodicOffset,
					t, closestOnSegment);
				if (t <= 0.001f)
					continue;

				const float pickRadius = std::max(bond.radius * 2.5f, 0.12f);
				const glm::vec3 closestOnRay = rayOrigin + rayDir * t;
				if (glm::distance(closestOnRay, closestOnSegment) <= pickRadius)
					return true;
			}
			return false;
		}
	}

	// G/R/S transform gizmo for the current selection. Pivot is the live centroid of selected
	// atoms, recomputed every frame (not cached) - stays correct under rotate/scale since a rigid
	// transform about its own centroid leaves that centroid fixed. While dragging, the frame's
	// incremental delta is applied directly to windowState.structure (renderer hot path) for
	// immediate visual feedback; on release, the final result is committed to the domain structure
	// as one undoable command (RendererAtomEditCommands::TransformSelectedAtomsCommand).
	bool RenderTransformGizmo(
		RendererWindowState &windowState, const ImVec2 &imageOrigin, const ImVec2 &imageSize, bool hovered,
		const WeakRef<CommandRegistry> &commandRegistryRef)
	{
		if (windowState.selectedAtomIndices.empty() || windowState.camera == nullptr)
		{
			windowState.gizmoDragActive = false;
			return false;
		}

		// A modal/click-drag reads X/Y/Z (axis lock), digits/-/./Backspace/Enter (numeric override,
		// see CaptureGizmoNumericInput) and Escape (cancel) directly via ImGui::IsKeyPressed below -
		// none of that goes through an actual ImGui widget, so ImGui's own WantCaptureKeyboard (the
		// gate Application::dispatchEvent uses to stop app-wide shortcuts firing into a focused text
		// field) stays false and doesn't protect it. Without this, typing "1" to enter a distance
		// mid-drag also fired the bare "1" -> renderer.align_axis_a shortcut and yanked the camera.
		// One frame of lag (this only takes effect for the NEXT frame's input dispatch, since this
		// frame's events were already dispatched before Render() runs) is harmless for a multi-frame
		// drag - only the drag's very first keystroke could still leak through.
		if (windowState.fallbackGizmoDragging)
			ImGui::GetIO().WantCaptureKeyboard = true;

		glm::vec3 pivot(0.0f);
		for (const std::size_t atomIndex : windowState.selectedAtomIndices)
			pivot += windowState.structure.atoms[atomIndex].cartesianPosition;
		pivot /= static_cast<float>(windowState.selectedAtomIndices.size());

		glm::mat4 gizmoMatrix = glm::translate(glm::mat4(1.0f), pivot);
		glm::mat4 deltaMatrix(1.0f);

		const glm::mat4 view = windowState.camera->ViewMatrix();
		const glm::mat4 projection = windowState.camera->ProjectionMatrix();

		ImGuizmo::OPERATION operation = ImGuizmo::TRANSLATE;
		switch (windowState.gizmoOperation)
		{
			case GizmoOperation::Translate: operation = ImGuizmo::TRANSLATE; break;
			case GizmoOperation::Rotate: operation = ImGuizmo::ROTATE; break;
			case GizmoOperation::Scale: operation = ImGuizmo::SCALE; break;
		}

		ImGuizmo::PushID(windowState.windowId.c_str());
		ImGuizmo::SetDrawlist();
		ImGuizmo::SetOrthographic(windowState.camera->Projection() == CameraProjection::Orthographic);
		// Enable(false) turns off ImGuizmo's own hit-test/drag path (see prior note: it was silently
		// live and fighting our fallback system for every click near the gizmo) - kept regardless of
		// whether Manipulate() below actually runs, in case anything else in this ID scope reads it.
		ImGuizmo::Enable(false);
		ImGuizmo::SetRect(imageOrigin.x, imageOrigin.y, imageSize.x, imageSize.y);
		// TRANSLATE/SCALE only draw OUR OWN axis lines below (see "Blender-style axis indicators") -
		// ImGuizmo's native draw for these two ops also always includes its plane-drag quads
		// (DrawTranslationGizmo draws one per axis pair unconditionally, see TRANSLATE_PLANS in the
		// vendored source) even though this app has no plane-drag interaction at all, which is what
		// the recurring "why is this gray?" / "doesn't look right" reports were pointing at - a
		// translucent square implying a capability that doesn't exist. ROTATE has no such quads and
		// its native rings are still the only rest-state visual we have for that mode, so it keeps
		// using Manipulate() to draw.
		if (operation == ImGuizmo::ROTATE)
		{
			ImGuizmo::Manipulate(
				glm::value_ptr(view),
				glm::value_ptr(projection),
				operation,
				ImGuizmo::WORLD,
				glm::value_ptr(gizmoMatrix),
				glm::value_ptr(deltaMatrix));
		}
		ImGuizmo::PopID();

		// ImGuizmo::Manipulate() above is called purely to DRAW the handles - its own screen-space
		// picking (IsOver()/IsUsing()) is unreliable in this app in BOTH directions (confirmed via
		// synthetic clicks measured directly against screenshots earlier this session) and every
		// interaction below is driven by our own screen-space hit-test instead, ported from
		// Desktop/STUDIA/Degects-Studio (an earlier iteration of this project that hit the same
		// problem): false IsOver()==true blocked our own pick from ever starting so a click that
		// looked right on target did nothing at all; false IsOver()==false right as a fallback drag
		// began let HandleAtomPick fire the same frame and silently re-pick whatever atom was
		// nearest the cursor - selection jumping mid-drag. World-space axes only (this gizmo never
		// runs in LOCAL mode).
		const glm::mat4 viewProjection = projection * view;
		auto projectToScreen = [&](const glm::vec3 &world, glm::vec2 &outScreen) -> bool {
			const glm::vec4 clip = viewProjection * glm::vec4(world, 1.0f);
			if (clip.w <= 0.0001f)
				return false;
			const glm::vec3 ndc = glm::vec3(clip) / clip.w;
			outScreen = glm::vec2(
				imageOrigin.x + (ndc.x * 0.5f + 0.5f) * imageSize.x,
				imageOrigin.y + (1.0f - (ndc.y * 0.5f + 0.5f)) * imageSize.y);
			return true;
		};

		glm::vec2 pivotScreen(0.0f);
		const bool pivotOnScreen = projectToScreen(pivot, pivotScreen);
		const glm::vec2 mousePos(ImGui::GetMousePos().x, ImGui::GetMousePos().y);
		// An atom actually under the cursor always wins over grabbing the gizmo (see
		// IsAtomUnderScreenPosition) - only matters for STARTING a new hover/drag below, never checked
		// once fallbackGizmoDragging is already true so it can't interrupt a drag in progress.
		const bool atomUnderCursor = !windowState.fallbackGizmoDragging && IsAtomUnderScreenPosition(windowState, imageOrigin, mousePos);
		const bool atomOrBondUnderCursor =
			atomUnderCursor || (!windowState.fallbackGizmoDragging && IsBondUnderScreenPosition(windowState, imageOrigin, mousePos));
		constexpr float kPickMinDistance = 20.0f;
		// This band is now evaluated every frame (not just on click) to drive gizmoCapturing's
		// hover-suppression of atom-pick/box-select - the original 350px was sized for a one-off
		// click test and, applied continuously, ate clicks on any atom within ~350px of the pivot in
		// a dense structure ("selection acts erratic"). SetGizmoSizeClipSpace keeps the gizmo's own
		// drawn size constant in screen pixels regardless of zoom, so a smaller fixed band still
		// tracks the visible handles correctly at any zoom level.
		constexpr float kPickMaxDistance = 130.0f;

		constexpr glm::vec3 kWorldAxes[3] = {
			glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f)};
		constexpr ImU32 kAxisLockColors[3] = {
			IM_COL32(230, 70, 70, 200), IM_COL32(90, 210, 90, 200), IM_COL32(90, 150, 240, 200)};

		if (operation == ImGuizmo::ROTATE)
		{
			// No per-axis ring hit-test (would need ImGuizmo's internal ring radius, which isn't
			// exposed) - instead a trackball: grab anywhere in the pick band around the pivot and
			// drag freely, rotation axis = cross(camera-forward, screen-space drag direction), angle
			// proportional to drag distance. Visually looser than ImGuizmo's 3 discrete rings but
			// gives full 3D rotation control without needing their exact geometry.
			const float radial = pivotOnScreen ? glm::length(mousePos - pivotScreen) : -1.0f;
			const bool hoveringRing =
				pivotOnScreen && !atomOrBondUnderCursor && radial >= kPickMinDistance && radial <= kPickMaxDistance;

			if (!windowState.fallbackGizmoDragging && hoveringRing && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
			{
				windowState.fallbackGizmoDragging = true;
				windowState.fallbackGizmoAxis = -2; // sentinel: trackball rotate, not a translate/scale axis
				windowState.fallbackLastMousePos = mousePos;
				windowState.fallbackNumericInput.clear();
			}

			if (windowState.fallbackGizmoDragging && windowState.fallbackGizmoAxis == -2)
			{
				if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
				{
					const glm::vec3 cameraRight(view[0][0], view[1][0], view[2][0]);
					const glm::vec3 cameraUp(view[0][1], view[1][1], view[2][1]);
					const glm::vec3 cameraForward = -glm::vec3(view[0][2], view[1][2], view[2][2]);

					const glm::vec2 delta = mousePos - windowState.fallbackLastMousePos;
					windowState.fallbackLastMousePos = mousePos;

					// Screen Y is flipped vs cameraUp (same convention as the pinned-measurement drag).
					const glm::vec3 dragWorldDir = cameraRight * delta.x - cameraUp * delta.y;
					const float dragLength = glm::length(dragWorldDir);
					if (dragLength > 0.0001f)
					{
						const glm::vec3 rotationAxis = glm::normalize(glm::cross(cameraForward, dragWorldDir));
						constexpr float kRadiansPerPixel = 0.006f;
						const glm::quat rotation = glm::angleAxis(glm::length(delta) * kRadiansPerPixel, rotationAxis);
						for (const std::size_t atomIndex : windowState.selectedAtomIndices)
						{
							if (atomIndex >= windowState.structure.atoms.size())
								continue;
							RendererAtomData &atom = windowState.structure.atoms[atomIndex];
							atom.cartesianPosition = pivot + rotation * (atom.cartesianPosition - pivot);
						}
					}
					windowState.gizmoDragActive = true;
					return true;
				}

				windowState.fallbackGizmoDragging = false;
				windowState.fallbackGizmoAxis = -1;
			}

			// Blender-style modal axis-locked rotate: pressing X/Y/Z with no mouse button starts a
			// rotation constrained to that world axis, following the mouse's angular motion around
			// the pivot - same modal convention as translate/scale below (confirm with a left-click,
			// cancel with Escape/right-click). Angular instead of linear since this trackball has no
			// discrete per-axis handle to click, only the modal (keypress-first) path applies here.
			if (hovered && pivotOnScreen && !windowState.fallbackGizmoDragging)
			{
				constexpr ImGuiKey kModalAxisKeys[3] = {ImGuiKey_X, ImGuiKey_Y, ImGuiKey_Z};
				for (int axis = 0; axis < 3; ++axis)
				{
					if (!ImGui::IsKeyPressed(kModalAxisKeys[axis], false))
						continue;
					windowState.fallbackGizmoDragging = true;
					windowState.fallbackModalDrag = true;
					windowState.fallbackGizmoAxis = axis;
					windowState.fallbackLastMousePos = mousePos;
					windowState.fallbackNumericInput.clear();
					windowState.fallbackDragStartPositions.clear();
					for (const std::size_t atomIndex : windowState.selectedAtomIndices)
						windowState.fallbackDragStartPositions.push_back(windowState.structure.atoms[atomIndex].cartesianPosition);
					break;
				}
			}

			if (windowState.fallbackGizmoDragging && windowState.fallbackGizmoAxis >= 0 && windowState.fallbackGizmoAxis <= 2)
			{
				if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsMouseClicked(ImGuiMouseButton_Right))
				{
					for (std::size_t i = 0;
						 i < windowState.selectedAtomIndices.size() && i < windowState.fallbackDragStartPositions.size();
						 ++i)
					{
						windowState.structure.atoms[windowState.selectedAtomIndices[i]].cartesianPosition =
							windowState.fallbackDragStartPositions[i];
					}
					windowState.fallbackGizmoDragging = false;
					windowState.fallbackModalDrag = false;
					windowState.fallbackGizmoAxis = -1;
					windowState.fallbackNumericInput.clear();
					windowState.gizmoDragActive = false;
					return true;
				}

				// Blender-style axis switch: pressing a different X/Y/Z re-points the lock without ending
				// the drag - matches translate/scale's override toggle, except rotate has no separate
				// "grabbed handle" baseline to release back to (this path only starts via modal X/Y/Z),
				// so re-pressing the SAME key is a no-op here instead of a release.
				constexpr ImGuiKey kRotateAxisKeys[3] = {ImGuiKey_X, ImGuiKey_Y, ImGuiKey_Z};
				for (int axis = 0; axis < 3; ++axis)
				{
					if (axis != windowState.fallbackGizmoAxis && ImGui::IsKeyPressed(kRotateAxisKeys[axis], false))
						windowState.fallbackGizmoAxis = axis;
				}

				CaptureGizmoNumericInput(windowState);
				const bool numericActive = !windowState.fallbackNumericInput.empty();
				const bool numericConfirmed = numericActive &&
					(ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false));
				const bool modalConfirmed = ImGui::IsMouseClicked(ImGuiMouseButton_Left) || numericConfirmed;
				if (pivotOnScreen)
				{
					const glm::vec3 cameraForward = -glm::vec3(view[0][2], view[1][2], view[2][2]);
					const glm::vec3 lockedAxisWorld = kWorldAxes[windowState.fallbackGizmoAxis];
					// Screen Y is flipped vs standard math convention, and a right-hand rotation
					// around an axis pointing away from the viewer (into the screen) reads as
					// clockwise on-screen - both flips cancel out when the axis points toward the
					// viewer instead, so only one sign check is needed here.
					const float rotationSign = glm::dot(lockedAxisWorld, cameraForward) >= 0.0f ? -1.0f : 1.0f;

					if (numericActive)
					{
						// Typed degrees apply ABSOLUTE from the pre-drag snapshot (not accumulated),
						// same reasoning as the translate/scale numeric path below. Pivot MUST be the
						// frozen start-of-drag centroid (MeanPosition of the snapshot), not the live
						// `pivot` above - see MeanPosition's comment for why mixing the two diverges.
						const glm::vec3 numericPivot = MeanPosition(windowState.fallbackDragStartPositions);
						const glm::quat rotation = glm::angleAxis(
							glm::radians(ParseTypedNumber(windowState.fallbackNumericInput)) * rotationSign, lockedAxisWorld);
						for (std::size_t i = 0;
							 i < windowState.selectedAtomIndices.size() && i < windowState.fallbackDragStartPositions.size();
							 ++i)
						{
							windowState.structure.atoms[windowState.selectedAtomIndices[i]].cartesianPosition =
								numericPivot + rotation * (windowState.fallbackDragStartPositions[i] - numericPivot);
						}
					}
					else
					{
						const glm::vec2 fromPivotLast = windowState.fallbackLastMousePos - pivotScreen;
						const glm::vec2 fromPivotNow = mousePos - pivotScreen;
						if (glm::length(fromPivotLast) > 1.0f && glm::length(fromPivotNow) > 1.0f)
						{
							const float lastAngle = std::atan2(fromPivotLast.y, fromPivotLast.x);
							const float nowAngle = std::atan2(fromPivotNow.y, fromPivotNow.x);
							float deltaAngle = nowAngle - lastAngle;
							while (deltaAngle > glm::pi<float>())
								deltaAngle -= glm::two_pi<float>();
							while (deltaAngle < -glm::pi<float>())
								deltaAngle += glm::two_pi<float>();

							const glm::quat rotation = glm::angleAxis(deltaAngle * rotationSign, lockedAxisWorld);
							for (const std::size_t atomIndex : windowState.selectedAtomIndices)
							{
								if (atomIndex >= windowState.structure.atoms.size())
									continue;
								RendererAtomData &atom = windowState.structure.atoms[atomIndex];
								atom.cartesianPosition = pivot + rotation * (atom.cartesianPosition - pivot);
							}
						}
					}
					windowState.fallbackLastMousePos = mousePos;

					ImDrawList *lockDrawList = ImGui::GetWindowDrawList();
					lockDrawList->AddCircle(
						ImVec2(pivotScreen.x, pivotScreen.y), kPickMaxDistance,
						kAxisLockColors[windowState.fallbackGizmoAxis], 64, 3.0f);
					if (numericActive)
					{
						char label[64];
						std::snprintf(label, sizeof(label), "Rotate %c: %s deg", "XYZ"[windowState.fallbackGizmoAxis],
							windowState.fallbackNumericInput.c_str());
						ImGui::GetForegroundDrawList()->AddText(
							ImVec2(pivotScreen.x + 12.0f, pivotScreen.y - 24.0f), IM_COL32(255, 230, 60, 255), label);
					}
				}
				windowState.gizmoDragActive = true;

				if (modalConfirmed)
				{
					windowState.fallbackGizmoDragging = false;
					windowState.fallbackModalDrag = false;
					windowState.fallbackGizmoAxis = -1;
					windowState.fallbackNumericInput.clear();
					// Falls through to the shared commit block below instead of returning - matches
					// the translate/scale modal-confirm convention (no separate "release" frame).
				}
				else
				{
					return true;
				}
			}

			if (!windowState.gizmoDragActive)
				return hoveringRing;
		}
		else
		{
			// A 1-world-unit probe gives axis direction + a pixels-per-world ratio for this frame's
			// zoom. Computed every frame (not just at pick time) so the X/Y/Z axis-lock override
			// below can re-derive its direction as the camera moves during a drag, and so hovering
			// (no click yet) can still report an accurate axis for the capturing return value.
			glm::vec2 axisScreenDir[3];
			float axisPixelsPerWorld[3] = {1.0f, 1.0f, 1.0f};
			bool axisValid[3] = {false, false, false};
			if (pivotOnScreen)
			{
				for (int axis = 0; axis < 3; ++axis)
				{
					glm::vec2 probeScreen;
					if (!projectToScreen(pivot + kWorldAxes[axis], probeScreen))
						continue;
					const glm::vec2 axisVec = probeScreen - pivotScreen;
					const float axisPixels = glm::length(axisVec);
					if (axisPixels < 1.0f)
						continue;
					axisScreenDir[axis] = axisVec / axisPixels;
					axisPixelsPerWorld[axis] = axisPixels;
					axisValid[axis] = true;
				}
			}

			// Always-on red/green/blue axis indicators (item: "show red/green/blue axis") - this is
			// now the ONLY gizmo visual for translate/scale (see Manipulate() above), so it carries
			// the full "Blender-style" look on its own: a thick shaft plus a solid triangular
			// arrowhead per axis, no plane-drag quads (this app has no plane-drag interaction to
			// advertise). Short handles at rest, replaced by the full-length lock line below once a
			// drag actually locks onto one.
			if (pivotOnScreen && !windowState.fallbackGizmoDragging)
			{
				ImDrawList *axisDrawList = ImGui::GetWindowDrawList();
				constexpr float kArrowHeadLength = 16.0f;
				constexpr float kArrowHeadHalfWidth = 6.0f;
				for (int axis = 0; axis < 3; ++axis)
				{
					if (!axisValid[axis])
						continue;
					const glm::vec2 dir = axisScreenDir[axis];
					const glm::vec2 perp(-dir.y, dir.x);
					const glm::vec2 tip = glm::vec2(pivotScreen.x, pivotScreen.y) + dir * kPickMaxDistance;
					const glm::vec2 headBase = tip - dir * kArrowHeadLength;
					axisDrawList->AddLine(ImVec2(pivotScreen.x, pivotScreen.y), ImVec2(headBase.x, headBase.y),
						kAxisLockColors[axis], 3.5f);
					const glm::vec2 headLeft = headBase + perp * kArrowHeadHalfWidth;
					const glm::vec2 headRight = headBase - perp * kArrowHeadHalfWidth;
					axisDrawList->AddTriangleFilled(
						ImVec2(tip.x, tip.y), ImVec2(headLeft.x, headLeft.y), ImVec2(headRight.x, headRight.y),
						kAxisLockColors[axis]);
				}
				axisDrawList->AddCircleFilled(ImVec2(pivotScreen.x, pivotScreen.y), 5.0f, IM_COL32(235, 235, 235, 255));
			}

			// Blender-style modal move/scale: pressing X/Y/Z with NO mouse button held starts a drag
			// constrained to that axis immediately, following the mouse freely - confirmed with a
			// left-click, cancelled (reverting to the pre-drag snapshot) with Escape/right-click. This
			// is in addition to the click-and-drag-a-handle path below, not a replacement for it.
			if (hovered && pivotOnScreen && !windowState.fallbackGizmoDragging)
			{
				constexpr ImGuiKey kModalAxisKeys[3] = {ImGuiKey_X, ImGuiKey_Y, ImGuiKey_Z};
				for (int axis = 0; axis < 3; ++axis)
				{
					if (!axisValid[axis] || !ImGui::IsKeyPressed(kModalAxisKeys[axis], false))
						continue;
					windowState.fallbackGizmoDragging = true;
					windowState.fallbackModalDrag = true;
					windowState.fallbackGizmoAxis = axis;
					// Deliberately NOT set here - the "Blender-style axis lock" toggle loop below runs
					// this same frame (fallbackGizmoDragging is already true) and would immediately see
					// this same X/Y/Z keypress and toggle it straight back off (armed here, disarmed
					// there, both reading the same still-true IsKeyPressed for one physical press) if it
					// were pre-armed here too. Leaving it at its previous value (-1 the first time) lets
					// that loop be the ONLY place that arms it, so the lock/full-length line shows from
					// the very first press instead of needing a second one.
					windowState.fallbackLastMousePos = mousePos;
					windowState.fallbackNumericInput.clear();
					windowState.fallbackDragAxisScreenDir = axisScreenDir[axis];
					windowState.fallbackDragAxisWorldDir = kWorldAxes[axis];
					windowState.fallbackDragPixelsPerWorld = std::max(1.0f, axisPixelsPerWorld[axis]);
					windowState.fallbackDragStartPositions.clear();
					for (const std::size_t atomIndex : windowState.selectedAtomIndices)
						windowState.fallbackDragStartPositions.push_back(windowState.structure.atoms[atomIndex].cartesianPosition);
					break;
				}
			}

			int hoveredAxis = -1;
			if (pivotOnScreen && !windowState.fallbackGizmoDragging && !atomOrBondUnderCursor)
			{
				constexpr float kPickPerpTolerance = 16.0f;
				const glm::vec2 fromPivot = mousePos - pivotScreen;
				const float radial = glm::length(fromPivot);
				float bestPerp = kPickPerpTolerance;
				if (radial >= kPickMinDistance && radial <= kPickMaxDistance)
				{
					for (int axis = 0; axis < 3; ++axis)
					{
						if (!axisValid[axis])
							continue;
						const float along = glm::dot(fromPivot, axisScreenDir[axis]);
						if (along <= 0.0f)
							continue;
						const float perp = glm::length(fromPivot - axisScreenDir[axis] * along);
						if (perp < bestPerp)
						{
							bestPerp = perp;
							hoveredAxis = axis;
						}
					}
				}

				if (hoveredAxis >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
				{
					windowState.fallbackGizmoDragging = true;
					windowState.fallbackModalDrag = false;
					windowState.fallbackGizmoAxis = hoveredAxis;
					windowState.fallbackAxisLockOverride = -1;
					windowState.fallbackLastMousePos = mousePos;
					windowState.fallbackNumericInput.clear();
					windowState.fallbackDragAxisScreenDir = axisScreenDir[hoveredAxis];
					windowState.fallbackDragAxisWorldDir = kWorldAxes[hoveredAxis];
					windowState.fallbackDragPixelsPerWorld = std::max(1.0f, axisPixelsPerWorld[hoveredAxis]);
					windowState.fallbackDragStartPositions.clear();
					for (const std::size_t atomIndex : windowState.selectedAtomIndices)
						windowState.fallbackDragStartPositions.push_back(windowState.structure.atoms[atomIndex].cartesianPosition);
				}
			}

			if (windowState.fallbackGizmoDragging && windowState.fallbackGizmoAxis >= 0)
			{
				// Blender-style axis lock: X/Y/Z re-point the drag at a single world axis regardless
				// of which handle was originally grabbed; pressing the same key again releases the
				// override back to the grabbed axis. No local-space double-tap (Blender's XX/YY/ZZ) -
				// atoms carry no per-object orientation, so a "local" axis would just equal the
				// global one here.
				constexpr ImGuiKey kAxisKeys[3] = {ImGuiKey_X, ImGuiKey_Y, ImGuiKey_Z};
				for (int axis = 0; axis < 3; ++axis)
				{
					if (ImGui::IsKeyPressed(kAxisKeys[axis], false))
						windowState.fallbackAxisLockOverride = windowState.fallbackAxisLockOverride == axis ? -1 : axis;
				}

				const int lockedAxis = windowState.fallbackAxisLockOverride;
				if (lockedAxis >= 0 && axisValid[lockedAxis])
				{
					windowState.fallbackDragAxisScreenDir = axisScreenDir[lockedAxis];
					windowState.fallbackDragAxisWorldDir = kWorldAxes[lockedAxis];
					windowState.fallbackDragPixelsPerWorld = std::max(1.0f, axisPixelsPerWorld[lockedAxis]);

					if (pivotOnScreen)
					{
						ImDrawList *drawList = ImGui::GetWindowDrawList();
						const glm::vec2 dir = axisScreenDir[lockedAxis];
						const ImVec2 farA(pivotScreen.x - dir.x * 10000.0f, pivotScreen.y - dir.y * 10000.0f);
						const ImVec2 farB(pivotScreen.x + dir.x * 10000.0f, pivotScreen.y + dir.y * 10000.0f);
						drawList->AddLine(farA, farB, kAxisLockColors[lockedAxis], 3.0f);
					}
				}

				// Cancel: Escape or right-click reverts to the pre-drag snapshot and ends the drag
				// without committing - works for both a modal drag and a click-drag (Blender lets you
				// abort either the same way), though in practice a click-drag's short lifetime makes
				// this mostly a modal-drag affordance.
				if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsMouseClicked(ImGuiMouseButton_Right))
				{
					for (std::size_t i = 0;
						 i < windowState.selectedAtomIndices.size() && i < windowState.fallbackDragStartPositions.size();
						 ++i)
					{
						windowState.structure.atoms[windowState.selectedAtomIndices[i]].cartesianPosition =
							windowState.fallbackDragStartPositions[i];
					}
					windowState.fallbackGizmoDragging = false;
					windowState.fallbackModalDrag = false;
					windowState.fallbackGizmoAxis = -1;
					windowState.fallbackAxisLockOverride = -1;
					windowState.fallbackNumericInput.clear();
					windowState.gizmoDragActive = false;
					return true;
				}

				CaptureGizmoNumericInput(windowState);
				const bool numericActive = !windowState.fallbackNumericInput.empty();
				const bool numericConfirmed = numericActive &&
					(ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false));

				// A modal drag (started by X/Y/Z with no button held) applies every frame regardless
				// of mouse-button state and confirms on left-click; a click-drag keeps applying only
				// while the button stays down and commits on release - both fall through to the same
				// apply step below, they just disagree on when "still active" is true. Once a number is
				// being typed, the drag stays active regardless of mouse state (mirroring a modal drag)
				// until Enter confirms or Escape cancels - Blender lets you type a number after either
				// starting method.
				const bool modalConfirmed =
					(windowState.fallbackModalDrag && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) || numericConfirmed;
				const bool stillActive = numericActive ? true
					: (windowState.fallbackModalDrag ? !modalConfirmed : ImGui::IsMouseDown(ImGuiMouseButton_Left));

				if (stillActive || modalConfirmed)
				{
					float deltaOnAxisWorld;
					if (numericActive)
					{
						// Typed value applies ABSOLUTE from the pre-drag snapshot below (not accumulated
						// frame over frame the way the mouse-delta path is), so it's computed once here
						// and reused as an absolute offset/factor per atom.
						deltaOnAxisWorld = ParseTypedNumber(windowState.fallbackNumericInput);
					}
					else
					{
						const glm::vec2 delta = mousePos - windowState.fallbackLastMousePos;
						const float deltaOnAxisPixels = glm::dot(delta, windowState.fallbackDragAxisScreenDir);
						deltaOnAxisWorld = deltaOnAxisPixels / windowState.fallbackDragPixelsPerWorld;
					}
					windowState.fallbackLastMousePos = mousePos;

					if (operation == ImGuizmo::SCALE)
					{
						// Typed number IS the scale factor itself (Blender's "S 2 Enter" means 2x, not
						// 1+2) - everyone else still accumulates 1+delta incrementally onto the live
						// position, so factor and "from what" differ between the two paths.
						const float factor = numericActive ? glm::clamp(deltaOnAxisWorld, 0.05f, 20.0f)
															: glm::clamp(1.0f + deltaOnAxisWorld, 0.05f, 20.0f);
						// Numeric path needs the frozen start-of-drag centroid, not the live `pivot`
						// above - same feedback-loop reasoning as the rotate numeric path (see
						// MeanPosition's comment): mixing a frozen basePosition with a pivot recomputed
						// from that same frozen data's own (already-scaled) output diverges whenever the
						// typed factor is outside (0, 2).
						const glm::vec3 scalePivot = numericActive ? MeanPosition(windowState.fallbackDragStartPositions) : pivot;
						for (std::size_t i = 0; i < windowState.selectedAtomIndices.size(); ++i)
						{
							const std::size_t atomIndex = windowState.selectedAtomIndices[i];
							if (atomIndex >= windowState.structure.atoms.size())
								continue;
							const glm::vec3 &basePosition = numericActive && i < windowState.fallbackDragStartPositions.size()
								? windowState.fallbackDragStartPositions[i]
								: windowState.structure.atoms[atomIndex].cartesianPosition;
							const glm::vec3 relative = basePosition - scalePivot;
							const float along = glm::dot(relative, windowState.fallbackDragAxisWorldDir);
							const glm::vec3 perpendicular = relative - windowState.fallbackDragAxisWorldDir * along;
							windowState.structure.atoms[atomIndex].cartesianPosition =
								scalePivot + perpendicular + windowState.fallbackDragAxisWorldDir * (along * factor);
						}
					}
					else
					{
						const glm::vec3 worldDelta = windowState.fallbackDragAxisWorldDir * deltaOnAxisWorld;
						for (std::size_t i = 0; i < windowState.selectedAtomIndices.size(); ++i)
						{
							const std::size_t atomIndex = windowState.selectedAtomIndices[i];
							if (atomIndex >= windowState.structure.atoms.size())
								continue;
							windowState.structure.atoms[atomIndex].cartesianPosition = numericActive &&
									i < windowState.fallbackDragStartPositions.size()
								? windowState.fallbackDragStartPositions[i] + worldDelta
								: windowState.structure.atoms[atomIndex].cartesianPosition + worldDelta;
						}
					}
					windowState.gizmoDragActive = true;

					if (numericActive && pivotOnScreen)
					{
						char label[64];
						const int effectiveAxis = windowState.fallbackAxisLockOverride >= 0
							? windowState.fallbackAxisLockOverride
							: windowState.fallbackGizmoAxis;
						std::snprintf(label, sizeof(label), "%s %c: %s",
							operation == ImGuizmo::SCALE ? "Scale" : "Move",
							"XYZ"[effectiveAxis], windowState.fallbackNumericInput.c_str());
						ImGui::GetForegroundDrawList()->AddText(
							ImVec2(pivotScreen.x + 12.0f, pivotScreen.y - 24.0f), IM_COL32(255, 230, 60, 255), label);
					}

					if (modalConfirmed)
					{
						windowState.fallbackGizmoDragging = false;
						windowState.fallbackModalDrag = false;
						windowState.fallbackGizmoAxis = -1;
						windowState.fallbackAxisLockOverride = -1;
						windowState.fallbackNumericInput.clear();
						// Fall through to the shared commit block below instead of returning - a
						// confirming click ends the drag the same frame, no separate "release" frame
						// exists for a modal drag the way there is for a held button.
					}
					else
					{
						return true;
					}
				}
				else
				{
					windowState.fallbackGizmoDragging = false;
					windowState.fallbackGizmoAxis = -1;
					windowState.fallbackAxisLockOverride = -1;
				}
			}

			if (!windowState.gizmoDragActive)
				return hoveredAxis >= 0;
		}

		windowState.gizmoDragActive = false;

		// An unregistered preview window (New Structure wizard) has no domain structure to commit
		// to, and ResolveAtomEditTarget rightly refuses it. The drag has already moved the atoms in
		// windowState; the wizard reads those positions back into its basis table, which is the
		// single source of truth there. Committing would only log a failure every time.
		if (windowState.structure.domainStructureId.empty())
			return false;

		Ref<CommandRegistry> commandRegistry = commandRegistryRef.lock();
		if (commandRegistry == nullptr)
			return false;

		GizmoTransformPayload payload;
		payload.windowId = windowState.windowId;
		payload.atomIndices = windowState.selectedAtomIndices;
		payload.afterPositions.reserve(payload.atomIndices.size());
		for (const std::size_t atomIndex : payload.atomIndices)
			payload.afterPositions.push_back(windowState.structure.atoms[atomIndex].cartesianPosition);
		payload.description = windowState.gizmoOperation == GizmoOperation::Translate ? "Move selected atoms"
			: windowState.gizmoOperation == GizmoOperation::Rotate                    ? "Rotate selected atoms"
																						: "Scale selected atoms";

		CommandContext context;
		context.Set<GizmoTransformPayload>("gizmo.transform_payload", std::move(payload));
		Result<CommandOutcome> result = commandRegistry->Execute(CommandID{"renderer.gizmo.commit_transform"}, std::move(context));
		if (!result)
			DS_LOG_WARN("Gizmo transform commit failed: {}", result.Error().technicalDetails);
		return false;
	}
} // namespace DefectStudio
