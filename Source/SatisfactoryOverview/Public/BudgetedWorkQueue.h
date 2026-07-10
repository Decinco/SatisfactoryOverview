#pragma once

#include "CoreMinimal.h"

/**
 * Generic time-sliced work queue. Processes a batch of items across multiple
 * ticks, stopping each tick once a configurable time budget is spent rather
 * than after a fixed item count -- because per-item cost is not uniform
 * (e.g. a conveyor pole with one connector vs. a train station with several
 * cargo/fuel connectors and a pipe network lookup).
 *
 * Reused by:
 *   - Phase 0's full-world scan (item type = buildable to enumerate/classify)
 *   - Design doc §5.3's Blueprint Designer batch placement handling
 *     (item type = a single placed piece to resolve into the graph)
 *
 * NOTE: this uses FTSTicker as a placeholder tick source. Verify against the
 * actual SML/Satisfactory subsystem base class once confirmed (design doc
 * §16 open unknowns) -- if your subsystem already has its own Tick(), it's
 * simpler to call TBudgetedWorkQueue::ProcessBudget() directly from there
 * instead of registering a second ticker.
 */
template<typename TItem>
class TBudgetedWorkQueue
{
public:
	/** Called once per item, on whatever tick it happens to be processed. */
	using FItemProcessor = TFunction<void(const TItem& Item, int32 Index)>;

	/** Called once, after the last item has been processed. */
	using FCompletionCallback = TFunction<void()>;

	/**
	 * @param InTimeBudgetSeconds   Max wall-clock time to spend per tick.
	 *                              Should be exposed as mod config (design
	 *                              doc §12), not hardcoded. ~0.0015 (1.5ms)
	 *                              is a reasonable starting point -- see
	 *                              rationale in the accompanying discussion.
	 */
	explicit TBudgetedWorkQueue(double InTimeBudgetSeconds = 0.0015)
		: TimeBudgetSeconds(InTimeBudgetSeconds)
	{
	}

	/** Begin processing a batch. Safe to call again once IsComplete() is true. */
	void Start(TArray<TItem> InItems, FItemProcessor InProcessor, FCompletionCallback InOnComplete = nullptr)
	{
		Items = MoveTemp(InItems);
		Processor = MoveTemp(InProcessor);
		OnComplete = MoveTemp(InOnComplete);
		CurrentIndex = 0;
		bComplete = Items.Num() == 0;

		if (bComplete && OnComplete)
		{
			OnComplete();
		}
	}

	/**
	 * Call once per tick (from your subsystem's own Tick, or from a
	 * registered ticker -- see class comment). Processes items starting
	 * from the saved cursor until the time budget for this tick is spent,
	 * then yields.
	 */
	void ProcessBudget()
	{
		if (bComplete || Items.Num() == 0)
		{
			return;
		}

		const double StartTime = FPlatformTime::Seconds();

		while (CurrentIndex < Items.Num())
		{
			Processor(Items[CurrentIndex], CurrentIndex);
			++CurrentIndex;

			if (FPlatformTime::Seconds() - StartTime >= TimeBudgetSeconds)
			{
				// Budget spent for this tick -- yield, resume next tick from CurrentIndex.
				return;
			}
		}

		// Reached the end of the batch.
		bComplete = true;
		if (OnComplete)
		{
			OnComplete();
		}
	}

	bool IsComplete() const { return bComplete; }

	/** For driving a loading-screen progress bar. */
	float GetProgress() const
	{
		return Items.Num() > 0 ? static_cast<float>(CurrentIndex) / static_cast<float>(Items.Num()) : 1.0f;
	}

	int32 GetCurrentIndex() const { return CurrentIndex; }
	int32 GetTotalCount() const { return Items.Num(); }

	/** Adjust the per-tick budget at runtime (e.g. from mod config, §12). */
	void SetTimeBudgetSeconds(double InTimeBudgetSeconds) { TimeBudgetSeconds = InTimeBudgetSeconds; }

private:
	TArray<TItem> Items;
	FItemProcessor Processor;
	FCompletionCallback OnComplete;

	int32 CurrentIndex = 0;
	bool bComplete = true;
	double TimeBudgetSeconds;
};
