#include "Core/dspch.hpp"

#include "App/CreationSession.hpp"

#include <algorithm>

namespace DefectStudio
{
	const char *ToString(CreationMode mode) noexcept
	{
		switch (mode)
		{
			case CreationMode::FromTemplate: return "From Template";
			case CreationMode::FromScratch: return "From Scratch";
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
