#include "FactoryCluster.h"

// Generic energy-accepting buildable
#include "Buildables/FGBuildableFactory.h"

// Generic crafter (aka factories or machines)
#include "Buildables/FGBuildableManufacturer.h"

// Generic resource extractor
#include "Buildables/FGBuildableResourceExtractor.h"

// Generic energy producer (aka generators) may be used later lol
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


TArray<TSubclassOf<UFGRecipe>> UFactoryCluster::GetProducedRecipes() const
{
	TArray<TSubclassOf<UFGRecipe>> Result;

	for (const FConnectorResolution& Res : ConnectorGraph)
	{
		if (Res.SourceDirection != EFactoryConnectionDirection::FCD_OUTPUT)
		{
			continue;
		}
		// By default, connecting to nothing is also considered "outermost", since it's the end of the production chain. TODO: add analysis to see what exactly is being output to other manufacturers, and what is being output to extractors or export boundaries, based on connector contents.
		bool bIsOutermost = Res.Endpoints.Num() == 0;

		// Check if it's the outermost output of a production chain regardless, based on its connection to a output boundary (station or space elevator).
		for (const FResolvedEndpoint& Endpoint : Res.Endpoints)
		{
			AFGBuildableFactory* EndpointBuildable = Endpoint.Buildable.Get();
			if (EndpointBuildable
				&& IsBoundaryBuildable(EndpointBuildable)
				&& Endpoint.EndpointDirection == EFactoryConnectionDirection::FCD_OUTPUT)
			{
				bIsOutermost = true; // one positive is enough, even with splits
				break;
			}
		}

		if (!bIsOutermost || !Res.SourceConnector)
		{
			continue;
		}

		// Get this connector's owner, and if it's a manufacturer with a valid recipe, add it to the result.
		if (AFGBuildableManufacturer* Owner = Cast<AFGBuildableManufacturer>(Res.SourceConnector->GetOuterBuildable()))
		{
			if (Owner->GetCurrentRecipe())
			{
				Result.AddUnique(Owner->GetCurrentRecipe());
			}
		}
	}

	return Result;
}

TArray<TSubclassOf<UFGRecipe>> UFactoryCluster::GetConsumedRecipes(TArray<FString>& out_InputErrors) const
{
	TArray<TSubclassOf<UFGRecipe>> Result;
	out_InputErrors.Empty();

	// While a manufacturer can be considered innermost, some of its inputs may still be connected to something. TODO: add analysis to see what exactly is being input from other manufacturers, and what is being input from extractors or import boundaries, based on connector contents.
	TMap<AFGBuildableManufacturer*, bool> ManufacturerHasPositiveInput;
	TSet<AFGBuildableManufacturer*> ManufacturersWithErrors;

	for (const FConnectorResolution& Res : ConnectorGraph)
	{
		if (Res.SourceDirection != EFactoryConnectionDirection::FCD_INPUT || !Res.SourceConnector)
		{
			continue;
		}

		AFGBuildableManufacturer* Owner = Cast<AFGBuildableManufacturer>(Res.SourceConnector->GetOuterBuildable());
		if (!Owner)
		{
			continue;
		}

		if (Res.Endpoints.Num() == 0)
		{
			ManufacturersWithErrors.Add(Owner);
			continue;
		}

		// Check if it's the innermost input of a production chain, based on its connection to an extractor or import station.
		for (const FResolvedEndpoint& Endpoint : Res.Endpoints)
		{
			AFGBuildableFactory* EndpointBuildable = Endpoint.Buildable.Get();
			if (!EndpointBuildable)
			{
				continue;
			}

			const bool bIsExtractor = Cast<AFGBuildableResourceExtractor>(EndpointBuildable) != nullptr;
			const bool bIsImportBoundary = IsBoundaryBuildable(EndpointBuildable)
				&& Endpoint.EndpointDirection == EFactoryConnectionDirection::FCD_OUTPUT;

			if (bIsExtractor || bIsImportBoundary)
			{
				ManufacturerHasPositiveInput.Add(Owner, true); // one positive is enough, even with splits
				break;
			}
		}
	}

	for (const auto& Pair : ManufacturerHasPositiveInput)
	{
		if (Pair.Value && Pair.Key && Pair.Key->GetCurrentRecipe())
		{
			Result.AddUnique(Pair.Key->GetCurrentRecipe());
		}
	}

	for (AFGBuildableManufacturer* Manufacturer : ManufacturersWithErrors)
	{
		if (Manufacturer)
		{
			out_InputErrors.Add(Manufacturer->GetName());
		}
	}

	return Result;
}