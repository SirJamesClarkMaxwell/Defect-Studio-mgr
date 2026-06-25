#include "Core/dspch.hpp"

#include "Presentation/Panels/RendererPanel.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

#include <glm/gtc/quaternion.hpp>
#include <imgui.h>

#include "Core/Utils/Logger.hpp"
#include "Renderer/RendererViewCamera.hpp"

namespace DefectStudio
{
	namespace
	{
		constexpr float kViewportMinSize = 64.0f;
		constexpr float kViewportMaxSize = 8192.0f;

		using PeriodicTableRow = std::array<const char *, 18>;
		const std::array<PeriodicTableRow, 7> kPeriodicTableRows = {
			PeriodicTableRow{"H", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "He"},
			PeriodicTableRow{"Li", "Be", "", "", "", "", "", "", "", "", "", "", "B", "C", "N", "O", "F", "Ne"},
			PeriodicTableRow{"Na", "Mg", "", "", "", "", "", "", "", "", "", "", "Al", "Si", "P", "S", "Cl", "Ar"},
			PeriodicTableRow{"K", "Ca", "Sc", "Ti", "V", "Cr", "Mn", "Fe", "Co", "Ni", "Cu", "Zn", "Ga", "Ge", "As", "Se", "Br", "Kr"},
			PeriodicTableRow{"Rb", "Sr", "Y", "Zr", "Nb", "Mo", "Tc", "Ru", "Rh", "Pd", "Ag", "Cd", "In", "Sn", "Sb", "Te", "I", "Xe"},
			PeriodicTableRow{"Cs", "Ba", "", "Hf", "Ta", "W", "Re", "Os", "Ir", "Pt", "Au", "Hg", "Tl", "Pb", "Bi", "Po", "At", "Rn"},
			PeriodicTableRow{"Fr", "Ra", "", "Rf", "Db", "Sg", "Bh", "Hs", "Mt", "Ds", "Rg", "", "", "", "", "", "", ""}};

		const std::array<const char *, 15> kLanthanides = {
			"La", "Ce", "Pr", "Nd", "Pm", "Sm", "Eu", "Gd", "Tb", "Dy", "Ho", "Er", "Tm", "Yb", "Lu"};
		const std::array<const char *, 15> kActinides = {
			"Ac", "Th", "Pa", "U", "Np", "Pu", "Am", "Cm", "Bk", "Cf", "Es", "Fm", "Md", "No", "Lr"};

		[[nodiscard]] float SanitizeViewportDimension(float value)
		{
			if (!std::isfinite(value))
				return 640.0f;
			return std::clamp(value, kViewportMinSize, kViewportMaxSize);
		}

		float ComputeCameraTransitionDurationSeconds(float rotationSpeed)
		{
			const float safeSpeed = std::max(0.1f, rotationSpeed);
			return std::clamp(0.14f / safeSpeed, 0.02f, 0.50f);
		}

	}

	RendererPanel::RendererPanel(RendererLayer &layer)
		: m_Layer(layer)
	{
	}

	void RendererPanel::Render(float deltaTime)
	{
		if (!m_Layer.IsAttached())
			return;

		for (RendererWindowState &windowState : m_Layer.GetWindows())
			renderStructureWindow(windowState, deltaTime);

		// drawPeriodicTableWindow();
		m_Layer.CollectProfilingData();
	}

	void RendererPanel::renderStructureWindow(RendererWindowState &windowState, float deltaTime)
	{
		if (windowState.camera == nullptr)
			return;

		updateCameraTransition(windowState, deltaTime);

		const bool began = ImGui::Begin(windowState.title.c_str());
		if (!began)
		{
			ImGui::End();
			return;
		}

		drawViewportToolbar(windowState);
		ImGui::Separator();

		const ImVec2 available = ImGui::GetContentRegionAvail();
		windowState.viewportSize.x = SanitizeViewportDimension(available.x);
		windowState.viewportSize.y = SanitizeViewportDimension(available.y);
		windowState.camera->SetViewport(windowState.viewportSize.x, windowState.viewportSize.y);

		const ImVec2 imageOrigin = ImGui::GetCursorScreenPos();

		const unsigned int textureId = m_Layer.RenderToFbo(
			windowState.title,
			windowState.structure,
			windowState,
			m_Layer.GetGlobalSettings());

		ImGui::Image(
			static_cast<ImTextureID>(static_cast<uintptr_t>(textureId)),
			windowState.viewportSize,
			ImVec2(0.0f, 1.0f),
			ImVec2(1.0f, 0.0f));

		const bool hovered = ImGui::IsItemHovered();
		if (hovered)
		{
			applyViewportInputNavigation(windowState, imageOrigin, deltaTime);

			ImGuiIO &io = ImGui::GetIO();
			const bool leftClicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
				!ImGui::IsMouseDragging(ImGuiMouseButton_Left) &&
				!io.KeyAlt;
			if (leftClicked)
			{
				const ImVec2 mousePos = ImGui::GetMousePos();
				const float relX = mousePos.x - imageOrigin.x;
				const float relY = mousePos.y - imageOrigin.y;
				if (relX >= 0.0f &&
					relY >= 0.0f &&
					relX < windowState.viewportSize.x &&
					relY < windowState.viewportSize.y)
				{
					handleAtomPick(windowState, relX, relY, io.KeyCtrl);
				}
			}
		}
		else
		{
			windowState.dragActive = false;
			if (windowState.viewInteractionActive &&
				windowState.viewInteractionSource.rfind("mouse.", 0) == 0)
			{
				commitViewInteraction(windowState);
			}
		}

		applyViewportKeyboardNavigation(windowState);

		ImGui::SetCursorScreenPos(imageOrigin);
		ImGui::End();
	}

	void RendererPanel::handleAtomPick(RendererWindowState &windowState, float relX, float relY, bool additive)
	{
		if (!windowState.camera || windowState.structure.atoms.empty())
			return;

		const float vpW = windowState.viewportSize.x;
		const float vpH = windowState.viewportSize.y;
		if (vpW <= 0.0f || vpH <= 0.0f)
			return;

		const float ndcX = (2.0f * relX / vpW) - 1.0f;
		const float ndcY = -((2.0f * relY / vpH) - 1.0f);

		const glm::mat4 invVP = glm::inverse(
			windowState.camera->ProjectionMatrix() *
			windowState.camera->ViewMatrix());

		const glm::vec4 nearH = invVP * glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
		const glm::vec4 farH = invVP * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
		const glm::vec3 rayOrigin = glm::vec3(nearH) / nearH.w;
		const glm::vec3 rayDir = glm::normalize(glm::vec3(farH) / farH.w - rayOrigin);

		float bestT = std::numeric_limits<float>::max();
		std::size_t hitIndex = std::numeric_limits<std::size_t>::max();

		for (std::size_t i = 0; i < windowState.structure.atoms.size(); ++i)
		{
			const RendererAtomData &atom = windowState.structure.atoms[i];
			const glm::vec3 oc = rayOrigin - atom.cartesianPosition;
			const float a = glm::dot(rayDir, rayDir);
			const float b = 2.0f * glm::dot(oc, rayDir);
			const float c = glm::dot(oc, oc) - atom.radius * atom.radius;
			const float disc = b * b - 4.0f * a * c;
			if (disc < 0.0f)
				continue;
			const float t = (-b - std::sqrt(disc)) / (2.0f * a);
			if (t > 0.001f && t < bestT)
			{
				bestT = t;
				hitIndex = i;
			}
		}

		if (hitIndex == std::numeric_limits<std::size_t>::max())
		{
			if (!additive)
				windowState.selectedAtomIndices.clear();
			return;
		}

		if (!additive)
		{
			windowState.selectedAtomIndices.clear();
			windowState.selectedAtomIndices.push_back(hitIndex);
			return;
		}

		auto it = std::find(
			windowState.selectedAtomIndices.begin(),
			windowState.selectedAtomIndices.end(),
			hitIndex);
		if (it != windowState.selectedAtomIndices.end())
			windowState.selectedAtomIndices.erase(it);
		else
			windowState.selectedAtomIndices.push_back(hitIndex);
	}

	void RendererPanel::drawPeriodicTableWindow()
	{
		if (!m_Layer.GetShowPeriodicTableWindow())
			return;

		ImGui::SetNextWindowSize(ImVec2(760.0f, 430.0f), ImGuiCond_FirstUseEver);
		if (!ImGui::Begin("Periodic Table", &m_Layer.GetShowPeriodicTableWindow()))
		{
			ImGui::End();
			return;
		}

		const ImVec2 cellSize(36.0f, 32.0f);
		for (const PeriodicTableRow &row : kPeriodicTableRows)
		{
			for (std::size_t column = 0; column < row.size(); ++column)
			{
				const char *symbol = row[column];
				if (column > 0)
					ImGui::SameLine();

				if (symbol == nullptr || symbol[0] == '\0')
				{
					ImGui::Dummy(cellSize);
					continue;
				}

				if (symbol == m_Layer.GetSelectedPeriodicElement())
				{
					ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.28f, 0.56f, 0.92f, 1.0f));
					ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.34f, 0.64f, 0.98f, 1.0f));
					ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.20f, 0.48f, 0.84f, 1.0f));
				}
				const bool clicked = ImGui::Button(symbol, cellSize);
				if (symbol == m_Layer.GetSelectedPeriodicElement())
					ImGui::PopStyleColor(3);

				if (clicked)
					m_Layer.GetSelectedPeriodicElement() = symbol;
			}
		}

		ImGui::Separator();
		ImGui::TextUnformatted("Lanthanides");
		ImGui::SameLine();
		for (std::size_t index = 0; index < kLanthanides.size(); ++index)
		{
			if (index > 0)
				ImGui::SameLine();
			if (ImGui::Button(kLanthanides[index], cellSize))
				m_Layer.GetSelectedPeriodicElement() = kLanthanides[index];
		}
		ImGui::TextUnformatted("Actinides");
		ImGui::SameLine();
		for (std::size_t index = 0; index < kActinides.size(); ++index)
		{
			if (index > 0)
				ImGui::SameLine();
			if (ImGui::Button(kActinides[index], cellSize))
				m_Layer.GetSelectedPeriodicElement() = kActinides[index];
		}

		ImGui::Separator();
		ImGui::Text("Selected element: %s", m_Layer.GetSelectedPeriodicElement().c_str());

		ImGui::End();
	}

	RendererViewSnapshot RendererPanel::captureViewSnapshot(const RendererWindowState &windowState) const
	{
		RendererViewSnapshot snapshot;
		if (windowState.camera == nullptr)
			return snapshot;

		snapshot.target = windowState.camera->Target();
		snapshot.distance = windowState.camera->Distance();
		snapshot.yaw = windowState.camera->Yaw();
		snapshot.pitch = windowState.camera->Pitch();
		snapshot.roll = windowState.camera->Roll();
		snapshot.projection = windowState.camera->Projection();
		return snapshot;
	}

	void RendererPanel::restoreViewSnapshot(
		RendererWindowState &windowState,
		const RendererViewSnapshot &snapshot,
		const char *sourceAction)
	{
		if (windowState.camera == nullptr)
			return;

		windowState.camera->SetProjection(snapshot.projection);
		windowState.transitionDuration = ComputeCameraTransitionDurationSeconds(
			m_Layer.GetGlobalSettings().rotationSpeed);
		startCameraTransition(
			windowState,
			snapshot.target,
			snapshot.distance,
			snapshot.yaw,
			snapshot.pitch,
			snapshot.roll,
			sourceAction);
	}

} // namespace DefectStudio
