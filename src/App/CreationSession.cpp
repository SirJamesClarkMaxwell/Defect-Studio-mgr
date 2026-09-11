#include "Core/dspch.hpp"

#include "App/CreationSession.hpp"

#include <algorithm>

#include "Core/Logging/Logger.hpp"
#include "Domain/Crystal/Supercell.hpp"

namespace DefectStudio
{
	const char *ToString(CreationMode mode) noexcept
	{
		switch (mode)
		{
			case CreationMode::FromTemplate: return "From Library";
			case CreationMode::FromScratch: return "Create New";
			case CreationMode::AnalyzeExisting: return "Analyze Existing";
			case CreationMode::ImportFile: return "Import File";
		}
		return "Unknown";
	}

	const char *ToString(CreationSessionState state) noexcept
	{
		switch (state)
		{
			case CreationSessionState::Draft: return "Draft";
			case CreationSessionState::Ready: return "Ready";
			case CreationSessionState::Submitted: return "Submitted";
			case CreationSessionState::Completing: return "Completing";
			case CreationSessionState::Success: return "Success";
			case CreationSessionState::Failed: return "Failed";
			case CreationSessionState::Closing: return "Closing";
		}
		return "Unknown";
	}

	CrystalStructure BuildSessionExportStructure(const CreationSession &session)
	{
		if (session.draftStructure.atoms.empty() || session.supercellCounts == glm::ivec3(1))
			return session.draftStructure;

		Result<CrystalStructure> built = BuildSupercell(
			session.draftStructure,
			SupercellMatrix::Diagonal(
				session.supercellCounts.x, session.supercellCounts.y, session.supercellCounts.z));
		if (!built)
		{
			DS_LOG_WARN("CreationSession: supercell expansion failed, using the unit cell: {}",
				built.Error().technicalDetails);
			return session.draftStructure;
		}
		return std::move(built.Value());
	}

	Ref<CreationSession> CreationSessionRegistry::Create(CreationMode mode)
	{
		Ref<CreationSession> session = CreateRef<CreationSession>();
		session->sessionId = GenerateUuid();
		session->mode = mode;
		session->createdAt = Time::Now();
		session->lastModifiedAt = session->createdAt;
		m_Sessions.push_back(session);
		return session;
	}

	WeakRef<CreationSession> CreationSessionRegistry::Find(const Uuid &sessionId) const
	{
		const auto it = std::find_if(m_Sessions.begin(), m_Sessions.end(), [&](const Ref<CreationSession> &session) {
			return session->sessionId == sessionId;
		});
		if (it == m_Sessions.end())
			return {};
		return *it;
	}

	bool CreationSessionRegistry::Remove(const Uuid &sessionId)
	{
		const auto it = std::find_if(m_Sessions.begin(), m_Sessions.end(), [&](const Ref<CreationSession> &session) {
			return session->sessionId == sessionId;
		});
		if (it == m_Sessions.end())
			return false;

		m_Sessions.erase(it);
		return true;
	}

	const CreationSessionRegistry::SessionList &CreationSessionRegistry::Sessions() const noexcept
	{
		return m_Sessions;
	}
} // namespace DefectStudio
