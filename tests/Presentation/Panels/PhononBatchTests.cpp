#include <gtest/gtest.h>

#include <bit>
#include <filesystem>
#include <fstream>
#include <functional>
#include <utility>
#include <variant>
#include "Core/Utils/Uuid.hpp"
#include "Presentation/Panels/PhononPanelSession.hpp"
#include "Renderer/RendererWindowState.hpp"
#include "../../Core/JobSystem/JobSystemTestUtils.hpp"

namespace DefectStudio::Tests
{
	struct PhononBatchTestAccess
	{
		using Request = std::variant<PhononAnalysisRequest, CcdAnalysisRequest>;
		static void Install(PhononPanelSession &session, std::function<JobId(const Ref<IJob> &)> submit)
		{
			session.m_CaseJobSubmitter = std::move(submit);
		}
		static Request RequestFor(const PhononPanelSession &session, const Ref<IJob> &job)
		{
			if (session.m_BatchEntry) return session.m_BatchEntry->request;
			if (std::dynamic_pointer_cast<PhononAnalysisJob>(job))
				return session.computationRequest(session.Settings(), session.ActiveCaseId());
			return session.ccdAnalysisRequest(session.Settings(), session.ActiveCaseId());
		}
	};

	namespace
	{
		void Write(const Path &directory, const std::string &file, const std::string &text)
		{
			std::filesystem::create_directories(directory.Native());
			std::ofstream out((directory / file).Native());
			out << text;
		}

		void PhononFiles(const Path &directory)
		{
			Write(directory, "summary.json", R"({"schema":"ds.phonon_vibronic.v1","label":"batch",
				"n_atoms":1,"n_modes":3,"imaginary_modes":[],"s_total":1,"debye_waller":0.36,"stable":true,
				"sigma_meV":6,"gamma_meV":2,"max_displacement_A":0.1,"delta_Q_amu05_A":0.2,"approximation":"harmonic",
				"zpl_eV":{"computed":2,"computed_spin_purified":null,"experimental":null}})");
			Write(directory, "structure.json", R"({"symbols":["B"],"masses_amu":[1],"positions_A":[[0,0,0]],
				"cell_A":[[1,0,0],[0,1,0],[0,0,1]]})");
			Write(directory, "modes.tsv", "index\tfreq_meV\tirrep\tn_eff\tbeta\tS_k\tq_k_amu05_A\tis_translation\n"
				"0\t50\tA1\t1\t1\t0.2\t0.1\t0\n1\t60\tE\t1\t1\t0.3\t0.1\t0\n2\t70\tE\t1\t1\t0.5\t0.1\t0\n");
			const auto npy = [&](const std::string &file, const std::string &shape, const std::vector<float> &values) {
				std::string header = "{'descr': '<f4', 'fortran_order': False, 'shape': " + shape + ", }";
				header.append((64 - (10 + header.size() + 1) % 64) % 64, ' ');
				header.push_back('\n');
				std::ofstream out((directory / file).Native(), std::ios::binary);
				out.write("\x93NUMPY\x01\x00", 8);
				out.put(static_cast<char>(header.size() & 255)); out.put(static_cast<char>(header.size() >> 8));
				out << header;
				for (float value : values)
					for (int byte = 0; byte < 4; ++byte)
						out.put(static_cast<char>((std::bit_cast<std::uint32_t>(value) >> (8 * byte)) & 255));
			};
			npy("eigenvectors.npy", "(3, 1, 3)", {1, 0, 0, 0, 1, 0, 0, 0, 1});
			npy("participation.npy", "(3, 1)", {1, 1, 1});
		}

		class StubAnalysis final : public IJob
		{
		public:
			StubAnalysis(Ref<IJob> analysis, Ref<Gate> gate, Path output, bool fail)
				: m_Analysis(std::move(analysis)), m_Gate(std::move(gate)), m_Output(std::move(output)), m_Fail(fail) {}
			std::string GetName() const override { return "Stub phonon/CCD analysis"; }
			std::string GetType() const override { return "test.phonon.batch"; }
			void Execute(JobContext &context) override
			{
				m_Gate->Wait();
				context.ThrowIfCancellationRequested();
				if (m_Fail) throw std::runtime_error("stub analysis failed");
				if (const auto phonons = std::dynamic_pointer_cast<PhononAnalysisJob>(m_Analysis))
				{
					PhononFiles(m_Output);
					phonons->GetResult() = PhononAnalysisRunSummary{m_Output, 1.0, true};
				}
				else
				{
					Write(m_Output, "summary.json", R"({"schema":"ds.ccd_analysis.v1","label":"batch CCD",
						"delta_Q_amu05_A":0.2,"zpl_eV":{"used":2}})");
					const auto ccd = std::dynamic_pointer_cast<CcdAnalysisJob>(m_Analysis);
					// The underlying job is mutable; only its public read accessor is const.
					const_cast<std::optional<CcdAnalysisRunSummary> &>(ccd->GetResult()) = CcdAnalysisRunSummary{m_Output, 2.0};
				}
			}
		private:
			Ref<IJob> m_Analysis;
			Ref<Gate> m_Gate;
			Path m_Output;
			bool m_Fail;
		};
	}

	class PhononBatchTests : public testing::Test
	{
	protected:
		Path root = Path::FromResolved(std::filesystem::temp_directory_path()) / ("ds_phonon_batch_" + ToString(GenerateUuid()));
		Ref<JobSystem> jobs = CreateRef<JobSystem>(Ref<EventBus>{}, 2);
		PhononPanelSession session{jobs};
		std::vector<RendererWindowState> windows;
		std::vector<PhononBatchTestAccess::Request> requests;
		std::vector<Ref<Gate>> gates;
		std::vector<JobId> ids;
		bool failNext = false;

		void SetUp() override
		{
			session.SetPersistedState({}, root);
			PhononBatchTestAccess::Install(session, [&](const Ref<IJob> &analysis) {
				requests.push_back(PhononBatchTestAccess::RequestFor(session, analysis));
				gates.push_back(CreateRef<Gate>());
				const auto output = std::visit([](const auto &request) { return request.outputDirectory; }, requests.back());
				const auto id = jobs->Submit(CreateRef<StubAnalysis>(analysis, gates.back(), output, std::exchange(failNext, false)));
				ids.push_back(id);
				return id;
			});
		}
		void TearDown() override
		{
			session.CancelBatch();
			for (const auto &gate : gates) gate->Open();
			jobs->Shutdown();
			session.SetPersistedState({}, {});
			std::error_code ignored;
			std::filesystem::remove_all(root.Native(), ignored);
		}
		std::string Add(const std::string &name)
		{
			const auto id = session.AddCase(name);
			auto &s = session.Settings();
			s.forceConstantDirectory = root / name / "fc";
			s.groundStateDirectory = root / name / "gs";
			s.excitedStateDirectory = root / name / "es";
			s.groundAtExcitedDirectory = root / name / "cross";
			s.outputDirectory = root / name / "phonons";
			s.ccdAnalysisOutputDirectory = root / name / "ccd";
			s.zplFormula = "2.0";
			for (const auto &path : {s.forceConstantDirectory, s.groundStateDirectory, s.excitedStateDirectory, s.groundAtExcitedDirectory})
				std::filesystem::create_directories(path.Native());
			return id;
		}
		void Tick() { session.Update(windows, 0, false); }
		bool Release(std::size_t index)
		{
			gates.at(index)->Open();
			return WaitUntil([&] {
				const auto job = jobs->GetJob(ids.at(index));
				return job && job->status != JobStatus::Queued && job->status != JobStatus::Running;
			}, Time::Milliseconds(5000));
		}
		const PhononCase &Case(const std::string &id)
		{
			return *std::ranges::find(session.Cases(), id, &PhononCase::id);
		}
	};

	TEST_F(PhononBatchTests, BlockedCaseIsSkippedAndOtherCasesRunStrictlySequentially)
	{
		const auto a = Add("A"), b = Add("B");
		session.Settings().forceConstantDirectory = {};
		const auto c = Add("C");
		ASSERT_TRUE(session.ActivateCase(b));
		session.StartBatch({a, b, c}, {PhononBatchStep::Phonons});
		ASSERT_EQ(ids.size(), 1u);
		EXPECT_EQ(session.CaseRun(b).status, PhononCaseStatus::Failed);
		EXPECT_FALSE(session.CaseRun(b).message.empty());
		EXPECT_TRUE(Case(b).datasets.empty());
		EXPECT_EQ(session.CaseRun(c).status, PhononCaseStatus::Queued);
		for (int i = 0; i < 3; ++i) Tick();
		EXPECT_EQ(ids.size(), 1u);
		ASSERT_TRUE(Release(0));
		Tick();
		ASSERT_EQ(ids.size(), 2u);
		EXPECT_EQ(jobs->GetJob(ids[0])->status, JobStatus::Completed);
		EXPECT_EQ(jobs->GetActiveJobs().size(), 1u);
		ASSERT_TRUE(Release(1));
		Tick();
		EXPECT_EQ(session.CaseRun(a).status, PhononCaseStatus::Done);
		EXPECT_EQ(session.CaseRun(c).status, PhononCaseStatus::Done);
		EXPECT_EQ(session.ActiveCaseId(), b);
		EXPECT_EQ(session.Result(), nullptr);
		EXPECT_EQ(session.Datasets().size(), 2u);
		EXPECT_EQ(session.BatchProgress(), (std::pair<std::size_t, std::size_t>{3, 3}));
		EXPECT_FALSE(session.IsBatchRunning());
	}

	TEST_F(PhononBatchTests, SinglePhononsSurviveCaseSwitchAndLeaveOtherViewUntouched)
	{
		const auto a = Add("A");
		session.SubmitComputation();
		const auto b = Add("B");
		PhononAnalysisResult existing;
		existing.label = "B view";
		session.SetResult(std::move(existing));
		session.Settings().resultDirectory = root / "B/loaded";
		session.SetCurrentDataset(root / "B/current");
		const auto revision = session.PlotRevision();
		ASSERT_TRUE(Release(0));
		Tick();
		EXPECT_EQ(session.ActiveCaseId(), b);
		ASSERT_NE(session.Result(), nullptr);
		EXPECT_EQ(session.Result()->label, "B view");
		EXPECT_EQ(session.Settings().resultDirectory, root / "B/loaded");
		EXPECT_EQ(session.CurrentDataset(), root / "B/current");
		EXPECT_EQ(session.PlotRevision(), revision + 2); // Only attachment/list revisions.
		EXPECT_EQ(Case(a).settings.resultDirectory, root / "A/phonons");
		EXPECT_EQ(session.DatasetCaseId(root / "A/phonons"), a);
		EXPECT_TRUE(Case(b).datasets.empty());
		EXPECT_EQ(session.CaseRun(a).status, PhononCaseStatus::Done);
	}

	TEST_F(PhononBatchTests, QueuedPhononRequestRetainsInputsOutputAndEmbedding)
	{
		const auto a = Add("A"), b = Add("B");
		auto &s = session.Settings();
		s.bulkForceConstantDirectory = root / "bulk";
		std::filesystem::create_directories(s.bulkForceConstantDirectory.Native());
		s.embeddingMult = "36x36x1";
		s.dcutoffAngstrom = 7;
		s.sigmaMeV = 8;
		const auto originalGs = s.groundStateDirectory;
		session.StartBatch({a, b}, {PhononBatchStep::Phonons});
		session.SetDirectory("gs", root / "edited");
		s.embeddingMult = "2x2x2";
		s.dcutoffAngstrom = 3;
		s.sigmaMeV = 20;
		s.outputDirectory = root / "edited-output";
		ASSERT_TRUE(Release(0));
		Tick();
		ASSERT_EQ(requests.size(), 2u);
		const auto &request = std::get<PhononAnalysisRequest>(requests[1]);
		EXPECT_EQ(request.groundStateDirectory, originalGs);
		EXPECT_EQ(request.embeddingMult, (std::array<int, 3>{36, 36, 1}));
		EXPECT_EQ(request.dcutoffAngstrom, 7);
		EXPECT_EQ(request.sigmaMeV, 8);
		EXPECT_EQ(request.outputDirectory, root / "B/phonons");
		ASSERT_TRUE(Release(1));
		Tick();
		EXPECT_EQ(session.Settings().groundStateDirectory, root / "edited");
		EXPECT_EQ(session.Settings().resultDirectory, root / "B/phonons");
		EXPECT_EQ(session.CaseRun(b).status, PhononCaseStatus::Done);
		ASSERT_NE(session.Result(), nullptr);
		EXPECT_EQ(session.Result()->label, "batch");
	}

	TEST_F(PhononBatchTests, CancelMarksRunningAndQueuedAndWaitsForWorkerBeforeReusingRunner)
	{
		const auto a = Add("A"), b = Add("B"), c = Add("C");
		session.StartBatch({a, b, c}, {PhononBatchStep::Phonons});
		ASSERT_TRUE(WaitUntil([&] { return jobs->GetJob(ids[0])->status == JobStatus::Running; }, Time::Milliseconds(5000)));
		session.CancelBatch();
		session.CancelBatch();
		for (const auto &id : {a, b, c}) EXPECT_EQ(session.CaseRun(id).status, PhononCaseStatus::Cancelled);
		session.SubmitComputation();
		session.StartBatch({c}, {PhononBatchStep::Phonons});
		EXPECT_EQ(ids.size(), 1u);
		EXPECT_TRUE(session.IsBusy());
		ASSERT_TRUE(Release(0));
		Tick();
		Tick();
		EXPECT_FALSE(session.IsBusy());
		EXPECT_EQ(ids.size(), 1u);
		EXPECT_EQ(session.BatchProgress(), (std::pair<std::size_t, std::size_t>{3, 3}));
		EXPECT_TRUE(session.Datasets().empty());
	}

	TEST_F(PhononBatchTests, ProjectReplacementCancelsAndClearsRuntimeBatchState)
	{
		const auto a = Add("A"), b = Add("B");
		session.StartBatch({a, b}, {PhononBatchStep::Phonons});
		session.SetPersistedState({}, {});
		ASSERT_TRUE(Release(0));
		Tick();
		EXPECT_FALSE(session.IsBusy());
		EXPECT_EQ(session.CaseRun(a).status, PhononCaseStatus::Idle);
		EXPECT_EQ(session.BatchProgress(), (std::pair<std::size_t, std::size_t>{0, 0}));
		EXPECT_TRUE(session.Datasets().empty());
		EXPECT_EQ(ids.size(), 1u);
	}

	TEST_F(PhononBatchTests, SingleAndBatchSubmissionsExcludeEachOther)
	{
		const auto a = Add("A");
		session.SubmitComputation();
		session.StartBatch({a}, {PhononBatchStep::Phonons});
		EXPECT_FALSE(session.IsBatchRunning());
		EXPECT_EQ(ids.size(), 1u);
		ASSERT_TRUE(Release(0));
		Tick();
		session.StartBatch({a}, {PhononBatchStep::Phonons});
		session.SubmitComputation();
		session.SubmitCcdAnalysis();
		session.SubmitCcdInputs();
		session.SubmitDispersion();
		EXPECT_EQ(ids.size(), 2u);
		EXPECT_EQ(jobs->GetAllJobs().size(), 2u);
	}

	TEST_F(PhononBatchTests, CcdRunsAfterPhononsWithFrozenNewCurvaturePath)
	{
		const auto a = Add("A");
		session.Settings().ccdPhononCurvature = true;
		session.Settings().groundAtExcitedDirectory = {};
		session.StartBatch({a}, {PhononBatchStep::CcdAnalysis, PhononBatchStep::Phonons});
		ASSERT_EQ(requests.size(), 1u);
		EXPECT_TRUE(std::holds_alternative<PhononAnalysisRequest>(requests[0]));
		session.Settings().ccdAnalysisOutputDirectory = root / "edited-ccd";
		session.Settings().sigmaMeV = 25;
		session.Settings().ccdTemperatureK = 300;
		const auto b = Add("B");
		ASSERT_TRUE(Release(0));
		Tick();
		ASSERT_EQ(requests.size(), 2u);
		const auto &ccd = std::get<CcdAnalysisRequest>(requests[1]);
		EXPECT_EQ(ccd.phononResultDir, root / "A/phonons");
		EXPECT_EQ(ccd.outputDirectory, root / "A/ccd");
		EXPECT_EQ(ccd.sigmaMeV, 6);
		EXPECT_EQ(ccd.temperatureK, 0);
		EXPECT_EQ(session.BatchProgress(), (std::pair<std::size_t, std::size_t>{1, 2}));
		ASSERT_TRUE(Release(1));
		Tick();
		EXPECT_EQ(Case(a).settings.selectedCcdDirectory, root / "A/ccd");
		EXPECT_EQ(Case(a).datasets.size(), 2u);
		EXPECT_EQ(session.CaseRun(a).status, PhononCaseStatus::Done);
		EXPECT_EQ(session.ActiveCaseId(), b);
		EXPECT_FALSE(session.CcdResult());
	}

	TEST_F(PhononBatchTests, SingleCcdCompletesIntoItsOriginatingCase)
	{
		const auto a = Add("A");
		session.SubmitCcdAnalysis();
		const auto b = Add("B");
		const auto revision = session.PlotRevision();
		ASSERT_TRUE(Release(0));
		Tick();
		EXPECT_EQ(session.ActiveCaseId(), b);
		EXPECT_FALSE(session.CcdResult());
		EXPECT_TRUE(session.Settings().selectedCcdDirectory.Empty());
		EXPECT_EQ(session.PlotRevision(), revision + 2);
		EXPECT_EQ(Case(a).settings.selectedCcdDirectory, root / "A/ccd");
		EXPECT_EQ(session.DatasetCaseId(root / "A/ccd"), a);
	}

	TEST_F(PhononBatchTests, FailureSkipsDependentStepAndContinuesNextCase)
	{
		const auto a = Add("A");
		session.Settings().ccdPhononCurvature = true;
		const auto b = Add("B");
		failNext = true;
		session.StartBatch({a, b}, {PhononBatchStep::Phonons, PhononBatchStep::CcdAnalysis});
		ASSERT_TRUE(Release(0));
		Tick();
		ASSERT_EQ(ids.size(), 2u);
		EXPECT_EQ(session.CaseRun(a).status, PhononCaseStatus::Failed);
		EXPECT_NE(session.CaseRun(a).message.find("stub analysis failed"), std::string::npos);
		EXPECT_EQ(session.BatchProgress().first, 2u);
		ASSERT_TRUE(Release(1));
		Tick();
		ASSERT_TRUE(Release(2));
		Tick();
		EXPECT_EQ(session.CaseRun(b).status, PhononCaseStatus::Done);
		EXPECT_EQ(session.BatchProgress(), (std::pair<std::size_t, std::size_t>{4, 4}));
	}

	TEST_F(PhononBatchTests, SharedBlockersRejectMissingDirectoryDestructiveOutputAndCcdWithoutPoints)
	{
		const auto a = Add("A");
		session.Settings().forceConstantDirectory = root / "missing";
		session.StartBatch({a}, {PhononBatchStep::Phonons});
		EXPECT_EQ(session.CaseRun(a).message, session.ComputationBlocker());
		EXPECT_TRUE(ids.empty());
		session.Settings().forceConstantDirectory = root / "A/fc";
		session.Settings().outputDirectory = root;
		session.StartBatch({a}, {PhononBatchStep::Phonons});
		EXPECT_EQ(session.CaseRun(a).status, PhononCaseStatus::Failed);
		EXPECT_TRUE(ids.empty());
		session.Settings().groundStateDirectory = session.Settings().excitedStateDirectory = {};
		session.StartBatch({a}, {PhononBatchStep::CcdAnalysis});
		EXPECT_EQ(session.CaseRun(a).status, PhononCaseStatus::Failed);
		EXPECT_FALSE(session.CaseRun(a).message.empty());
		EXPECT_TRUE(ids.empty());
	}

	TEST_F(PhononBatchTests, InactiveBatchCompletionChangesOnlyDatasetListRevision)
	{
		const auto a = Add("A"), b = Add("B");
		PhononAnalysisResult result;
		result.label = "B view";
		session.SetResult(std::move(result));
		session.Settings().resultDirectory = root / "B/loaded";
		session.StartBatch({a}, {PhononBatchStep::Phonons});
		const auto revision = session.PlotRevision();
		ASSERT_TRUE(Release(0));
		Tick();
		ASSERT_NE(session.Result(), nullptr);
		EXPECT_EQ(session.Result()->label, "B view");
		EXPECT_EQ(session.ActiveCaseId(), b);
		EXPECT_EQ(session.Settings().resultDirectory, root / "B/loaded");
		EXPECT_EQ(session.PlotRevision(), revision + 2);
		EXPECT_EQ(Case(a).settings.resultDirectory, root / "A/phonons");
	}

	TEST_F(PhononBatchTests, DefaultOutputsAreReservedForEachQueuedCase)
	{
		const auto a = Add("A");
		session.Settings().outputDirectory = {};
		const auto b = session.DuplicateCase(a);
		session.StartBatch({a, b}, {PhononBatchStep::Phonons});
		ASSERT_EQ(requests.size(), 1u);
		const auto first = std::get<PhononAnalysisRequest>(requests[0]).outputDirectory;
		ASSERT_TRUE(Release(0));
		Tick();
		ASSERT_EQ(requests.size(), 2u);
		const auto second = std::get<PhononAnalysisRequest>(requests[1]).outputDirectory;
		EXPECT_NE(first, second);
		EXPECT_EQ(session.DatasetCaseId(first), a);
		EXPECT_EQ(session.DatasetCaseId(second), b);
		ASSERT_TRUE(Release(1));
		Tick();
		EXPECT_EQ(Case(a).settings.resultDirectory, first);
		EXPECT_EQ(Case(b).settings.resultDirectory, second);
	}

	TEST_F(PhononBatchTests, IndependentCcdStillRunsAfterPhononFailureAndCaseRemainsFailed)
	{
		const auto a = Add("A");
		failNext = true;
		session.StartBatch({a}, {PhononBatchStep::Phonons, PhononBatchStep::CcdAnalysis});
		ASSERT_TRUE(Release(0));
		Tick();
		ASSERT_EQ(ids.size(), 2u);
		ASSERT_TRUE(Release(1));
		Tick();
		EXPECT_EQ(session.CaseRun(a).status, PhononCaseStatus::Failed);
		EXPECT_NE(session.CaseRun(a).message.find("stub analysis failed"), std::string::npos);
		EXPECT_EQ(Case(a).settings.selectedCcdDirectory, root / "A/ccd");
		EXPECT_EQ(session.BatchProgress(), (std::pair<std::size_t, std::size_t>{2, 2}));
	}
}
