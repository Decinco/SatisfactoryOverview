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
	bool IsScanInProgress() const { return !WorkQueue.IsComplete(); }

	UFUNCTION(BlueprintPure, Category = "FactoryAnalysis")
	float GetScanProgress() const { return WorkQueue.GetProgress(); }

	UPROPERTY(BlueprintAssignable, Category = "FactoryAnalysis")
	FOnFactoryScanFinished OnFactoryScanFinished;

	// --- FTickableGameObject ---
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UFactoryAnalysisSubsystem, STATGROUP_Tickables); }
	virtual bool IsTickable() const override { return IsScanInProgress(); }

private:
	/** Per-item processing: register in union-find, follow connectors, record pipe network membership. */
	void ProcessBuildable(AFGBuildableFactory* const& Buildable, int32 Index);

	/** Runs once the work queue finishes: pipe-network merge pass + classification + logging. */
	void OnScanComplete();

	/** Builds the ConnectorGraph for a cluster, using the union-find data structure to find connected components. */
	void BuildConnectorGraph(UFactoryCluster* Cluster);

	TBudgetedWorkQueue<AFGBuildableFactory*> WorkQueue;
	FBuildableUnionFind UnionFind;

	// Boundary connections to be added to the cluster after the scan completes.
	TMap<TWeakObjectPtr<AFGBuildableFactory>, TArray<FResolvedEndpoint>> PendingBoundaryEndpoints;

	// Populated during ProcessBuildable, consumed in OnScanComplete's pipe-merge pass.
	TMap<int32, TArray<AFGBuildableFactory*>> PipeNetworkGroups; // ADJUST: confirm real pipe network ID type (assumed int32 here)

	// Mod config
	bool bTreatContainersAsFactoryEnd = false;
};
