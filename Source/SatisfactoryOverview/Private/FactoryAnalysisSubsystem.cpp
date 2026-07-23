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
#include "Buildables/FGBuildablePipelineAttachment.h"

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
	FactoryUnionFind = FBuildableUnionFind();

	TArray<AActor*> AllActors;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AFGBuildableConveyorBase::StaticClass(), AllActors);

	TArray<AFGBuildableConveyorBase*> Belts;
	Belts.Reserve(AllActors.Num());
	for (AActor* Actor : AllActors)
	{
		AFGBuildableConveyorBase* Buildable = Cast<AFGBuildableConveyorBase>(Actor);
		if (Buildable)
		{
			Belts.Add(Buildable);
		}
	}

	ConveyorWorkQueue.Start(
		Belts,
		[this](AFGBuildableConveyorBase* const& Buildable, int32 Index) { ProcessConveyor(Buildable, Index); },
		[this]() { BeginFactoryProcessing(); }
	);
}

void UFactoryAnalysisSubsystem::ProcessConveyor(AFGBuildableConveyorBase* const& Buildable, int32 Index) 
{
	FactoryUnionFind.MakeSet(Buildable);

	// Just union connections
	if (Buildable->GetConnection0()->IsConnected()) {
		AFGBuildable* Connected0 = Buildable->GetConnection0()->GetConnection()->GetOuterBuildable();
		if (!Cast<AFGBuildableFactory>(Connected0)) {
			FactoryUnionFind.MakeSet(Connected0);
			FactoryUnionFind.Union(Buildable, Connected0);
		}
	}

	if (Buildable->GetConnection1()->IsConnected()) {
		AFGBuildable* Connected1 = Buildable->GetConnection1()->GetConnection()->GetOuterBuildable();
		if (!Cast<AFGBuildableFactory>(Connected1)) {
			FactoryUnionFind.MakeSet(Connected1);
			FactoryUnionFind.Union(Buildable, Connected1);
		}
	}
}

void UFactoryAnalysisSubsystem::BeginFactoryProcessing() 
{
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

	FactoryWorkQueue.Start(
		RelevantBuildables,
		[this](AFGBuildableFactory* const& Buildable, int32 Index) { ProcessFactory(Buildable, Index); },
		[this]() { BeginContainerProcessing(); }
	);
}

void UFactoryAnalysisSubsystem::ProcessFactory(AFGBuildableFactory* const& Buildable, int32 Index)
{
	// Containers are skipped, their logic lives in the third pass.
	if (Cast<AFGBuildableStorage>(Buildable) && !Cast<AFGCentralStorageContainer>(Buildable))
	{
		UE_LOG(LogTemp, Warning, TEXT("[FactoryAnalysis] Skipping buildable %s (index %d) because it is a container."), *Buildable->GetName(), Index);
		return;
	}

	// Pipe attachments are NOT actually helping produce anything and thus are useless here. Skip them.
	if (Cast<AFGBuildablePipelineAttachment>(Buildable)) {
		return;
	}

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

	FactoryUnionFind.MakeSet(Buildable);

	// Unions anything connected via belt, from the net union type present previously
	for (UFGFactoryConnectionComponent* Connection : FactoryConnectors) {
		if (!Connection->IsConnected()) continue;
		AFGBuildable* Outer = Connection->GetConnection()->GetOuterBuildable();

		// Assumes that the connected belts are already in the set, lmao
		FactoryUnionFind.Union(Buildable, Outer);
	}

	// Unions anything connected via pipe
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

void UFactoryAnalysisSubsystem::BeginContainerProcessing() 
{
	TArray<AActor*> AllActors;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AFGBuildableStorage::StaticClass(), AllActors);

	TArray<AFGBuildableStorage*> Containers;
	Containers.Reserve(AllActors.Num());
	for (AActor* Actor : AllActors)
	{
		AFGBuildableStorage* Buildable = Cast<AFGBuildableStorage>(Actor);
		if (Buildable && !Cast<AFGCentralStorageContainer>(Buildable))
		{
			Containers.Add(Buildable);
		}
	}

	ContainerWorkQueue.Start(
		Containers,
		[this](AFGBuildableStorage* const& Buildable, int32 Index) { ProcessContainer(Buildable, Index); },
		[this]() { OnScanComplete(); }
	);
}

void UFactoryAnalysisSubsystem::ProcessContainer(AFGBuildableStorage* const& Buildable, int32 Index) {

	// Get all connectors
	TArray<UFGFactoryConnectionComponent*> ContainerConnectors;
	ContainerConnectors = Buildable->GetConnectionComponents();

	if (ContainerConnectors.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[FactoryAnalysis] Skipping buildable %s (index %d) because it has no connectors."), *Buildable->GetName(), Index);
		return;
	}

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
	bool bIsConnectedOnBothSides = bIsOutputConnected && bIsInputConnected;

	// Do actual things now
	if (bTreatContainersAsFactoryEnd && bIsConnectedOnBothSides) {
		// Sike, I won't do this yet (TODO)
	}
	else {

		FactoryUnionFind.MakeSet(Buildable);

		// Assumes all belts have a set
		for (UFGFactoryConnectionComponent* Connector : ContainerConnectors) {
			if (!Connector->IsConnected()) continue;
			AFGBuildable* Outer = Connector->GetConnection()->GetOuterBuildable();

			// i forgor
			FactoryUnionFind.Union(Buildable, Outer);
		}
	}
}

void UFactoryAnalysisSubsystem::OnScanComplete()
{
	// Finish unioning pipe networks
	for (auto& Pair : PipeNetworkGroups)
	{
		TArray<AFGBuildableFactory*>& Members = Pair.Value;
		for (int32 i = 1; i < Members.Num(); ++i)
		{
			FactoryUnionFind.Union(Members[0], Members[i]);
		}
	}

	// Initialize clusters
	TArray<UFactoryCluster*> Clusters;
	for (auto& RawClusterPair : FactoryUnionFind.GetClusters())
	{
		UFactoryCluster* Cluster = NewObject<UFactoryCluster>(this);
		for (AFGBuildable* Member : RawClusterPair.Value)
		{
			if (AFGBuildableFactory* MemberFactory = Cast<AFGBuildableFactory>(Member)) {
				Cluster->AddToFactory(MemberFactory);
			}
		}

		if (!Cluster->GetMembers().IsEmpty()) {
			Cluster->PipeNetworkGroups = PipeNetworkGroups;
			Clusters.Add(Cluster);
		}
	}

	// Log results
	UE_LOG(LogTemp, Warning, TEXT("[FactoryAnalysis] Scan complete: %d clusters found."), Clusters.Num());
	TArray<UFactoryCluster*> FilteredClusters = UFactoryCluster::GetAllValidClusters(Clusters);
	UE_LOG(LogTemp, Warning, TEXT("[FactoryAnalysis] %d valid clusters found."), FilteredClusters.Num());

	for (auto& Cluster : FilteredClusters)
	{
		UE_LOG(LogTemp, Warning, TEXT("[FactoryAnalysis]   Cluster (root=%s): %d buildables, IsNature=%d"),
			*Cluster->GetName(), Cluster->GetMembers().Num(), Cluster->IsNature());

		FItemBalance Balance = Cluster->ComputeItemBalanceSheet(false);
		UE_LOG(LogTemp, Warning, TEXT("[FactoryAnalysis]     Members:"));

		// Print buildings.
		for (TWeakObjectPtr<AFGBuildableFactory> WeakFactory : Cluster->GetMembers()) 
		{
			AFGBuildableFactory* Factory = WeakFactory.Get();
			if (!Factory) continue;

			UE_LOG(LogTemp, Warning, TEXT("[FactoryAnalysis]       -> %s (%s)"), *Factory->GetName(), *Factory->GetClass()->GetName());
		}

		// Print items produced.
		UE_LOG(LogTemp, Warning, TEXT("[FactoryAnalysis]     Items produced:"));
		TMap<TSubclassOf< class UFGItemDescriptor >, float> ProducedItems = Balance.Export;
		for (TPair<TSubclassOf< class UFGItemDescriptor >, float> Item : ProducedItems)
		{
			UE_LOG(LogTemp, Warning, TEXT("[FactoryAnalysis]       -> %s - %.2f/min"), *Item.Key->GetName(), Item.Value);
		}

		// Print items consumed.
		UE_LOG(LogTemp, Warning, TEXT("[FactoryAnalysis]     Items consumed:"));
		TMap<TSubclassOf< class UFGItemDescriptor >, float> ConsumedItems = Balance.Import;
		for (TPair<TSubclassOf< class UFGItemDescriptor >, float> Item : ConsumedItems)
		{
			UE_LOG(LogTemp, Warning, TEXT("[FactoryAnalysis]       -> %s - %.2f/min"), *Item.Key->GetName(), Item.Value);
		}

		// Print items produced by miners
		UE_LOG(LogTemp, Warning, TEXT("[FactoryAnalysis]     Items supplied by miners:"));
		TMap<TSubclassOf< class UFGItemDescriptor >, float> SuppliedItems = Balance.Produced;
		for (TPair<TSubclassOf< class UFGItemDescriptor >, float> Item : SuppliedItems)
		{
			UE_LOG(LogTemp, Warning, TEXT("[FactoryAnalysis]       -> %s - %.2f/min"), *Item.Key->GetName(), Item.Value);
		}

		// Print items consumed by consumers
		UE_LOG(LogTemp, Warning, TEXT("[FactoryAnalysis]     Items used by generators, portals or apas:"));
		TMap<TSubclassOf< class UFGItemDescriptor >, float> UsedItems = Balance.Consumed;
		for (TPair<TSubclassOf< class UFGItemDescriptor >, float> Item : UsedItems)
		{
			UE_LOG(LogTemp, Warning, TEXT("[FactoryAnalysis]       -> %s - %.2f/min"), *Item.Key->GetName(), Item.Value);
		}

	}

	OnFactoryScanFinished.Broadcast();
}
	
void UFactoryAnalysisSubsystem::Tick(float DeltaTime)
{
	if (!ConveyorWorkQueue.IsComplete())
	{
		ConveyorWorkQueue.ProcessBudget();
	}
	else if (!FactoryWorkQueue.IsComplete())
	{
		FactoryWorkQueue.ProcessBudget();
	}
	else if (!ContainerWorkQueue.IsComplete())
	{
		ContainerWorkQueue.ProcessBudget();
	}
}