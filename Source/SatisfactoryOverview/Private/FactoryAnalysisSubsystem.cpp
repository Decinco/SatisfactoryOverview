// Main headers
#include "FactoryAnalysisSubsystem.h"
#include "Engine/Engine.h"

// Connectors (to belts and pipes)
#include "FGFactoryConnectionComponent.h"
#include "FGPipeConnectionComponent.h"

// Buildables (factories, belts, and attachments)
#include "Buildables/FGBuildable.h"
#include "Buildables/FGBuildableFactory.h"
#include "Buildables/FGBuildableConveyorBase.h"
#include "Buildables/FGBuildableConveyorAttachment.h"

// Utility for union-find clustering
#include "Kismet/GameplayStatics.h"
#include "Buildables/FGBuildableManufacturer.h"
#include "Buildables/FGBuildableResourceExtractor.h"
#include "Buildables/FGCentralStorageContainer.h"
#include "FactoryCluster.h"

UFactoryAnalysisSubsystem* UFactoryAnalysisSubsystem::GetFactoryAnalysisSubsystem(const UObject* WorldContextObject)
{
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull) : nullptr;
	return World ? World->GetSubsystem<UFactoryAnalysisSubsystem>() : nullptr;
}

void UFactoryAnalysisSubsystem::StartScan()
{

	UnionFind = FBuildableUnionFind();
	PipeNetworkGroups.Empty();

	TArray<AActor*> AllActors;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AFGBuildableFactory::StaticClass(), AllActors);

	TArray<AFGBuildableFactory*> RelevantBuildables;
	RelevantBuildables.Reserve(AllActors.Num());
	for (AActor* Actor : AllActors)
	{
		AFGBuildableFactory* Buildable = Cast<AFGBuildableFactory>(Actor);
		if (Buildable)
		{
			RelevantBuildables.Add(Buildable);
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("[SatisfactoryOverview] Scan starting: %d of %d total buildables have connectors."),
		RelevantBuildables.Num(), AllActors.Num());

	WorkQueue.Start(
		RelevantBuildables,
		[this](AFGBuildableFactory* const& Buildable, int32 Index) { ProcessBuildable(Buildable, Index); },
		[this]() { OnScanComplete(); }
	);
}

void UFactoryAnalysisSubsystem::ProcessBuildable(AFGBuildableFactory* const& Buildable, int32 Index)
{
	// Get all factory connection components for this buildable
	TArray<UFGFactoryConnectionComponent*> FactoryConnectors;
	FactoryConnectors = Buildable->GetConnectionComponents();

	// Get all pipe connections for this buildable
	TArray<UFGPipeConnectionComponent*> PipeConnections;
	Buildable->GetComponents<UFGPipeConnectionComponent>(PipeConnections);

	if(FactoryConnectors.Num() == 0 && PipeConnections.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[FactoryAnalysis] Skipping buildable %s (index %d) because it has no connectors."), *Buildable->GetName(), Index);
		return;
	}

	// Containers aren't ever put into a set
	if (Cast<AFGBuildableStorage>(Buildable) && !Cast<AFGCentralStorageContainer>(Buildable))
	{
		UE_LOG(LogTemp, Warning, TEXT("[FactoryAnalysis] Skipping buildable %s (index %d) because it is a container."), *Buildable->GetName(), Index);
		return;
	}

	UnionFind.MakeSet(Buildable);

	// Print all buildable names and their connectors for debugging
	for (UFGFactoryConnectionComponent* Connector : FactoryConnectors)
	{
		if (Connector->IsConnected())
		{
			TSet<AFGBuildable*> VisitedConveyors;
			TArray<FResolvedEndpoint> Connections = ResolveConnections(Connector->GetConnection(), VisitedConveyors);

			for (const FResolvedEndpoint& Connection : Connections)
			{
				AFGBuildableFactory* ConnectedBuildable = Connection.Buildable.Get();
				if (!ConnectedBuildable)
				{
					continue;
				}

				if (Connection.BoundType == EFactoryBoundaryType::ContainerBoundary)
				{
					// Qualifying container, never unioned
					UE_LOG(LogTemp, Warning, TEXT("[FactoryAnalysis] Buildable %s notes container boundary %s, not unioned"),
						*Buildable->GetName(), *ConnectedBuildable->GetName());
					PendingBoundaryEndpoints.FindOrAdd(Buildable).AddUnique(Connection);
				}
				else
				{
					// Ordinary machine, or a physical terminal, unioned
					UE_LOG(LogTemp, Warning, TEXT("[FactoryAnalysis] Buildable %s is connected to %s via connector %s (TerminalKind=%d)"),
						*Buildable->GetName(), *ConnectedBuildable->GetName(), *ConnectedBuildable->GetName(), static_cast<int32>(Connection.BoundType));
					UnionFind.MakeSet(ConnectedBuildable);
					UnionFind.Union(Buildable, ConnectedBuildable);
				}
			}
		}
	}

	for (UFGPipeConnectionComponent* PipeConn : PipeConnections)
	{
		int32 NetworkId = PipeConn->GetPipeNetworkID();
		if (NetworkId != INDEX_NONE) // INDEX_NONE means not connected to any network
		{
			UE_LOG(LogTemp, Warning, TEXT("[FactoryAnalysis] Buildable %s is part of pipe network %d via pipe connection %s"),
				*Buildable->GetName(), NetworkId, *PipeConn->GetName());
			PipeNetworkGroups.FindOrAdd(NetworkId).Add(Buildable);
		}
	}
}

TArray <FResolvedEndpoint> UFactoryAnalysisSubsystem::ResolveConnections(UFGFactoryConnectionComponent* StartConnector, TSet<AFGBuildable*>& Visited)
{
	TArray<FResolvedEndpoint> Result;
	UFGFactoryConnectionComponent* Current = StartConnector;
	while (Current)
	{
		AFGBuildable* Outer = Current->GetOuterBuildable();
		if (!Outer) break;
		if (Visited.Contains(Outer)) break;
		Visited.Add(Outer);

		// Terminal Check
		const EFactoryBoundaryType Terminal = UFactoryCluster::ClassifyTerminal(Outer);
		if (Terminal != EFactoryBoundaryType::None)
		{
			FResolvedEndpoint Endpoint;
			Endpoint.Buildable = Cast<AFGBuildableFactory>(Outer);
			Endpoint.EndpointDirection = Current->GetDirection(); // Current IS the terminal's own connector here
			Endpoint.BoundType = Terminal;
			Result.Add(Endpoint);
			return Result;
		}

		if (AFGBuildableFactory* Factory = Cast<AFGBuildableFactory>(Outer))
		{
			// Manufacturer check
			if (Cast<AFGBuildableManufacturer>(Factory))
			{
				FResolvedEndpoint Endpoint;
				Endpoint.Buildable = Factory;
				Endpoint.EndpointDirection = Current->GetDirection();
				Endpoint.BoundType = EFactoryBoundaryType::None;
				Result.Add(Endpoint);
				return Result;
			}

			// Container check
			if (AFGBuildableStorage* Container = Cast<AFGBuildableStorage>(Factory))
			{
				// Will check if the container is a boundary and add it to a special variable in the cluster if so.
				Result.Append(ResolveContainer(Container, Current, Visited));
				return Result;
			}

			// Anything else factory-like (unclassified attachments etc.) -- pass through, ignored as terminals.
			for (UFGFactoryConnectionComponent* Conn : Factory->GetConnectionComponents())
			{
				if (Conn != Current && Conn->IsConnected())
					Result.Append(ResolveConnections(Conn->GetConnection(), Visited));
			}
			return Result;
		}

		// Conveyor check (If connected to a belt, follow it)
		if (AFGBuildableConveyorBase* Conveyor = Cast<AFGBuildableConveyorBase>(Outer))
		{
			UFGFactoryConnectionComponent* OtherSide = (Current == Conveyor->GetConnection0()) ? Conveyor->GetConnection1() : Conveyor->GetConnection0();
			if (OtherSide && OtherSide->IsConnected()) { Current = OtherSide->GetConnection(); continue; }
			break;
		}

		// Splitter/merger check (If connected to a splitter/merger, follow all other connections)
		if (AFGBuildableConveyorAttachment* Attachment = Cast<AFGBuildableConveyorAttachment>(Outer))
		{
			TArray<UFGFactoryConnectionComponent*> AllConnections;
			Attachment->GetComponents<UFGFactoryConnectionComponent>(AllConnections);
			for (auto* Connection : AllConnections)
				if (Connection != Current && Connection->IsConnected())
					Result.Append(ResolveConnections(Connection->GetConnection(), Visited));
			return Result;
		}
		break;
	}
	return Result;
}

TArray<FResolvedEndpoint> UFactoryAnalysisSubsystem::ResolveContainer(AFGBuildableStorage* Container, UFGFactoryConnectionComponent* EntryConnector, TSet<AFGBuildable*>& Visited)
{
	// Condition 1: Playstyle toggle makes every container an unconditional terminal.
	if (bTreatContainersAsFactoryEnd)
	{
		return { MakeContainerTerminalEndpoint(Container, EntryConnector) };
	}

	TArray<UFGFactoryConnectionComponent*> AllConnectors;
	Container->GetComponents<UFGFactoryConnectionComponent>(AllConnectors);

	// Condition 2: no other connected connector.
	bool bHasOtherConnectedConnector = false;
	for (UFGFactoryConnectionComponent* Conn : AllConnectors)
	{
		if (Conn != EntryConnector && Conn->IsConnected())
		{
			bHasOtherConnectedConnector = true;
			break;
		}
	}
	if (!bHasOtherConnectedConnector)
	{
		return { MakeContainerTerminalEndpoint(Container, EntryConnector) };
	}

	// Otherwise, resolve through as a transparent pass-through, same as any other attachment.
	TArray<FResolvedEndpoint> PassThroughResult;
	for (UFGFactoryConnectionComponent* Conn : AllConnectors)
	{
		if (Conn != EntryConnector && Conn->IsConnected())
			PassThroughResult.Append(ResolveConnections(Conn->GetConnection(), Visited));
	}

	// Condition 1: if the next found machine is also a boundary.
	const bool bDownstreamIsTerminal = PassThroughResult.ContainsByPredicate([](const FResolvedEndpoint& Endpoint)
		{
			return Endpoint.BoundType != EFactoryBoundaryType::None;
		});

	if (bDownstreamIsTerminal)
	{
		return { MakeContainerTerminalEndpoint(Container, EntryConnector) };
	}

	return PassThroughResult;
}

FResolvedEndpoint UFactoryAnalysisSubsystem::MakeContainerTerminalEndpoint(AFGBuildableStorage* Container, UFGFactoryConnectionComponent* EntryConnector)
{
	FResolvedEndpoint Endpoint;
	Endpoint.Buildable = Cast<AFGBuildableFactory>(Container);
	Endpoint.EndpointDirection = EntryConnector ? EntryConnector->GetDirection() : EFactoryConnectionDirection::FCD_ANY;
	Endpoint.BoundType = EFactoryBoundaryType::ContainerBoundary;
	return Endpoint;
}

void UFactoryAnalysisSubsystem::OnScanComplete()
{
	for (auto& Pair : PipeNetworkGroups)
	{
		TArray<AFGBuildableFactory*>& Members = Pair.Value;
		for (int32 i = 1; i < Members.Num(); ++i)
		{
			UnionFind.Union(Members[0], Members[i]);
		}
	}

	TArray<UFactoryCluster*> Clusters;
	for (auto& RawClusterPair : UnionFind.GetClusters())
	{
		UFactoryCluster* Cluster = NewObject<UFactoryCluster>(this);
		for (AFGBuildableFactory* Member : RawClusterPair.Value)
		{
			Cluster->Members.Add(Member);

			// Re-attach any terminal references recorded against this member during ProcessBuildable.
			if (TArray<FResolvedEndpoint>* Boundaries = PendingBoundaryEndpoints.Find(Member))
			{
				for (const FResolvedEndpoint& Boundary : *Boundaries)
				{
					Cluster->BoundaryRefs.AddUnique(Boundary);
				}
			}
		}
		BuildConnectorGraph(Cluster);
		Clusters.Add(Cluster);
	}

	UE_LOG(LogTemp, Warning, TEXT("[FactoryAnalysis] Scan complete: %d clusters found."), Clusters.Num());

	for (auto& Cluster : Clusters)
	{
		UE_LOG(LogTemp, Warning, TEXT("[FactoryAnalysis]   Cluster (root=%s): %d buildables, %d boundary refs, IsNature=%d"),
			*Cluster->GetName(), Cluster->Members.Num(), Cluster->BoundaryRefs.Num(), Cluster->IsNature());

		// Print recipes produced at the endpoints.
		UE_LOG(LogTemp, Warning, TEXT("[FactoryAnalysis]     Recipes produced at endpoints:"));
		TArray<TSubclassOf<UFGRecipe>> ProducedRecipes = Cluster->GetProducedRecipes();
		for (TSubclassOf<UFGRecipe> Recipe : ProducedRecipes)
		{
			UE_LOG(LogTemp, Warning, TEXT("[FactoryAnalysis]       -> %s"), *Recipe->GetName());
		}

		if (Cluster->Members.Num() <= 5)
		{
			TArray<TWeakObjectPtr<AFGBuildableFactory>> Members = Cluster->Members;

			// Print stats about each member of the cluster
			for (TWeakObjectPtr<AFGBuildableFactory> MemberPtr : Members)
			{
				auto Member = MemberPtr.Get();

				FVector Location = Member->GetActorLocation();

				TArray<UFGFactoryConnectionComponent*> Connectors;
				Member->GetComponents<UFGFactoryConnectionComponent>(Connectors);
				int32 ConnectedCount = 0;
				for (auto* Conn : Connectors)
				{
					if (Conn->IsConnected()) ConnectedCount++;
				}

				UE_LOG(LogTemp, Warning, TEXT("[FactoryAnalysis]     -> %s at (%.0f, %.0f, %.0f) | Configured=%d Producing=%d HasPower=%d | Connectors: %d/%d connected"),
					*Member->GetName(), Location.X, Location.Y, Location.Z,
					Member->IsConfigured(), Member->IsProducing(), Member->HasPower(),
					ConnectedCount, Connectors.Num());

				// Also log class name and inherited classes name
				UClass* Class = Member->GetClass();
				FString ClassName = Class->GetName();
				FString InheritedClasses;
				while (Class->GetSuperClass())
				{
					Class = Class->GetSuperClass();
					InheritedClasses += Class->GetName() + TEXT(" -> ");
				}
				UE_LOG(LogTemp, Warning, TEXT("[FactoryAnalysis]         Class: %s | Inherited: %s"), *ClassName, *InheritedClasses);

			}
		}
	}

	OnFactoryScanFinished.Broadcast();
}

void UFactoryAnalysisSubsystem::BuildConnectorGraph(UFactoryCluster* Cluster)
{
	for (const TWeakObjectPtr<AFGBuildableFactory>& Weak : Cluster->Members)
	{
		AFGBuildableManufacturer* Manufacturer = Cast<AFGBuildableManufacturer>(Weak.Get());
		if (!Manufacturer) continue;

		for (UFGFactoryConnectionComponent* Conn : Manufacturer->GetConnectionComponents())
		{
			FConnectorResolution Resolution;
			Resolution.SourceConnector = Conn;
			Resolution.SourceDirection = Conn->GetDirection();

			if (Conn->IsConnected())
			{
				TSet<AFGBuildable*> Visited;
				Resolution.Endpoints = ResolveConnections(Conn->GetConnection(), Visited);
			}
			Cluster->ConnectorGraph.Add(Resolution);
		}
	}
}
	
void UFactoryAnalysisSubsystem::Tick(float DeltaTime)
{
	WorkQueue.ProcessBudget();
}