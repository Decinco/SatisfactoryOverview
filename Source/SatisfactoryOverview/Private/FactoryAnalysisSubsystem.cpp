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
#include "FactoryCluster.h"

UFactoryAnalysisSubsystem* UFactoryAnalysisSubsystem::GetFactoryAnalysisSubsystem(const UObject* WorldContextObject)
{
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull) : nullptr;
	return World ? World->GetSubsystem<UFactoryAnalysisSubsystem>() : nullptr;
}

void UFactoryAnalysisSubsystem::StartScan()
{
	UE_LOG(LogTemp, Warning, TEXT("[SatisfactoryOverview] Begginning Factory Scan preparations..."));

	UnionFind = FBuildableUnionFind();
	PipeNetworkGroups.Empty();


	UE_LOG(LogTemp, Warning, TEXT("[SatisfactoryOverview] Scanning world for buildables..."));
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

	UnionFind.MakeSet(Buildable);

	// Print all buildable names and their connectors for debugging
	for (UFGFactoryConnectionComponent* Connector : FactoryConnectors)
	{
		if (Connector->IsConnected()) 
		{
			TSet<AFGBuildable*> VisitedConveyors;
			TArray<FResolvedEndpoint> Connections = ResolveConnections(Connector->GetConnection(), VisitedConveyors);

			for (FResolvedEndpoint Connection : Connections)
			{
				AFGBuildableFactory* ConnectedBuildable = Connection.Buildable.Get();

				UE_LOG(LogTemp, Warning, TEXT("[FactoryAnalysis] Buildable %s is connected to %s via connector %s"),
					*Buildable->GetName(), *ConnectedBuildable->GetName(), *ConnectedBuildable->GetName());
				UnionFind.MakeSet(ConnectedBuildable);
				UnionFind.Union(Buildable, ConnectedBuildable);
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
		}
		BuildConnectorGraph(Cluster);
		Clusters.Add(Cluster);
	}

	UE_LOG(LogTemp, Warning, TEXT("[FactoryAnalysis] Scan complete: %d clusters found."), Clusters.Num());

	for (auto& Cluster : Clusters)
	{
		UE_LOG(LogTemp, Warning, TEXT("[FactoryAnalysis]   Cluster (root=%s): %d buildables"),
			*Cluster->GetName(), Cluster->Members.Num());

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
			}
		}
	}

	OnFactoryScanFinished.Broadcast();
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

		if (AFGBuildableFactory* Factory = Cast<AFGBuildableFactory>(Outer))
		{
			if (Cast<AFGBuildableManufacturer>(Factory) || Cast<AFGBuildableResourceExtractor>(Factory) || UFactoryCluster::IsBoundaryBuildable(Factory))
			{
				FResolvedEndpoint Endpoint;
				Endpoint.Buildable = Factory;
				Endpoint.EndpointDirection = Current->GetDirection(); // Current IS the terminal's own connector here
				Result.Add(Endpoint);
				return Result;
			}
			// Storage containers etc. -- pass through, ignored as terminals
			for (UFGFactoryConnectionComponent* Conn : Factory->GetConnectionComponents())
			{
				if (Conn != Current && Conn->IsConnected())
					Result.Append(ResolveConnections(Conn->GetConnection(), Visited));
			}
			return Result;
		}

		if (AFGBuildableConveyorBase* Conveyor = Cast<AFGBuildableConveyorBase>(Outer))
		{
			UFGFactoryConnectionComponent* OtherSide = (Current == Conveyor->GetConnection0()) ? Conveyor->GetConnection1() : Conveyor->GetConnection0();
			if (OtherSide && OtherSide->IsConnected()) { Current = OtherSide->GetConnection(); continue; }
			break;
		}

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
	
void UFactoryAnalysisSubsystem::Tick(float DeltaTime)
{
	WorkQueue.ProcessBudget();
}