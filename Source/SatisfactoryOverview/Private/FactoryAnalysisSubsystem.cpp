// Main headers
#include "FactoryAnalysisSubsystem.h"
#include "Engine/Engine.h"
#include "FGBuildableSubsystem.h"
#include "FGGameState.h"
#include "FGCharacterPlayer.h"

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

void UFactoryAnalysisSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	StartScan();

	AFGGameState* GameState = Cast<AFGGameState>(InWorld.GetGameState());
	if (GameState)
	{
		GameState->mOnActorsConstructedByPlayer.AddDynamic(this, &UFactoryAnalysisSubsystem::HandleActorsConstructedByPlayer);
	}
}

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
	UE_LOG(LogTemp, Warning, TEXT("[FactoryAnalysis] ProcessConveyor: %s"), *Buildable->GetName());

	FactoryUnionFind.MakeSet(Buildable);

	// TODO: If there's ever container filtering, do it here most likely.

	// Just union connections
	if (Buildable->GetConnection0()->IsConnected()) {
		AFGBuildable* Connected0 = Buildable->GetConnection0()->GetConnection()->GetOuterBuildable();
		UE_LOG(LogTemp, Warning, TEXT("[FactoryAnalysis]   Connection0 -> %s"), *Connected0->GetName());
		FactoryUnionFind.MakeSet(Connected0);
		FactoryUnionFind.Union(Buildable, Connected0);
	}

	if (Buildable->GetConnection1()->IsConnected()) {
		AFGBuildable* Connected1 = Buildable->GetConnection1()->GetConnection()->GetOuterBuildable();
		UE_LOG(LogTemp, Warning, TEXT("[FactoryAnalysis]   Connection1 -> %s"), *Connected1->GetName());
		FactoryUnionFind.MakeSet(Connected1);
		FactoryUnionFind.Union(Buildable, Connected1);
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
	UE_LOG(LogTemp, Warning, TEXT("[FactoryAnalysis] ProcessFactory: %s (%s)"), *Buildable->GetName(), *Buildable->GetClass()->GetName());

	// Containers are skipped, their logic lives in the third pass.
	if (Cast<AFGBuildableStorage>(Buildable) && !Cast<AFGCentralStorageContainer>(Buildable))
	{
		UE_LOG(LogTemp, Warning, TEXT("[FactoryAnalysis]   Skipped (container)"));
		return;
	}

	// Pipe attachments are NOT actually helping produce anything and thus are useless here. Skip them.
	if (Cast<AFGBuildablePipelineAttachment>(Buildable)) {
		UE_LOG(LogTemp, Warning, TEXT("[FactoryAnalysis]   Skipped (pipe attachment)"));
		return;
	}

	// Get all factory connection components for this buildable
	TArray<UFGFactoryConnectionComponent*> FactoryConnectors;
	FactoryConnectors = Buildable->GetConnectionComponents();

	// Get all pipe connections for this buildable
	TArray<UFGPipeConnectionComponent*> PipeConnections;
	Buildable->GetComponents<UFGPipeConnectionComponent>(PipeConnections);

	if (FactoryConnectors.Num() == 0 && PipeConnections.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[FactoryAnalysis]   Skipped (no connectors)"));
		return;
	}

	FactoryUnionFind.MakeSet(Buildable);

	UE_LOG(LogTemp, Warning, TEXT("[FactoryAnalysis]   %d factory connectors, %d pipe connections"), FactoryConnectors.Num(), PipeConnections.Num());

	// Unions anything connected via belt, from the net union type present previously
	for (UFGFactoryConnectionComponent* Connection : FactoryConnectors) {
		if (!Connection->IsConnected()) continue;
		AFGBuildable* Outer = Connection->GetConnection()->GetOuterBuildable();
		UE_LOG(LogTemp, Warning, TEXT("[FactoryAnalysis]   Belt union -> %s"), *Outer->GetName());

		FactoryUnionFind.MakeSet(Outer);
		FactoryUnionFind.Union(Buildable, Outer);
	}

	// Unions anything connected via pipe
	for (UFGPipeConnectionComponent* PipeConn : PipeConnections)
	{
		int32 NetworkId = PipeConn->GetPipeNetworkID();
		if (NetworkId != INDEX_NONE) // INDEX_NONE means not connected to any network
		{
			UE_LOG(LogTemp, Warning, TEXT("[FactoryAnalysis]   Pipe network %d"), NetworkId);
			PipeNetworkGroups.FindOrAdd(NetworkId).Add(Buildable);
		}
	}
}

void UFactoryAnalysisSubsystem::BeginContainerProcessing()
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
		[this]() { RegisterClusters(); }
	);
}

void UFactoryAnalysisSubsystem::ProcessContainer(AFGBuildableStorage* const& Buildable, int32 Index) {
	UE_LOG(LogTemp, Warning, TEXT("[FactoryAnalysis] ProcessContainer: %s"), *Buildable->GetName());

	// Get all connectors
	TArray<UFGFactoryConnectionComponent*> ContainerConnectors;
	ContainerConnectors = Buildable->GetConnectionComponents();

	if (ContainerConnectors.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[FactoryAnalysis]   Skipped (no connectors)"));
		return;
	}

	FactoryUnionFind.MakeSet(Buildable);

	UE_LOG(LogTemp, Warning, TEXT("[FactoryAnalysis]   %d connectors"), ContainerConnectors.Num());

	for (UFGFactoryConnectionComponent* Connector : ContainerConnectors) {
		if (!Connector->IsConnected()) continue;
		AFGBuildable* Outer = Connector->GetConnection()->GetOuterBuildable();
		UE_LOG(LogTemp, Warning, TEXT("[FactoryAnalysis]   Union -> %s"), *Outer->GetName());
		FactoryUnionFind.MakeSet(Outer);
		FactoryUnionFind.Union(Buildable, Outer);
	}
}

void UFactoryAnalysisSubsystem::RegisterClusters()
{
	Clusters.Empty();
	BuildableToCluster.Empty();

	for (auto& RawClusterPair : FactoryUnionFind.GetClusters())
	{
		UFactoryCluster* Cluster = NewObject<UFactoryCluster>(this);
		Cluster->RootBuildable = RawClusterPair.Key;
		BuildableToCluster.Add(RawClusterPair.Key, Cluster); // This will let us look in which cluster buildables are in.

		for (AFGBuildable* Member : RawClusterPair.Value)
		{
			if (AFGBuildableFactory* MemberFactory = Cast<AFGBuildableFactory>(Member))
				Cluster->AddToFactory(MemberFactory);
		}

		Cluster->PipeNetworkGroups = PipeNetworkGroups;
		Clusters.Add(Cluster);
	}
}

void UFactoryAnalysisSubsystem::DebugClusters() {
	UE_LOG(LogTemp, Warning, TEXT("[FactoryAnalysis] CLUSTER LOG"));

	// Log results
	UE_LOG(LogTemp, Warning, TEXT("[FactoryAnalysis] %d clusters found."), Clusters.Num());
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
}

UFactoryCluster* UFactoryAnalysisSubsystem::FindClusterFor(AFGBuildable* Buildable) const
{
	if (!Buildable || !FactoryUnionFind.Contains(Buildable))
	{
		return nullptr;
	}
	AFGBuildable* Root = FactoryUnionFind.Find(Buildable);
	if (const TWeakObjectPtr<UFactoryCluster>* Found = BuildableToCluster.Find(Root))
	{
		return Found->Get();
	}
	return nullptr;
}

UFactoryCluster* UFactoryAnalysisSubsystem::FindOrCreateClusterFor(AFGBuildable* Buildable)
{
	FactoryUnionFind.MakeSet(Buildable);

	AFGBuildable* Root = FactoryUnionFind.Find(Buildable);
	if (const TWeakObjectPtr<UFactoryCluster>* Found = BuildableToCluster.Find(Root))
	{
		if (UFactoryCluster* Existing = Found->Get())
		{
			return Existing;
		}
	}

	UFactoryCluster* NewCluster = NewObject<UFactoryCluster>(this);
	NewCluster->RootBuildable = Root;
	BuildableToCluster.Add(Root, NewCluster);
	Clusters.Add(NewCluster);
	return NewCluster;
}

void UFactoryAnalysisSubsystem::HandleBuildableConstructed(AFGBuildable* NewBuildable)
{
	// This works on the assumption that this buildable is only part of 1 cluster (Which if untrue, something is wrong with the union-find)

	if (!NewBuildable)
	{
		UE_LOG(LogTemp, Warning, TEXT("[FactoryAnalysis] HandleBuildableConstructed called with null buildable"));
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("[FactoryAnalysis] HandleBuildableConstructed: %s (%s)"), *NewBuildable->GetName(), *NewBuildable->GetClass()->GetName());

	FactoryUnionFind.MakeSet(NewBuildable);

	// If buildable is a conveyor belt
	if (AFGBuildableConveyorBase* Conveyor = Cast<AFGBuildableConveyorBase>(NewBuildable))
	{
		UE_LOG(LogTemp, Warning, TEXT("[FactoryAnalysis]   -> Processing as conveyor"));
		ProcessConveyor(Conveyor, 0); // We only process one buildable, so index will always be 0. This is a leftover from the work queue system.
	}
	// If buildable is a factory building
	else if (AFGBuildableFactory* Factory = Cast<AFGBuildableFactory>(NewBuildable))
	{
		// If buildable is a container
		if (AFGBuildableStorage* Container = Cast<AFGBuildableStorage>(Factory))
		{
			UE_LOG(LogTemp, Warning, TEXT("[FactoryAnalysis]   -> Processing as container"));
			ProcessContainer(Container, 0); 
		}
		else {
			UE_LOG(LogTemp, Warning, TEXT("[FactoryAnalysis]   -> Processing as factory"));
			ProcessFactory(Factory, 0);
		}
	}
	// Other buildables are skipped to match the initial logic
	else {
		UE_LOG(LogTemp, Warning, TEXT("[FactoryAnalysis]   -> Untracked building type, exiting."));
		return;
	}

	// Union pipes just in case.
	TArray<UFGPipeConnectionComponent*> PipeConns;
	NewBuildable->GetComponents<UFGPipeConnectionComponent>(PipeConns);
	UE_LOG(LogTemp, Warning, TEXT("[FactoryAnalysis]   -> Found %d pipe connections"), PipeConns.Num());
	for (UFGPipeConnectionComponent* Conn : PipeConns)
	{
		const int32 NetworkId = Conn->GetPipeNetworkID();
		if (NetworkId == INDEX_NONE) continue;
		if (TArray<AFGBuildableFactory*>* Members = PipeNetworkGroups.Find(NetworkId))
		{
			UE_LOG(LogTemp, Warning, TEXT("[FactoryAnalysis]   -> Unioning pipe network %d (%d members)"), NetworkId, Members->Num());
			for (int32 i = 0; i < Members->Num(); ++i)
			{
				FactoryUnionFind.MakeSet((*Members)[i]);
				FactoryUnionFind.Union(NewBuildable, (*Members)[i]);
			}
		}
	}

	// With all of this done, get the, now assigned, root of this buildable
	AFGBuildable* Root = FactoryUnionFind.Find(NewBuildable);
	TMap<AFGBuildable*, TArray<AFGBuildable*>> DisjointSetClusters = FactoryUnionFind.GetClusters();
	bool isNew = true;

	UE_LOG(LogTemp, Warning, TEXT("[FactoryAnalysis]   -> Root=%s, Existing clusters=%d, Disjoint sets=%d"),
		Root ? *Root->GetName() : TEXT("null"), Clusters.Num(), DisjointSetClusters.Num());

	// Actions on clusters
	for (UFactoryCluster* Cluster : Clusters) {
		// Will be destroyed
		if (!DisjointSetClusters.Contains(Cluster->RootBuildable.Get())) {
			UE_LOG(LogTemp, Warning, TEXT("[FactoryAnalysis]   -> Removing stale cluster (root=%s)"), *Cluster->GetName());
			Clusters.Remove(Cluster);
		}

		// Will get updated
		if (Cluster->RootBuildable.Get() == Root) {
			isNew = false;

			TArray<AFGBuildable*>* Members = DisjointSetClusters.Find(Root);

			UE_LOG(LogTemp, Warning, TEXT("[FactoryAnalysis]   -> Updating existing cluster (root=%s), %d members"), *Root->GetName(), Members ? Members->Num() : 0);

			Cluster->EmptyLists();

			for (AFGBuildable* Member : *Members)
			{
				if (!IsValid(Member))
					continue;
				if (AFGBuildableFactory* MemberFactory = Cast<AFGBuildableFactory>(Member))
					Cluster->AddToFactory(MemberFactory);
			}
		}

		// Either way, make sure these stay updated
		Cluster->PipeNetworkGroups = PipeNetworkGroups;
	}

	// Is part of a new cluster (this will happen upon two buildables connecting)
	if (isNew)
	{
		TArray<AFGBuildable*>* Members = DisjointSetClusters.Find(Root);

		UE_LOG(LogTemp, Warning, TEXT("[FactoryAnalysis]   -> Creating new cluster (root=%s), %d members"), Root ? *Root->GetName() : TEXT("null"), Members ? Members->Num() : 0);

		UFactoryCluster* Cluster = NewObject<UFactoryCluster>(this);
		Cluster->RootBuildable = Root;
		BuildableToCluster.Add(Root, Cluster); // This will let us look in which cluster buildables are in.

		for (AFGBuildable* Member : *Members)
		{
			if (!IsValid(Member))
				continue;
			if (AFGBuildableFactory* MemberFactory = Cast<AFGBuildableFactory>(Member))
				Cluster->AddToFactory(MemberFactory);
		}

		Cluster->PipeNetworkGroups = PipeNetworkGroups;
		Clusters.Add(Cluster);
	}

	UE_LOG(LogTemp, Warning, TEXT("[FactoryAnalysis]   -> Done. Total clusters now: %d"), Clusters.Num());
}

void UFactoryAnalysisSubsystem::HandleActorsConstructedByPlayer(AFGCharacterPlayer* Player, TArray<AActor*> ConstructedActors)
{
	UE_LOG(LogTemp, Warning, TEXT("[FactoryAnalysis] HandleActorsConstructedByPlayer: %d actors constructed by %s"), ConstructedActors.Num(), Player ? *Player->GetName() : TEXT("null"));

	for (AActor* Actor : ConstructedActors)
	{
		if (AFGBuildable* Buildable = Cast<AFGBuildable>(Actor))
		{
			PendingBuildables.Add(Buildable);
		}
	}
}

void UFactoryAnalysisSubsystem::FlushIncrementalQueue()
{
	if (PendingBuildables.Num() > 0)
	{
		TArray<AFGBuildable*> Items = MoveTemp(PendingBuildables);
		PendingBuildables.Empty();

		IncrementalWorkQueue.Start(
			MoveTemp(Items),
			[this](AFGBuildable* const& Buildable, int32 Index) { HandleBuildableConstructed(Buildable); },
			[this]() { FlushIncrementalQueue(); }
		);
	}
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

	// Incremental work queue: executes after initial scan
	else if (!IncrementalWorkQueue.IsComplete())
	{
		IncrementalWorkQueue.ProcessBudget();
	}
	else if (PendingBuildables.Num() > 0)
	{
		FlushIncrementalQueue();
	}
}