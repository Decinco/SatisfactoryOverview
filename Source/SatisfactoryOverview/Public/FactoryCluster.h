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
struct FItemRate
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

	UPROPERTY(BlueprintReadOnly, Category = "FactoryAnalysis")
	bool bIsBounded = false;
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

	/** Adds a building to one of the lists */
	void AddToFactory(AFGBuildableFactory* Buildable);

	/** Returns all buildings */
	UFUNCTION()
	TArray<TWeakObjectPtr<AFGBuildableFactory>> GetMembers() const;

	/** Returns only buildings that are still valid (not demolished since the scan). */
	UFUNCTION(BlueprintCallable, Category = "FactoryAnalysis")
	TArray<AFGBuildableFactory*> GetValidMembers() const;

	/** Nature: only extractors + boundary, no processing machines at all. Will likely be extended to define other factory types.*/
	UFUNCTION(BlueprintPure, Category = "FactoryAnalysis")
	bool IsNature() const;

	/** Fuel Station: only truck or drone stations */
	bool IsFuelStation() const;

	TArray<FConnectorResolution> GetConnectionsFrom(AFGBuildableFactory* Target, EConnectionDirection Direction) const;

	/** Calculates factory's inputs and outputs by the amount produced and consumed within its machines */
	UFUNCTION(BlueprintCallable, Category = "FactoryAnalysis")
	FItemBalance ComputeItemBalanceSheet(bool bInFlagOverflowAsInefficient) const;

	/** Returns the produced amount by a certain buildable. Returns consumption as a negative number. */
	TMap<TSubclassOf<UFGItemDescriptor>, FItemRate> FindProductionConsumption(AFGBuildableFactory* Factory) const;

	/** Pipe network groups, copied from the subsystem at scan completion. */
	TMap<int32, TArray<AFGBuildableFactory*>> PipeNetworkGroups;

	/** Find all valid clusters in a cluster list */
	static TArray<UFactoryCluster*> GetAllValidClusters(const TArray<UFactoryCluster*>& ClusterList);

	bool bHasSpaceElevator = false;

private:
	/** Maps item type to connection type. Helper function. */
	EFactoryConnectionKind MapItemType(EResourceForm ResourceForm) const;

	/** Gets the amount of lists that have at least 1 member */
	int GetNumListsNotEmpty() const;

	/** Only storage with two connections should ever fall here. Used as fallback and will be checked */
	UPROPERTY()
	TArray<TWeakObjectPtr<AFGBuildableFactory>> UnclassifiedMembers;

	/** The five types of building */
	UPROPERTY()
	TArray<TWeakObjectPtr<AFGBuildableFactory>> Manufacturers;
	UPROPERTY()
	TArray<TWeakObjectPtr<AFGBuildableFactory>> Producers;
	UPROPERTY()
	TArray<TWeakObjectPtr<AFGBuildableFactory>> Consumers;
	UPROPERTY()
	TArray<TWeakObjectPtr<AFGBuildableFactory>> ProducerConsumers;
	UPROPERTY()
	TArray<TWeakObjectPtr<AFGBuildableFactory>> Bounds;
};