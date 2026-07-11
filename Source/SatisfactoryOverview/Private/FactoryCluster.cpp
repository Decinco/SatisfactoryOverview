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

TMap<TSubclassOf< class UFGItemDescriptor >, float> UFactoryCluster::GetProducedItems() const
{
	UE_LOG(LogTemp, Warning, TEXT("[FactoryAnalysis] GetProducedItems - %d BoundaryRefs"), BoundaryRefs.Num());

	TMap<TSubclassOf< class UFGItemDescriptor >, float> ItemSet;

	// Build ItemSet
	for (FResolvedEndpoint Endpoint : BoundaryRefs) {

		// Only if input
		if (Endpoint.EndpointDirection == EConnectionDirection::Input) {

			AFGBuildableFactory* EndpointFactory = Endpoint.Buildable.Get();
			UE_LOG(LogTemp, Warning, TEXT("[FactoryAnalysis]   Endpoint: %s (Input)"), EndpointFactory ? *EndpointFactory->GetName() : TEXT("null"));

			// An output boundary's input connects to the previous buildings' output.
			TArray<FConnectorResolution> Connections = GetConnectorsTo(EndpointFactory, EConnectionDirection::Output);
			UE_LOG(LogTemp, Warning, TEXT("[FactoryAnalysis]     %d connections from Output side"), Connections.Num());

			// Takes item of type
			for (FConnectorResolution Connection : Connections) {

				if (AFGBuildableManufacturer* Manufacturer = Cast<AFGBuildableManufacturer>(Connection.SourceOwner)) {

					// Machine data
					float productionAmplifier = Manufacturer->GetCurrentProductionBoost();
					float cycleTime = Manufacturer->GetProductionCycleTime();

					UE_LOG(LogTemp, Warning, TEXT("[FactoryAnalysis]         Manufacturer: %s, cycleTime: %.2f, productionAmplifier: %.2f"),
						*Manufacturer->GetName(), cycleTime, productionAmplifier);

					// Get products
					TSubclassOf<class UFGRecipe> Recipe = Manufacturer->GetCurrentRecipe();
					TArray<FItemAmount> Products = UFGRecipe::GetProducts(Recipe);

					UE_LOG(LogTemp, Warning, TEXT("[FactoryAnalysis]         Recipe: %s, %d products"),
						Recipe ? *Recipe->GetName() : TEXT("null"), Products.Num());

					// Only products that match the connector type we're looking through
					for (FItemAmount Product : Products) {
						TSubclassOf< class UFGItemDescriptor > item = Product.ItemClass;
						int amount = Product.Amount;

						if (MapItemType(UFGItemDescriptor::GetForm(item)) == Connection.Kind) {
							// mafs
							float producedAmount = 60 / cycleTime * amount * productionAmplifier;

							UE_LOG(LogTemp, Warning, TEXT("[FactoryAnalysis]           + %s: raw %d * 60/%.2f * %.2f = %.2f/min"),
								*item->GetName(), amount, cycleTime, productionAmplifier, producedAmount);

							if (ItemSet.Contains(item)) {
								ItemSet[item] += producedAmount;
								UE_LOG(LogTemp, Warning, TEXT("[FactoryAnalysis]             accumulated: %.2f/min"), ItemSet[item]);
							}
							else {
								ItemSet.Add(item, producedAmount);
							}

						}
						else {
							UE_LOG(LogTemp, Warning, TEXT("[FactoryAnalysis]           - %s skipped (form mismatch)"), *item->GetName());
						}
					}
				}
				else {
					UE_LOG(LogTemp, Warning, TEXT("[FactoryAnalysis]       Not a manufacturer, skipping"));
				}
			}
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("[FactoryAnalysis] GetProducedItems final: %d items"), ItemSet.Num());

	return ItemSet;
}

TMap<TSubclassOf< class UFGItemDescriptor >, float> UFactoryCluster::GetConsumedItems(TArray<FString>& out_InputErrors) const
{
	TMap<TSubclassOf< class UFGItemDescriptor >, float> ItemSet;

	for (FResolvedEndpoint Endpoint : BoundaryRefs) {

		// Only if output
		if (Endpoint.EndpointDirection == EConnectionDirection::Output) {

			// An input boundary's output connects to the next buildings' input.
			TArray<FConnectorResolution> Connections = GetConnectorsTo(Endpoint.Buildable.Get(), EConnectionDirection::Input);

			// Currently takes all items of type
			for (FConnectorResolution Connection : Connections) {
				continue; // TODO
			}

		}
	}

	return ItemSet;
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