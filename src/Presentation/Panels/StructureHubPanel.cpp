#include "Core/dspch.hpp"

#include "Presentation/Panels/StructureHubPanel.hpp"

#include <cstring>
#include <utility>

#include <imgui.h>

#include "Core/Domain/StructureLifecycleEvents.hpp"
#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/Utils/PathValidation.hpp"

namespace DefectStudio
{
	namespace
	{
		constexpr ImVec4 SuccessColor{0.0f, 1.0f, 0.0f, 1.0f};
		constexpr ImVec4 ErrorColor{1.0f, 0.35f, 0.35f, 1.0f};
		constexpr ImVec4 WarningColor{1.0f, 0.8f, 0.2f, 1.0f};

		[[nodiscard]] bool SamePath(const Path &left, const Path &right)
		{
			if (left.Empty() || right.Empty())
				return left.Empty() && right.Empty();

			std::error_code error;
			const FilePath leftCanonical = std::filesystem::weakly_canonical(left.Native(), error);
			if (error)
				return left == right;
			const FilePath rightCanonical = std::filesystem::weakly_canonical(right.Native(), error);
			if (error)
				return left == right;
			return leftCanonical == rightCanonical;
		}
	} // namespace

	StructureHubPanel::StructureHubPanel(
		Ref<CreationSessionRegistry> sessionRegistry,
		Ref<EventBus> eventBus,
		std::string title,
		bool visibleByDefault)
		: IPanel(std::move(title), visibleByDefault),
		  m_SessionRegistry(std::move(sessionRegistry)),
		  m_EventBus(std::move(eventBus))
	{
	}

	Ref<IPanel> StructureHubPanel::Clone() const
	{
		return CreateRef<StructureHubPanel>(*this);
	}

	void StructureHubPanel::SetTargetDirectory(const Path &targetDirectory)
	{
		m_TargetDirectory = targetDirectory;
	}

	std::array<char, 128> &StructureHubPanel::nameBufferFor(const CreationSession &session)
	{
		const std::string key = ToString(session.sessionId);
		auto it = m_NameBuffers.find(key);
		if (it != m_NameBuffers.end())
			return it->second;

		std::array<char, 128> buffer{};
		const std::string initial = session.displayName.empty() ? session.draftStructure.name : session.displayName;
		std::snprintf(buffer.data(), buffer.size(), "%s", initial.c_str());
		return m_NameBuffers.emplace(key, buffer).first->second;
	}

	void StructureHubPanel::Render()
	{
		if (!IsVisible())
			return;

		bool windowOpen = true;
		if (!ImGui::Begin(GetTitle().c_str(), &windowOpen))
		{
			ImGui::End();
			SetVisible(windowOpen);
			return;
		}

		if (m_SessionRegistry == nullptr || m_SessionRegistry->Sessions().empty())
		{
			ImGui::TextWrapped(
				"No structures in progress. Build one in the New Structure panel and press "
				"\"Move to Structure Hub\".");
			ImGui::End();
			SetVisible(windowOpen);
			return;
		}

		ImGui::TextUnformatted("Project Tree selection:");
		ImGui::SameLine();
		if (m_TargetDirectory.Empty())
			ImGui::TextDisabled("(none - click a folder in the Project Tree)");
		else
			ImGui::TextWrapped("%s", m_TargetDirectory.String().c_str());
		ImGui::Separator();

		// Copied: adding a structure can close its own session (settings-driven auto-close).
		const CreationSessionRegistry::SessionList sessions = m_SessionRegistry->Sessions();
		for (const Ref<CreationSession> &session : sessions)
		{
			if (session == nullptr || session->state == CreationSessionState::Draft)
				continue; // Still being built in New Structure; not handed over yet
			drawSession(*session);
		}

		ImGui::End();
		SetVisible(windowOpen);
	}

	void StructureHubPanel::drawSession(CreationSession &session)
	{
		const std::string key = ToString(session.sessionId);
		ImGui::PushID(key.c_str());

		const std::string header = std::string(ToString(session.mode)) + "  -  " + ToString(session.state)
			+ "##session_header";
		if (ImGui::CollapsingHeader(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::Text("Atoms: %zu", session.draftStructure.atoms.size());

			std::array<char, 128> &nameBuffer = nameBufferFor(session);
			ImGui::TextUnformatted("Structure name:");
			if (ImGui::InputText("##structure_name", nameBuffer.data(), nameBuffer.size()))
				session.displayName = nameBuffer.data();

			drawTargetSection(session);

			if (session.lastError.has_value())
			{
				ImGui::TextColored(ErrorColor, "%s", session.lastError->userMessage.c_str());
				if (!session.lastError->suggestion.empty())
					ImGui::TextWrapped("%s", session.lastError->suggestion.c_str());
			}
			else if (session.state == CreationSessionState::Success)
			{
				ImGui::TextColored(SuccessColor, "Added to the project.");
			}

			// The button is disabled for an in-flight attempt, but that is only a courtesy: the
			// coordinator refuses a second attempt regardless of what the UI allows.
			const bool busy = session.activeAttemptId.has_value();
			const bool hasName = nameBuffer[0] != '\0';
			const bool hasTarget = !session.targetDirectory.Empty() || !m_TargetDirectory.Empty();
			ImGui::BeginDisabled(busy || !hasName || !hasTarget || session.draftStructure.atoms.empty());
			if (ImGui::Button("Add to Project", {-1, 0}))
				dispatchAddToProject(session);
			ImGui::EndDisabled();

			if (busy)
				ImGui::TextDisabled("Writing structure...");
		}

		ImGui::PopID();
		ImGui::Separator();
	}

	void StructureHubPanel::drawTargetSection(CreationSession &session)
	{
		ImGui::TextUnformatted("Target folder:");
		if (session.targetDirectory.Empty())
		{
			ImGui::TextDisabled("(none captured; the current Project Tree selection will be used)");
			return;
		}

		ImGui::TextWrapped("%s", session.targetDirectory.String().c_str());

		// The folder the session was started against can drift from what the user has since clicked.
		// Silently following the new selection would write the structure somewhere they never asked
		// for, so the change is surfaced and the choice is theirs.
		if (m_TargetDirectory.Empty() || SamePath(session.targetDirectory, m_TargetDirectory))
			return;

		ImGui::TextColored(WarningColor, "Target folder has changed since this structure was started.");
		ImGui::TextWrapped("Project Tree now points at: %s", m_TargetDirectory.String().c_str());
		if (ImGui::Button("Use the new folder"))
		{
			session.targetDirectory = m_TargetDirectory;
			session.lastModifiedAt = Time::Now();
		}
		ImGui::SameLine();
		ImGui::TextDisabled("or leave it to keep the original");
	}

	void StructureHubPanel::dispatchAddToProject(CreationSession &session)
	{
		if (m_EventBus == nullptr)
			return;

		const Path target = session.targetDirectory.Empty() ? m_TargetDirectory : session.targetDirectory;
		const std::string displayName = nameBufferFor(session).data();

		// Cheap local feedback for the obvious mistakes. The coordinator and the job both validate
		// again - this check exists so a typo does not have to round-trip through a background job.
		if (Result<std::string> validated = PathValidation::ValidateAndSanitizeName(displayName); !validated)
		{
			session.lastError = validated.Error();
			return;
		}

		session.displayName = displayName;
		session.targetDirectory = target;
		session.lastError.reset();

		DomainEvents::AddStructureToProjectRequested event;
		event.sessionId = session.sessionId;
		event.structure = session.draftStructure;
		event.displayName = displayName;
		event.targetDirectory = target.Native();
		m_EventBus->Publish(event);
	}
} // namespace DefectStudio
