#pragma once

#include "CoreMinimal.h"
#include "FactoryTypes.h"
#include "ItemAmount.h"

#include "FactoryCluster.generated.h"

class AFGBuildableFactory;
class UFGRecipe;

/**
 * Per-item production/consumption balance
 */
UENUM(BlueprintType)
enum class EItemBalanceStatus : uint8
{
	/** Net production and consumption cancel out. */
	Balanced        UMETA(DisplayName = "Balanced"),
	/** Net surplus, AND a matching external boundary exists to carry it out. This is "produced". */
	Exported        UMETA(DisplayName = "Exported"),
	/** Net deficit, AND a matching external boundary exists to supply it. This is "consumed". */
	Imported        UMETA(DisplayName = "Imported"),
	/** Net deficit with NO external source to cover it. The player will be warned. */
	Deficit         UMETA(DisplayName = "Deficit"),
	/** Net surplus with NO external boundary to carry it out. The player will be warned if configured. */
	Surplus			UMETA(DisplayName = "Surplus"),
};

/** Represents item balance */
USTRUCT(BlueprintType)
struct FItemBalance
{
	GENERATED_BODY()

	/** Summed rate (items or m3 per minute) across every manufacturer group whose recipe outputs this item. */
	UPROPERTY(BlueprintReadOnly, Category = "FactoryAnalysis")
	float ProducedRate = 0.f;

	/** Summed rate across every manufacturer group whose recipe needs this item as an ingredient. */
	UPROPERTY(BlueprintReadOnly, Category = "FactoryAnalysis")
	float ConsumedRate = 0.f;

	/** ProducedRate - ConsumedRate. Positive = net surplus, negative = net deficit. */
	UPROPERTY(BlueprintReadOnly, Category = "FactoryAnalysis")
	float NetRate = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "FactoryAnalysis")
	EItemBalanceStatus Status = EItemBalanceStatus::Balanced;
};


/**
 * One connected cluster of buildables.
 * Members and ConnectorGraph are both populated once, during the scan's
 * analysis pass (UFactoryAnalysisSubsystem::BuildConnectorGraph.
 */
UCLASS(BlueprintType)
class UFactoryCluster : public UObject
{
	GENERATED_BODY()

public:
	/** Config-set */
	UPROPERTY()
	bool bFlagOverflowAsInefficient = false;

	UPROPERTY()
	TArray<TWeakObjectPtr<AFGBuildableFactory>> Members;

	/** Boundaries for this factory */
	UPROPERTY()
	TArray<FResolvedEndpoint> BoundaryRefs;

	/** Precomputed by BuildConnectorGraph. */
	UPROPERTY()
	TArray<FConnectorResolution> ConnectorGraph;

	/** Builds EndpointIndex, to keep track of connections */
	void RebuildEndpointIndex();

	/** Returns only Members that are still valid (not demolished since the scan). */
	UFUNCTION(BlueprintCallable, Category = "FactoryAnalysis")
	TArray<AFGBuildableFactory*> GetValidMembers() const;

	UFUNCTION(BlueprintPure, Category = "FactoryAnalysis")
	int32 GetMemberCount() const { return Members.Num(); }

	/** Nature: only extractors + boundary, no processing machines at all. */
	UFUNCTION(BlueprintPure, Category = "FactoryAnalysis")
	bool IsNature() const;

	TArray<FConnectorResolution> GetConnectorsTo(AFGBuildableFactory* Target, EConnectionDirection Direction) const;

	/** Calculates factory's inputs and outputs by the amount produced and consumed within its machines */
	UFUNCTION(BlueprintCallable, Category = "FactoryAnalysis")
	TMap<TSubclassOf<class UFGItemDescriptor>, FItemBalance> ComputeItemBalanceSheet(bool bInFlagOverflowAsInefficient) const;

	/**
	 * Returns items whose BalanceStatus is Exported.
	 */
	UFUNCTION(BlueprintCallable, Category = "FactoryAnalysis")
	TMap<TSubclassOf< class UFGItemDescriptor >, float> GetProducedItems() const;

	/**
	 * Returns items whose BalanceStatus is Imported.
	 */
	UFUNCTION(BlueprintCallable, Category = "FactoryAnalysis")
	TMap<TSubclassOf< class UFGItemDescriptor >, float> GetConsumedItems() const;


	static EFactoryBoundaryType ClassifyTerminal(AFGBuildable* Buildable);

	/** True if Buildable is a boundary, defined at EFactoryTerminalKind. */
	static bool IsBoundaryBuildable(AFGBuildable* Buildable);

private:
	/** Stores this factory's connections between machines. Populated by RebuildEndpointIndex(). */
	TMap<AFGBuildableFactory*, TArray<int32>> EndpointIndex;

	/** Maps item type to connection type. Helper function. */
	EFactoryConnectionKind MapItemType(EResourceForm ResourceForm) const;

};