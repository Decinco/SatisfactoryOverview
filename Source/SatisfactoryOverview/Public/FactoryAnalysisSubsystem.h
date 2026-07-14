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
// Scan finished delegate.
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnFactoryScanFinished);

/**
 * Scans the world for buildables and their connectors, classifies them into clusters,
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
	bool IsScanInProgress() const { return !FactoryWorkQueue.IsComplete(); }

	UFUNCTION(BlueprintPure, Category = "FactoryAnalysis")
	float GetScanProgress() const { return FactoryWorkQueue.GetProgress(); }

	UPROPERTY(BlueprintAssignable, Category = "FactoryAnalysis")
	FOnFactoryScanFinished OnFactoryScanFinished;

	// --- FTickableGameObject ---
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UFactoryAnalysisSubsystem, STATGROUP_Tickables); }
	virtual bool IsTickable() const override { return IsScanInProgress(); }

private:
	/** Unions conveyors between themselves. */
	void ProcessConveyor(AFGBuildableConveyorBase* const& Buildable, int32 Index);

	/** Unions factory buildings to already existing networks. */
	void ProcessFactory(AFGBuildableFactory* const& Buildable, int32 Index);

	/** Applies conditional filtering for containers. */
	void ProcessContainer(AFGBuildableStorage* const& Buildable, int32 Index);

	/** Prepares factory processing */
	void BeginFactoryProcessing();

	/** Prepares container processing */
	void BeginContainerProcessing();

	/** Runs once all work queues finish  */
	void OnScanComplete();

	/** Work queues and Union-Find */
	TBudgetedWorkQueue<AFGBuildableConveyorBase*> ConveyorWorkQueue;
	TBudgetedWorkQueue<AFGBuildableFactory*> FactoryWorkQueue;
	TBudgetedWorkQueue<AFGBuildableStorage*> ContainerWorkQueue;
	FBuildableUnionFind FactoryUnionFind;

	// Boundary connections to be added to the cluster after the scan completes.
	TMap<TWeakObjectPtr<AFGBuildableFactory>, TArray<FResolvedEndpoint>> PendingBoundaryEndpoints;

	// Populated during ProcessBuildable, consumed in OnScanComplete's pipe-merge pass.
	TMap<int32, TArray<AFGBuildableFactory*>> PipeNetworkGroups; // ADJUST: confirm real pipe network ID type (assumed int32 here)

	// Mod config
	bool bTreatContainersAsFactoryEnd = false;
};
