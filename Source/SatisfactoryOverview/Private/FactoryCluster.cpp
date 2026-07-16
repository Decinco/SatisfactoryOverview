#include "FactoryCluster.h"
#include "ItemAmount.h"
#include "FactoryConnectionResolver.h"
#include "FGPipeConnectionComponent.h"

// Generic energy-accepting buildable
#include "Buildables/FGBuildableFactory.h"

// Manufacturer Types
#include "Buildables/FGBuildableManufacturer.h"

// Producer Types
#include "Buildables/FGBuildableResourceExtractor.h"

// Consumer Types 
#include "Buildables/FGBuildableGenerator.h"
#include "FGBuildablePowerBooster.h"
#include "Buildables/FGBuildablePortal.h"

// ProducerConsumer Types
#include "Buildables/FGBuildableGeneratorNuclear.h"

// Bound Types
#include "Buildables/FGBuildableDockingStation.h"		// Road
#include "Buildables/FGBuildableTrainPlatformCargo.h"	// Rail
#include "Buildables/FGBuildableDroneStation.h"			// Air
#include "Buildables/FGBuildableSpaceElevator.h"		// Space????

#include "Buildables/FGBuildableStorage.h"				// Solid Container
#include "Buildables/FGBuildablePipeReservoir.h"		// Fluid Container
#include "Buildables/FGCentralStorageContainer.h"

#include "Buildables/FGBuildableResourceSink.h"			// AWESOME!

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
				&& CycleTime == Other.CycleTime
				&& ProductionBoost == Other.ProductionBoost;
		}
	};

	// Hashing on Recipe alone is fine: CycleTime/ProductionBoost only need to be
	// correct in operator==, they don't need to be reflected in the bucket hash.
	uint32 GetTypeHash(const FRecipeGroupKey& Key)
	{
		return GetTypeHash(Key.Recipe);
	}
}

TArray<TWeakObjectPtr<AFGBuildableFactory>> UFactoryCluster::GetMembers() const {
	TArray<TWeakObjectPtr<AFGBuildableFactory>> Members;

	Members.Append(UnclassifiedMembers);

	Members.Append(Manufacturers);
	Members.Append(Producers);
	Members.Append(Consumers);
	Members.Append(ProducerConsumers);
	Members.Append(Bounds);

	return Members;
}

TArray<AFGBuildableFactory*> UFactoryCluster::GetValidMembers() const
{
	// Stored here are weak references to the buildables. We're returning the buildables themselves.
	TArray<AFGBuildableFactory*> Result;
	TArray<TWeakObjectPtr<AFGBuildableFactory>> Members = GetMembers();

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

bool UFactoryCluster::IsNature() const
{
	// There can only be bounds and producers and BOTH must be present in some way.
	return Manufacturers.IsEmpty() && Consumers.IsEmpty() && ProducerConsumers.IsEmpty() && !Bounds.IsEmpty() && !Producers.IsEmpty();
}

FItemBalance UFactoryCluster::ComputeItemBalanceSheet(bool bInFlagOverflowAsInefficient) const
{
	FItemBalance Result;

	// Find out items in Manufacturers' recipes
	TMap<TSubclassOf<UFGItemDescriptor>, FItemRate> Sheet;

	for (const TWeakObjectPtr<AFGBuildableFactory>& WeakManufacturer : Manufacturers)
	{
		// Get manufacturer. Should always be a manufacturer (duh).
		AFGBuildableManufacturer* Manufacturer = Cast<AFGBuildableManufacturer>(WeakManufacturer.Get());
		if (!Manufacturer) continue;

		const float CycleTime = Manufacturer->GetProductionCycleTime();
		const float ProductionBoost = Manufacturer->GetCurrentProductionBoost();

		if (CycleTime <= 0.f)
		{
			continue;
		}

		const float CyclesPerMinute = 60.f / CycleTime;
		const float ProducedRateMultiplier = CyclesPerMinute * ProductionBoost;
		const float ConsumedRateMultiplier = CyclesPerMinute;

		auto Recipe = Manufacturer->GetCurrentRecipe();

		for (const FItemAmount& Product : UFGRecipe::GetProducts(Recipe))
		{
			Sheet.FindOrAdd(Product.ItemClass).ProducedRate += Product.Amount * ProducedRateMultiplier;
		}

		for (const FItemAmount& Ingredient : UFGRecipe::GetIngredients(this, Recipe))
		{
			Sheet.FindOrAdd(Ingredient.ItemClass).ConsumedRate += Ingredient.Amount * ConsumedRateMultiplier;
		}
	}

	// Find out additional production and consumption - TODO

	// Producers
	for (const TWeakObjectPtr<AFGBuildableFactory>& WeakProducer : Producers) {
		AFGBuildableResourceExtractor* Producer = Cast<AFGBuildableResourceExtractor>(WeakProducer.Get());

		// Get producer's production
	}

	// Find out output kind
	TSet<TSubclassOf<UFGItemDescriptor>> ExportableViaBoundary;
	TSet<TSubclassOf<UFGItemDescriptor>> ImportableViaBoundary;

	for (const TWeakObjectPtr<AFGBuildableFactory>& WeakBound : Bounds)
	{
		AFGBuildableFactory* Bound = WeakBound.Get();
		if (!Bound) continue;

		// Get all importable
		for (const FConnectorResolution& Connection : GetConnectionsFrom(Bound, EConnectionDirection::Output))
		{
			for (const FResolvedEndpoint& Endpoint : Connection.Endpoints)
			{
				AFGBuildableManufacturer* Manufacturer = Cast<AFGBuildableManufacturer>(Endpoint.Buildable.Get());
				if (!Manufacturer) continue;
				TSubclassOf<UFGRecipe> Recipe = Manufacturer->GetCurrentRecipe();
				if (!Recipe) continue;
				for (const FItemAmount& Ingredient : UFGRecipe::GetIngredients(this, Recipe))
				{
					ImportableViaBoundary.Add(Ingredient.ItemClass);
				}
			}
		}

		// Get all exportable
		for (const FConnectorResolution& Connection : GetConnectionsFrom(Bound, EConnectionDirection::Input))
		{
			for (const FResolvedEndpoint& Endpoint : Connection.Endpoints)
			{
				AFGBuildableManufacturer* Manufacturer = Cast<AFGBuildableManufacturer>(Endpoint.Buildable.Get());
				if (!Manufacturer) continue;
				TSubclassOf<UFGRecipe> Recipe = Manufacturer->GetCurrentRecipe();
				if (!Recipe) continue;
				for (const FItemAmount& Product : UFGRecipe::GetProducts(Recipe))
				{
					ExportableViaBoundary.Add(Product.ItemClass);
				}
			}
		}
	}

	// Classify outputs
	for (const TPair<TSubclassOf<UFGItemDescriptor>, FItemRate>& Pair : Sheet)
	{
		const TSubclassOf<UFGItemDescriptor>& Item = Pair.Key;
		const float NetRate = Pair.Value.ProducedRate - Pair.Value.ConsumedRate;

		if (NetRate > KINDA_SMALL_NUMBER)
		{
			if (ExportableViaBoundary.Contains(Item) || Pair.Value.bIsBounded) // Producers and consumers will count as bounds for this
			{
				Result.Export.Add(Item, NetRate);
			}
			else
			{
				Result.Surplus.Add(Item, NetRate);
			}
		}
		else if (NetRate < -KINDA_SMALL_NUMBER)
		{
			if (ImportableViaBoundary.Contains(Item) || Pair.Value.bIsBounded) // Producers and consumers will count as bounds for this
			{
				Result.Import.Add(Item, -NetRate);
			}
			else
			{
				Result.Deficit.Add(Item, -NetRate);
			}
		}
	}

	return Result;
}

TArray<FConnectorResolution> UFactoryCluster::GetConnectionsFrom(AFGBuildableFactory* Target, EConnectionDirection Direction) const
{
	TArray<FConnectorResolution> ConnectedFactories;

	// Resolve belt connections
	for (UFGFactoryConnectionComponent* Connection : Target->GetConnectionComponents()) {
		if (FConnectionDirectionMapper::Map(Connection->GetDirection()) == Direction) {
			TSet<AFGBuildable*> Visited;

			FConnectorResolution Resolution;
			Resolution.SourceConnector = Connection;
			Resolution.SourceDirection = Direction;
			Resolution.Endpoints = FactoryConnectionResolver::ResolveBeltConnections(Connection, Visited);
			Resolution.Kind = EFactoryConnectionKind::Item;
			Resolution.SourceOwner = Target;
			ConnectedFactories.Add(MoveTemp(Resolution));
		}
	}

	// Resolve pipe connections
	TArray<UFGPipeConnectionComponent*> PipeConnections;
	Target->GetComponents<UFGPipeConnectionComponent>(PipeConnections);
	for (UFGPipeConnectionComponent* PipeConn : PipeConnections) {
		EConnectionDirection PipeDir = FConnectionDirectionMapper::Map(PipeConn->GetPipeConnectionType());
		if (PipeDir == Direction || PipeDir == EConnectionDirection::Any) {
			ConnectedFactories.Add(FactoryConnectionResolver::ResolvePipeConnections(Target, PipeConn, PipeNetworkGroups));
		}
	}

	return ConnectedFactories;
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

void UFactoryCluster::AddToFactory(AFGBuildableFactory* Buildable) {
	if (!Buildable) return;

	if (Cast<AFGBuildableManufacturer>(Buildable)) {
		Manufacturers.Add(Buildable);
	}
	else if (Cast<AFGBuildableResourceExtractor>(Buildable)) {
		Producers.Add(Buildable);
	}
	// Nuclear before generic generator — it's a subclass of generator
	else if (Cast<AFGBuildableGeneratorNuclear>(Buildable)) {
		ProducerConsumers.Add(Buildable);
	}
	else if (Cast<AFGBuildableGenerator>(Buildable)) {
		Consumers.Add(Buildable);
	}
	else if (Cast<AFGBuildablePowerBooster>(Buildable)) {
		Consumers.Add(Buildable);
	}
	else if (Cast<AFGBuildablePortal>(Buildable)) {
		Consumers.Add(Buildable);
	}
	else if (Cast<AFGBuildableDockingStation>(Buildable)
		|| Cast<AFGBuildableTrainPlatformCargo>(Buildable)
		|| Cast<AFGBuildableDroneStation>(Buildable)
		|| Cast<AFGBuildableSpaceElevator>(Buildable)
		|| Cast<AFGCentralStorageContainer>(Buildable)
		|| Cast<AFGBuildablePipeReservoir>(Buildable)
		|| Cast<AFGBuildableResourceSink>(Buildable)) {
		Bounds.Add(Buildable);
	}
	else if (Cast<AFGBuildableStorage>(Buildable)) {
		// Get all connectors
		TArray<UFGFactoryConnectionComponent*> ContainerConnectors;
		ContainerConnectors = Buildable->GetConnectionComponents();

		bool bIsOutputConnected = false;
		bool bIsInputConnected = false;

		// Determine how many sides are connected.
		for (UFGFactoryConnectionComponent* Connector : ContainerConnectors) {
			if (Connector->IsConnected()) {
				if (Connector->GetDirection() == EFactoryConnectionDirection::FCD_INPUT) {
					bIsInputConnected = true;
				}
				else if (Connector->GetDirection() == EFactoryConnectionDirection::FCD_OUTPUT) {
					bIsOutputConnected = true;
				}
			}
		}

		// Check if the container is connected both sides
		bool bIsConnectedOnOneSide = bIsOutputConnected == !bIsInputConnected;

		// Check connections
		if (bIsConnectedOnOneSide) {
			Bounds.Add(Buildable);
		}
		else {
			UnclassifiedMembers.Add(Buildable);
		}
	}
	else {
		UnclassifiedMembers.Add(Buildable);
	}
}