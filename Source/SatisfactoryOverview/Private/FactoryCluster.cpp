#include "FactoryCluster.h"
#include "ItemAmount.h"

// Generic energy-accepting buildable
#include "Buildables/FGBuildableFactory.h"

// Generic crafter (aka factories or machines)
#include "Buildables/FGBuildableManufacturer.h"

// Generic resource extractor
#include "Buildables/FGBuildableResourceExtractor.h"

// Generic energy producer (aka generators) 
#include "Buildables/FGBuildableGenerator.h"

// Station classes
#include "Buildables/FGBuildableDockingStation.h"		// Road
#include "Buildables/FGBuildableTrainPlatformCargo.h"	// Rail
#include "Buildables/FGBuildableDroneStation.h"			// Air

// Spelevator
#include "Buildables/FGBuildableSpaceElevator.h"		// Isn't it technically a station for space?

// Sink
#include "Buildables/FGBuildableResourceSink.h"

// Containers
#include "Buildables/FGBuildableStorage.h"				// Solid
#include "Buildables/FGBuildablePipeReservoir.h"		// Fluid

// Dimensional Depot
#include "Buildables/FGCentralStorageContainer.h"

namespace
{
	/**
	 * Groups manufacturers whose configuration is identical.
	 */
	struct FRecipeGroupKey
	{
		TSubclassOf<UFGRecipe> Recipe;
		float CycleTime = 0.f;
		float ProductionBoost = 0.f;

		bool operator==(const FRecipeGroupKey& Other) const
		{
			return Recipe == Other.Recipe
				&& FMath::IsNearlyEqual(CycleTime, Other.CycleTime)
				&& FMath::IsNearlyEqual(ProductionBoost, Other.ProductionBoost);
		}
	};

	// Hashing on Recipe alone is fine: CycleTime/ProductionBoost only need to be
	// correct in operator==, they don't need to be reflected in the bucket hash.
	uint32 GetTypeHash(const FRecipeGroupKey& Key)
	{
		return GetTypeHash(Key.Recipe);
	}
}

TArray<AFGBuildableFactory*> UFactoryCluster::GetValidMembers() const
{
	// Stored here are weak references to the buildables. We're returning the buildables themselves.
	TArray<AFGBuildableFactory*> Result;
	Result.Reserve(Members.Num());
	for (const TWeakObjectPtr<AFGBuildableFactory>& Weak : Members)
	{
		if (AFGBuildableFactory* M = Weak.Get())
		{
			Result.Add(M);
		}
	}
	return Result;
}

EFactoryBoundaryType UFactoryCluster::ClassifyTerminal(AFGBuildable* Buildable)
{
	if (!Buildable) return EFactoryBoundaryType::None;

	// Plain Boundary: vehicle stations, AWESOME Sink, Space Elevator.
	if (Cast<AFGBuildableDockingStation>(Buildable)
		|| Cast<AFGBuildableDroneStation>(Buildable)
		|| Cast<AFGBuildableTrainPlatformCargo>(Buildable)
		|| Cast<AFGBuildableSpaceElevator>(Buildable)
		|| Cast<AFGBuildableResourceSink>(Buildable))
	{
		return EFactoryBoundaryType::Boundary;
	}

	// Generators: power generators, biofuel generators, etc.
	if (Cast<AFGBuildableGenerator>(Buildable))
	{
		return EFactoryBoundaryType::Generator;
	}

	// Extractors: resource extractors, water extractors, etc.
	if (Cast<AFGBuildableResourceExtractor>(Buildable))
	{
		return EFactoryBoundaryType::Extractor;
	}

	// Dimensional Depot
	if (Cast<AFGCentralStorageContainer>(Buildable))
	{
		return EFactoryBoundaryType::DimensionalDepot;
	}

	return EFactoryBoundaryType::None;
}

bool UFactoryCluster::IsBoundaryBuildable(AFGBuildable* Buildable)
{

	return ClassifyTerminal(Buildable) != EFactoryBoundaryType::None;
}

bool UFactoryCluster::IsNature() const
{
	// A factory is considered "nature" if it has it least one extractor and boundary, but no manufacturers.

	bool bHasExtractor = false;
	bool bHasBoundary = false;
	bool bHasManufacturer = false;

	for (const TWeakObjectPtr<AFGBuildableFactory>& Weak : Members)
	{
		AFGBuildableFactory* M = Weak.Get();
		if (!M) continue;

		const EFactoryBoundaryType Kind = ClassifyTerminal(M);
		if (Kind == EFactoryBoundaryType::Extractor) bHasExtractor = true;
		if (Kind == EFactoryBoundaryType::Boundary) bHasBoundary = true;
		if (Cast<AFGBuildableManufacturer>(M)) bHasManufacturer = true;
	}

	return bHasExtractor && bHasBoundary && !bHasManufacturer;
}

TMap<TSubclassOf<class UFGItemDescriptor>, FItemBalance> UFactoryCluster::ComputeItemBalanceSheet(bool bInFlagOverflowAsInefficient) const
{
	// --- Pass 1: group manufacturers by identical production rate. ---
	TMap<FRecipeGroupKey, int32> GroupCounts;

	for (const TWeakObjectPtr<AFGBuildableFactory>& Weak : Members)
	{
		AFGBuildableManufacturer* Manufacturer = Cast<AFGBuildableManufacturer>(Weak.Get());
		if (!Manufacturer) continue;

		TSubclassOf<UFGRecipe> Recipe = Manufacturer->GetCurrentRecipe();
		if (!Recipe) continue;

		const FRecipeGroupKey Key{
			Recipe,
			Manufacturer->GetProductionCycleTime(),
			Manufacturer->GetCurrentProductionBoost()
		};
		GroupCounts.FindOrAdd(Key)++;
	}

	// --- Pass 2: turn each group into produced/consumed rate contributions. ---
	TMap<TSubclassOf<UFGItemDescriptor>, FItemBalance> Sheet;

	for (const TPair<FRecipeGroupKey, int32>& GroupPair : GroupCounts)
	{
		const FRecipeGroupKey& Key = GroupPair.Key;
		const int32 Count = GroupPair.Value;

		if (Key.CycleTime <= 0.f)
		{
			continue;
		}

		const float CyclesPerMinute = 60.f / Key.CycleTime * static_cast<float>(Count);
		const float ProducedRateMultiplier = CyclesPerMinute * Key.ProductionBoost;
		const float ConsumedRateMultiplier = CyclesPerMinute;


		for (const FItemAmount& Product : UFGRecipe::GetProducts(Key.Recipe))
		{
			Sheet.FindOrAdd(Product.ItemClass).ProducedRate += Product.Amount * ProducedRateMultiplier;
		}

		for (const FItemAmount& Ingredient : UFGRecipe::GetIngredients(this, Key.Recipe))
		{
			Sheet.FindOrAdd(Ingredient.ItemClass).ConsumedRate += Ingredient.Amount * ConsumedRateMultiplier;
		}
	}

	// --- Pass 3: classify each item's net balance against BoundaryRefs. ---
	for (TPair<TSubclassOf<UFGItemDescriptor>, FItemBalance>& Pair : Sheet)
	{
		FItemBalance& Balance = Pair.Value;
		Balance.NetRate = Balance.ProducedRate - Balance.ConsumedRate;

		const EFactoryConnectionKind ItemKind = MapItemType(UFGItemDescriptor::GetForm(Pair.Key));
		const bool bHasMatchingBoundary = BoundaryRefs.ContainsByPredicate([&](const FResolvedEndpoint& Boundary)
			{
				return Boundary.Kind == ItemKind;
			});

		if (Balance.NetRate < -KINDA_SMALL_NUMBER)
		{
			// Deficit is never gated by config -- underflow always surfaces.
			Balance.Status = bHasMatchingBoundary ? EItemBalanceStatus::Imported : EItemBalanceStatus::Deficit;
		}
		else if (Balance.NetRate > KINDA_SMALL_NUMBER)
		{
			Balance.Status = bHasMatchingBoundary
				? EItemBalanceStatus::Exported
				: (bInFlagOverflowAsInefficient ? EItemBalanceStatus::Surplus : EItemBalanceStatus::Balanced);
		}
		else
		{
			Balance.Status = EItemBalanceStatus::Balanced;
		}
	}

	return Sheet;
}

TMap<TSubclassOf<class UFGItemDescriptor>, float> UFactoryCluster::GetProducedItems() const
{
	TMap<TSubclassOf<UFGItemDescriptor>, float> Result;
	const TMap<TSubclassOf<UFGItemDescriptor>, FItemBalance> Sheet = ComputeItemBalanceSheet(bFlagOverflowAsInefficient);

	for (const TPair<TSubclassOf<UFGItemDescriptor>, FItemBalance>& Pair : Sheet)
	{
		if (Pair.Value.Status == EItemBalanceStatus::Exported)
		{
			Result.Add(Pair.Key, Pair.Value.NetRate);
		}
	}

	return Result;
}

TMap<TSubclassOf<class UFGItemDescriptor>, float> UFactoryCluster::GetConsumedItems() const
{
	TMap<TSubclassOf<UFGItemDescriptor>, float> Result;
	const TMap<TSubclassOf<UFGItemDescriptor>, FItemBalance> Sheet = ComputeItemBalanceSheet(bFlagOverflowAsInefficient);

	for (const TPair<TSubclassOf<UFGItemDescriptor>, FItemBalance>& Pair : Sheet)
	{
		if (Pair.Value.Status == EItemBalanceStatus::Imported)
		{
			Result.Add(Pair.Key, Pair.Value.NetRate);
		}
	}

	return Result;
}

void UFactoryCluster::RebuildEndpointIndex()
{
	EndpointIndex.Empty();
	for (int32 i = 0; i < ConnectorGraph.Num(); ++i)
	{
		for (const FResolvedEndpoint& Endpoint : ConnectorGraph[i].Endpoints)
		{
			if (AFGBuildableFactory* Buildable = Endpoint.Buildable.Get())
			{
				EndpointIndex.FindOrAdd(Buildable).Add(i);
			}
		}
	}
}

TArray<FConnectorResolution> UFactoryCluster::GetConnectorsTo(AFGBuildableFactory* Target, EConnectionDirection Direction) const
{
	TArray<FConnectorResolution> Result;
	if (!Target)
	{
		return Result;
	}

	if (const TArray<int32>* Indices = EndpointIndex.Find(Target))
	{
		Result.Reserve(Indices->Num());
		for (int32 Index : *Indices)
		{
			if (ConnectorGraph.IsValidIndex(Index) && ConnectorGraph[Index].SourceDirection == Direction)
			{
				Result.Add(ConnectorGraph[Index]);
			}
		}
	}

	return Result;
}

EFactoryConnectionKind UFactoryCluster::MapItemType(EResourceForm ResourceForm) const {
	switch (ResourceForm)
	{
	case EResourceForm::RF_LIQUID:
		return EFactoryConnectionKind::Fluid; 
	case EResourceForm::RF_GAS:
		return EFactoryConnectionKind::Fluid;
	default:
		return EFactoryConnectionKind::Item;
	}
}