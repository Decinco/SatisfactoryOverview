#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "BudgetedWorkQueue.h"
#include "BuildableUnionFind.h"
#include "Buildables/FGBuildableFactory.h"
#include "FactoryTypes.h"
#include "FactoryCluster.h"
#include "Buildables/FGBuildableStorage.h"

#include "FactoryAnalysisSubsystem.generated.h"

class AFGBuildable;
class AFGCharacterPlayer;
// Scan finished delegate.
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnFactoryScanFinished);

/**
 * Scans the world for buildables and their connectors, classifies them into clusters,
 * and (as of this revision) keeps the resulting graph alive on the subsystem afterwards
 * instead of discarding it -- this is the foundation Phase 1's incremental updates build on.
 */
UCLASS()
class UFactoryAnalysisSubsystem : public UWorldSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:

	UFUNCTION(BlueprintPure, Category = "FactoryAnalysis", meta = (WorldContext = "WorldContextObject"))
	static UFactoryAnalysisSubsystem* GetFactoryAnalysisSubsystem(const UObject* WorldContextObject);

	/** Kicks off a new full-world scan. */
	UFUNCTION(BlueprintCallable, Category = "FactoryAnalysis")
	void StartScan();

	UFUNCTION(BlueprintPure, Category = "FactoryAnalysis")
	bool IsScanInProgress() const {
		return !ConveyorWorkQueue.IsComplete()
			|| !FactoryWorkQueue.IsComplete()
			|| !ContainerWorkQueue.IsComplete();
	}

	UFUNCTION(BlueprintPure, Category = "FactoryAnalysis")
	float GetScanProgress() const { return FactoryWorkQueue.GetProgress(); }

	UPROPERTY(BlueprintAssignable, Category = "FactoryAnalysis")
	FOnFactoryScanFinished OnFactoryScanFinished;

	/**
	 * Gets all clusters. Will return nothing if first scan hasn't passed yet.
	 */
	UFUNCTION(BlueprintPure, Category = "FactoryAnalysis")
	const TArray<UFactoryCluster*>& GetClusters() const { return Clusters; }

	/**
	 * Gives a rundown of all clusters.
	 */
	UFUNCTION(BlueprintCallable, Category = "FactoryAnalysis")
	void DebugClusters();

	/**
	 * Looks up which cluster owns a given buildable.
	 */
	UFactoryCluster* FindClusterFor(AFGBuildable* Buildable) const;

	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	UFUNCTION(BlueprintCallable, Category = "FactoryAnalysis")
	void HandleBuildableConstructed(AFGBuildable* NewBuildable);

	/** Handles player-constructed actors (fires after connections are established). */
	UFUNCTION()
	void HandleActorsConstructedByPlayer(AFGCharacterPlayer* Player, TArray<AActor*> ConstructedActors);

	// --- FTickableGameObject ---
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UFactoryAnalysisSubsystem, STATGROUP_Tickables); }
	virtual bool IsTickable() const override { return IsScanInProgress() || IsIncrementalInProgress(); }

private:
	/** True while the incremental queue has work pending or in-flight. */
	bool IsIncrementalInProgress() const {
		return !IncrementalWorkQueue.IsComplete() || PendingBuildables.Num() > 0;
	}

	/** Called when an incremental batch finishes; drains PendingBuildables into a new batch. */
	void FlushIncrementalQueue();

	/** Unions conveyors between themselves. */
	void ProcessConveyor(AFGBuildableConveyorBase* const& Buildable, int32 Index);

	/** Unions factory buildings to already existing networks. */
	void ProcessFactory(AFGBuildableFactory* const& Buildable, int32 Index);

	/** Unions containers into whatever they're connected to. */
	void ProcessContainer(AFGBuildableStorage* const& Buildable, int32 Index);

	/** Prepares factory processing */
	void BeginFactoryProcessing();

	/** Prepares container processing */
	void BeginContainerProcessing();

	/** Runs once all work queues finish. Rebuilds Clusters/BuildableToCluster from FactoryUnionFind. */
	void RegisterClusters();

	TMap<TWeakObjectPtr<AFGBuildable>, TWeakObjectPtr<UFactoryCluster>> BuildableToCluster;

	TBudgetedWorkQueue<AFGBuildableConveyorBase*> ConveyorWorkQueue;
	TBudgetedWorkQueue<AFGBuildableFactory*> FactoryWorkQueue;
	TBudgetedWorkQueue<AFGBuildableStorage*> ContainerWorkQueue;
	TBudgetedWorkQueue<AFGBuildable*> IncrementalWorkQueue;
	TArray<AFGBuildable*> PendingBuildables;
	mutable FBuildableUnionFind FactoryUnionFind;

	UPROPERTY()
	TArray<UFactoryCluster*> Clusters;

	// Boundary connections to be added to the cluster after the scan completes.
	TMap<TWeakObjectPtr<AFGBuildableFactory>, TArray<FItemBalance>> PendingBoundaryEndpoints;

	// Populated during ProcessFactory/ProcessContainer, consumed in OnScanComplete's pipe-merge pass.
	TMap<int32, TArray<AFGBuildableFactory*>> PipeNetworkGroups;

	UFactoryCluster* FindOrCreateClusterFor(AFGBuildable* Buildable);
};