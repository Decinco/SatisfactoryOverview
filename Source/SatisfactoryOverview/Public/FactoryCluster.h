#pragma once

#include "CoreMinimal.h"
#include "FactoryTypes.h"
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

	/** Returns only Members that are still valid (not demolished since the scan). */
	UFUNCTION(BlueprintCallable, Category = "FactoryAnalysis")
	TArray<AFGBuildableFactory*> GetValidMembers() const;

	UFUNCTION(BlueprintPure, Category = "FactoryAnalysis")
	int32 GetMemberCount() const { return Members.Num(); }

	/** Nature: only extractors + boundary, no processing machines at all. */
	UFUNCTION(BlueprintPure, Category = "FactoryAnalysis")
	bool IsNature() const;

	/**
	 * Recipes of manufacturers whose OUTPUT is "outermost": connects to
	 * nothing, or to an export boundary.
	 */
	UFUNCTION(BlueprintCallable, Category = "FactoryAnalysis")
	TArray<TSubclassOf<UFGRecipe>> GetProducedRecipes() const;

	/**
	 * Recipes of manufacturers whose INPUT is "innermost": connects to a
	 * miner or an import boundary. out_InputErrors lists manufacturer names
	 * whose input connects to nothing at all.
	 */
	UFUNCTION(BlueprintCallable, Category = "FactoryAnalysis")
	TArray<TSubclassOf<UFGRecipe>> GetConsumedRecipes(TArray<FString>& out_InputErrors) const;


	static EFactoryBoundaryType ClassifyTerminal(AFGBuildable* Buildable);

	/** True if Buildable is a boundary, defined at EFactoryTerminalKind. */
	static bool IsBoundaryBuildable(AFGBuildable* Buildable);

};