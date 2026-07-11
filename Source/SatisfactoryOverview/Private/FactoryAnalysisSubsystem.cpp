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

// Connection resolver
#include "FactoryConnectionResolver.h"

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
			TArray<FResolvedEndpoint> Connections = FactoryConnectionResolver::ResolveBeltConnections(Connector->GetConnection(), VisitedConveyors);

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

					// Also record physical terminals as boundary refs
					if (Connection.BoundType != EFactoryBoundaryType::None)
					{
						PendingBoundaryEndpoints.FindOrAdd(Buildable).AddUnique(Connection);
					}
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
		Cluster->RebuildEndpointIndex();
		Clusters.Add(Cluster);
	}

	UE_LOG(LogTemp, Warning, TEXT("[FactoryAnalysis] Scan complete: %d clusters found."), Clusters.Num());

	for (auto& Cluster : Clusters)
	{
		UE_LOG(LogTemp, Warning, TEXT("[FactoryAnalysis]   Cluster (root=%s): %d buildables, %d boundary refs, IsNature=%d"),
			*Cluster->GetName(), Cluster->Members.Num(), Cluster->BoundaryRefs.Num(), Cluster->IsNature());

		// Print recipes produced at the endpoints.
		UE_LOG(LogTemp, Warning, TEXT("[FactoryAnalysis]     Recipes produced at endpoints:"));
		TMap<TSubclassOf< class UFGItemDescriptor >, float> ProducedItems = Cluster->GetProducedItems();
		for (TPair<TSubclassOf< class UFGItemDescriptor >, float> Item : ProducedItems)
		{
			UE_LOG(LogTemp, Warning, TEXT("[FactoryAnalysis]       -> %s - %.2f/min"), *Item.Key->GetName(), Item.Value);
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
			Resolution.SourceDirection = FConnectionDirectionMapper::Map(Conn->GetDirection());

			if (Conn->IsConnected())
			{
				TSet<AFGBuildable*> Visited;
				Resolution.Endpoints = FactoryConnectionResolver::ResolveBeltConnections(Conn->GetConnection(), Visited);
			}
			Cluster->ConnectorGraph.Add(Resolution);
		}

		// Get its pipe connections
		TArray<UFGPipeConnectionComponent*> PipeConnections;
		Manufacturer->GetComponents<UFGPipeConnectionComponent>(PipeConnections);
		for (UFGPipeConnectionComponent* PipeConn : PipeConnections)
		{
			Cluster->ConnectorGraph.Add(FactoryConnectionResolver::ResolvePipeConnections(Manufacturer, PipeConn, PipeNetworkGroups));
		}
	}
}
	
void UFactoryAnalysisSubsystem::Tick(float DeltaTime)
{
	WorkQueue.ProcessBudget();
}