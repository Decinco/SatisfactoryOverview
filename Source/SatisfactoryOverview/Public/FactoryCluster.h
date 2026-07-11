#pragma once

#include "CoreMinimal.h"
#include "FactoryTypes.h"
#include "ItemAmount.h"

#include "FactoryCluster.generated.h"

class AFGBuildableFactory;
class UFGRecipe;

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

	/**
	 * Exported items of manufacturers whose OUTPUT is "outermost" -> directly connected to a boundary's input.
	 */
	UFUNCTION(BlueprintCallable, Category = "FactoryAnalysis")
	TMap<TSubclassOf< class UFGItemDescriptor >, float> GetProducedItems() const;

	/**
	 * Imported items of manufacturers whose INPUT is "innermost" -> directly connected to a boundary's output.
	 */
	UFUNCTION(BlueprintCallable, Category = "FactoryAnalysis")
	TMap<TSubclassOf< class UFGItemDescriptor >, float> GetConsumedItems(TArray<FString>& out_InputErrors) const;


	static EFactoryBoundaryType ClassifyTerminal(AFGBuildable* Buildable);

	/** True if Buildable is a boundary, defined at EFactoryTerminalKind. */
	static bool IsBoundaryBuildable(AFGBuildable* Buildable);

private:
	/** Stores this factory's connections between machines. Populated by RebuildEndpointIndex(). */
	TMap<AFGBuildableFactory*, TArray<int32>> EndpointIndex;

	/** Maps item type to connection type. Helper function. */
	EFactoryConnectionKind MapItemType(EResourceForm ResourceForm) const;

};