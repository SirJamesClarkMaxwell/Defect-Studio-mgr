# Progress Tracker System

## What It Is

`ProgressTracker` is a thread-safe progress registry for long-running operations.

It is designed as a read model for UI/debug views.

## Core API

- `Register(label, total) -> id`
- `Report(id, completed)`
- `Finish(id)`
- `Snapshot() -> copy of entries`

## Responsibility Split

Worker threads should:

- report incremental progress,
- avoid direct UI/state commits.

Main thread should:

- read `Snapshot()` during update/render,
- decide when to transition application-level state.

## Application Perspective Example

```cpp
class ImportController
{
public:
	void StartImport(DefectStudio::JobSystem& jobs, DefectStudio::ProgressTracker& tracker)
	{
		m_ProgressId = tracker.Register("Import Structures", 100);
		m_Future = jobs.SubmitDetached(std::bind(&ImportController::RunImport, this, std::ref(tracker)));
	}

	void OnMainThreadUpdate(DefectStudio::ProgressTracker& tracker)
	{
		const auto entries = tracker.Snapshot();
		UpdateUi(entries);
		if (IsFinished(entries, m_ProgressId))
		{
			CommitImportedData();
		}
	}

private:
	void RunImport(DefectStudio::ProgressTracker& tracker)
	{
		for (std::size_t i = 1; i <= 100; ++i)
		{
			DoImportStep(i);
			tracker.Report(m_ProgressId, i);
		}
		tracker.Finish(m_ProgressId);
	}

	void DoImportStep(std::size_t step);
	void UpdateUi(const std::vector<DefectStudio::ProgressEntry>& entries);
	bool IsFinished(const std::vector<DefectStudio::ProgressEntry>& entries, std::size_t id) const;
	void CommitImportedData();

private:
	std::size_t m_ProgressId = 0;
	std::future<void> m_Future;
};
```

## Why This Pattern Works

- The background job reports facts (progress numbers).
- The main thread owns final orchestration decisions.
- UI always reads a safe snapshot.

This keeps the concurrency boundary explicit and debuggable.
