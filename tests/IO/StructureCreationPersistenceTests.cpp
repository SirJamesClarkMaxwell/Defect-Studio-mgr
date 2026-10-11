#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "App/CreationSession.hpp"
#include "App/StructureLifecycleCoordinator.hpp"
#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/Notifications/NotificationEvents.hpp"
#include "Core/Utils/Uuid.hpp"
#include "Domain/DomainLayer.hpp"
#include "../Core/JobSystem/JobSystemTestUtils.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		class StructureCreationPersistenceTests : public ::testing::Test
		{
		protected:
			void SetUp() override
			{
				m_Directory = FileSystem::TempDirectoryPath() / ("structure creation " + ToString(GenerateUuid()));
				ASSERT_TRUE(std::filesystem::create_directory(m_Directory));
				m_Directory = std::filesystem::canonical(m_Directory);
				m_Bus = CreateRef<EventBus>();
				m_Jobs = CreateRef<JobSystem>(m_Bus, 1);
				m_Domain = CreateRef<DomainLayer>();
				m_Sessions = CreateRef<CreationSessionRegistry>();
				m_Coordinator = CreateUnique<StructureLifecycleCoordinator>(
					m_Bus, CreateWeakRef(m_Jobs), CreateWeakRef(m_Domain), m_Sessions);
				m_NotificationSubscription = m_Bus->Subscribe<NotificationRequestedEvent>(
					[this](const NotificationRequestedEvent &event) { m_Notifications.push_back(event.notification); });
				ProjectEvents::ProjectRootsChanged roots;
				roots.roots.push_back(Path::FromResolved(m_Directory));
				m_Bus->Publish(roots);
				m_Session = m_Sessions->Create(CreationMode::FromScratch);
				m_Session->state = CreationSessionState::Ready;
				AtomSite atom;
				atom.species = "Si";
				atom.fractional = glm::vec3(0.25f, 0.5f, 0.125f);
				atom.position = atom.fractional;
				m_Session->draftStructure.atoms.push_back(atom);
			}

			void TearDown() override
			{
				if (m_Jobs != nullptr)
				{
					(void)m_Jobs->ResumeAllJobs();
					m_Jobs->Shutdown();
				}
				m_Coordinator.reset();
				std::error_code error;
				FileSystem::RemoveAll(m_Directory, error);
			}

			void RequestAdd(const std::string &name = "New Si")
			{
				DomainEvents::AddStructureToProjectRequested request;
				request.sessionId = m_Session->sessionId;
				request.structure = m_Session->draftStructure;
				request.displayName = name;
				request.targetDirectory = m_Directory;
				m_Bus->Publish(request);
			}

			bool WaitForAttempt()
			{
				const bool finished = WaitUntil([this] {
					m_Bus->ProcessQueue();
					return !m_Session->activeAttemptId.has_value();
				}, Time::Milliseconds(5000));
				m_Bus->ProcessQueue();
				return finished;
			}

			FilePath Poscar() const { return m_Directory / "New Si" / "POSCAR"; }

			FilePath m_Directory;
			Ref<EventBus> m_Bus;
			Ref<JobSystem> m_Jobs;
			Ref<DomainLayer> m_Domain;
			Ref<CreationSessionRegistry> m_Sessions;
			Ref<CreationSession> m_Session;
			Unique<StructureLifecycleCoordinator> m_Coordinator;
			SubscriptionHandle m_NotificationSubscription;
			std::vector<Notification> m_Notifications;
		};
	} // namespace

	TEST_F(StructureCreationPersistenceTests, WritesAndRegistersBeforePublishingSuccess)
	{
		int successes = 0;
		auto subscription = m_Bus->Subscribe<DomainEvents::ProjectStructureAdded>(
			[&](const DomainEvents::ProjectStructureAdded &event) {
				++successes;
				EXPECT_TRUE(FileSystem::Exists(event.poscarPath));
				EXPECT_FALSE(FileSystem::Exists(event.poscarPath.parent_path() / ".pending_registration"));
				EXPECT_FALSE(m_Domain->Workspace().Structures().Find(event.newStructureId).expired());
			});
		RequestAdd();
		ASSERT_TRUE(WaitForAttempt());
		EXPECT_EQ(m_Session->state, CreationSessionState::Success)
			<< (m_Notifications.empty() ? std::string() : m_Notifications.front().title + ": " + m_Notifications.front().message);
		EXPECT_EQ(successes, 1);
		EXPECT_EQ(m_Domain->Workspace().Structures().Records().size(), 1u);
		EXPECT_TRUE(m_Notifications.empty());
		EXPECT_EQ(FileSystem::ListDirectory(m_Directory).size(), 1u);
		EXPECT_EQ(FileSystem::ListDirectory(Poscar().parent_path()).size(), 1u);
	}

	TEST_F(StructureCreationPersistenceTests, MissingProjectRootNotifiesWithoutWriting)
	{
		ProjectEvents::ProjectRootsChanged roots;
		m_Bus->Publish(roots);
		RequestAdd();
		m_Bus->ProcessQueue();
		EXPECT_TRUE(m_Session->lastError.has_value());
		EXPECT_FALSE(m_Session->activeAttemptId.has_value());
		ASSERT_EQ(m_Notifications.size(), 1u);
		EXPECT_EQ(m_Notifications.front().severity, NotificationSeverity::Error);
		EXPECT_TRUE(FileSystem::ListDirectory(m_Directory).empty());
	}

	TEST_F(StructureCreationPersistenceTests, UnavailableRootAndInvalidNameNotify)
	{
		ProjectEvents::ProjectRootsChanged roots;
		roots.roots.push_back(Path::FromResolved(m_Directory / "missing"));
		m_Bus->Publish(roots);
		RequestAdd("CON");
		m_Bus->ProcessQueue();
		EXPECT_EQ(m_Notifications.size(), 2u);
		EXPECT_TRUE(m_Session->lastError.has_value());
		EXPECT_TRUE(FileSystem::ListDirectory(m_Directory).empty());
	}

	TEST_F(StructureCreationPersistenceTests, MissingTargetNotifiesWithoutWriting)
	{
		DomainEvents::AddStructureToProjectRequested request;
		request.sessionId = m_Session->sessionId;
		request.structure = m_Session->draftStructure;
		request.displayName = "New Si";
		m_Bus->Publish(request);
		m_Bus->ProcessQueue();
		EXPECT_TRUE(m_Session->lastError.has_value());
		EXPECT_EQ(m_Notifications.size(), 1u);
		EXPECT_TRUE(FileSystem::ListDirectory(m_Directory).empty());
	}

	TEST_F(StructureCreationPersistenceTests, DirectoryCollisionNotifiesAndPreservesExistingFile)
	{
		ASSERT_TRUE(std::filesystem::create_directory(Poscar().parent_path()));
		std::ofstream(Poscar()) << "keep me";
		RequestAdd();
		ASSERT_TRUE(WaitForAttempt());
		EXPECT_EQ(m_Session->state, CreationSessionState::Failed);
		EXPECT_EQ(m_Notifications.size(), 1u);
		EXPECT_TRUE(m_Domain->Workspace().Structures().Records().empty());
		std::ifstream file(Poscar());
		std::string contents;
		std::getline(file, contents);
		EXPECT_EQ(contents, "keep me");
		EXPECT_EQ(FileSystem::ListDirectory(m_Directory).size(), 1u);
	}

	TEST_F(StructureCreationPersistenceTests, RejectedJobSubmissionDoesNotLeaveAnActiveAttempt)
	{
		m_Jobs->Shutdown();
		RequestAdd();
		m_Bus->ProcessQueue();
		EXPECT_EQ(m_Session->state, CreationSessionState::Failed);
		EXPECT_FALSE(m_Session->activeAttemptId.has_value());
		EXPECT_EQ(m_Notifications.size(), 1u);
		EXPECT_TRUE(FileSystem::ListDirectory(m_Directory).empty());
	}

	TEST_F(StructureCreationPersistenceTests, CancelledJobClearsTheAttemptAndNotifies)
	{
		ASSERT_TRUE(m_Jobs->PauseAllJobs());
		RequestAdd();
		const auto jobs = m_Jobs->GetAllJobs();
		ASSERT_EQ(jobs.size(), 1u);
		ASSERT_TRUE(m_Jobs->RequestCancel(jobs.front().id));
		ASSERT_TRUE(m_Jobs->ResumeAllJobs());
		ASSERT_TRUE(WaitForAttempt());
		EXPECT_EQ(m_Session->state, CreationSessionState::Failed);
		EXPECT_EQ(m_Notifications.size(), 1u);
		EXPECT_TRUE(FileSystem::ListDirectory(m_Directory).empty());
	}

	TEST_F(StructureCreationPersistenceTests, DomainDisappearingAfterWriteRollsBackAndNotifies)
	{
		auto subscription = m_Bus->Subscribe<JobCompletedEvent>(
			[&](const JobCompletedEvent &) { m_Domain.reset(); }, EventPriority::Highest);
		RequestAdd();
		ASSERT_TRUE(WaitForAttempt());
		EXPECT_EQ(m_Session->state, CreationSessionState::Failed);
		EXPECT_EQ(m_Notifications.size(), 1u);
		EXPECT_TRUE(FileSystem::ListDirectory(m_Directory).empty());
	}

	TEST_F(StructureCreationPersistenceTests, RegistrationFailureRollsBackAndNotifies)
	{
		auto subscription = m_Bus->Subscribe<JobCompletedEvent>(
			[&](const JobCompletedEvent &) {
				EXPECT_TRUE(m_Domain->Workspace().Structures().RegisterAsProjectMember(
					m_Session->draftStructure, Path::FromResolved(Poscar()), "racing registration"));
			}, EventPriority::Highest);
		RequestAdd();
		ASSERT_TRUE(WaitForAttempt());
		EXPECT_EQ(m_Session->state, CreationSessionState::Failed);
		EXPECT_EQ(m_Notifications.size(), 1u);
		EXPECT_TRUE(FileSystem::ListDirectory(m_Directory).empty());
	}

	TEST_F(StructureCreationPersistenceTests, CancellationAfterCommitRemovesTheUnregisteredDirectory)
	{
		auto subscription = m_Bus->Subscribe<JobCompletedEvent>(
			[&](const JobCompletedEvent &event) {
				ASSERT_TRUE(FileSystem::Exists(Poscar()));
				JobCancelledEvent cancelled{event.id, event.name, event.type, event.finishedAt};
				m_Bus->Publish(cancelled);
			}, EventPriority::Highest);
		RequestAdd();
		ASSERT_TRUE(WaitForAttempt());
		EXPECT_EQ(m_Session->state, CreationSessionState::Failed);
		EXPECT_EQ(m_Notifications.size(), 1u);
		EXPECT_TRUE(m_Domain->Workspace().Structures().Records().empty());
		EXPECT_TRUE(FileSystem::ListDirectory(m_Directory).empty());
	}

	TEST_F(StructureCreationPersistenceTests, FailedSentinelCleanupWarnsAndKeepsTheRegisteredStructure)
	{
		auto subscription = m_Bus->Subscribe<JobCompletedEvent>(
			[&](const JobCompletedEvent &) {
				const auto sentinel = Poscar().parent_path() / ".pending_registration";
				ASSERT_TRUE(FileSystem::Remove(sentinel));
				ASSERT_TRUE(std::filesystem::create_directory(sentinel));
				std::ofstream(sentinel / "keep.txt") << "blocks removal";
			}, EventPriority::Highest);
		RequestAdd();
		ASSERT_TRUE(WaitForAttempt());
		EXPECT_EQ(m_Session->state, CreationSessionState::Success);
		ASSERT_EQ(m_Notifications.size(), 1u);
		EXPECT_EQ(m_Notifications.front().severity, NotificationSeverity::Warn);
		EXPECT_TRUE(FileSystem::Exists(Poscar()));
		EXPECT_EQ(m_Domain->Workspace().Structures().Records().size(), 1u);
	}

	TEST_F(StructureCreationPersistenceTests, InvalidSupercellReturnsAnErrorInsteadOfTheUnitCell)
	{
		m_Session->supercellCounts = glm::ivec3(0, 2, 2);
		const auto exported = BuildSessionExportStructure(*m_Session);
		EXPECT_FALSE(exported);
		EXPECT_TRUE(FileSystem::ListDirectory(m_Directory).empty());
	}

	TEST_F(StructureCreationPersistenceTests, HandingOffAClosedDraftNotifies)
	{
		ASSERT_TRUE(m_Sessions->Remove(m_Session->sessionId));
		DomainEvents::SessionReadyForStructureHub ready;
		ready.sessionId = m_Session->sessionId;
		m_Bus->Publish(ready);
		m_Bus->ProcessQueue();
		EXPECT_EQ(m_Notifications.size(), 1u);
		EXPECT_TRUE(FileSystem::ListDirectory(m_Directory).empty());
	}
} // namespace DefectStudio::Tests
