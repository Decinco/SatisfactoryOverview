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
#include "Buildables/FGBuildableGeneratorFuel.h"

// Nuclear fuel descriptor (waste class lookup)
#include "Resources/FGItemDescriptorNuclearFuel.h"
#include "Resources/FGItemDescriptor.h"

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
	// UnclassifiedMembers (e.g. containers) must also be absent.
	return Manufacturers.IsEmpty() && Consumers.IsEmpty() && ProducerConsumers.IsEmpty()
		&& UnclassifiedMembers.IsEmpty()
		&& !Bounds.IsEmpty() && !Producers.IsEmpty();
}

bool UFactoryCluster::IsFuelStation() const
{
	bool bIsFuelStation = true;

	// Check conditions are valid. Will not check an absurdly big amount.
	if (Bounds.Num() > 0 && GetNumListsNotEmpty() == 1 && GetValidMembers().Num() <= 25) {
		for (TWeakObjectPtr<AFGBuildableFactory> WeakBound : Bounds) {
			AFGBuildableFactory* Factory = WeakBound.Get();

			if (!Cast<AFGBuildableDockingStation>(Factory) && !Cast<AFGBuildableDroneStation>(Factory)) {
				bIsFuelStation = false;
			}
		}
	}
	else {
		bIsFuelStation = false;
	}

	return bIsFuelStation;
}

TMap<TSubclassOf<UFGItemDescriptor>, FItemRate> UFactoryCluster::FindProductionConsumption(AFGBuildableFactory* Factory) const 
{
	TMap<TSubclassOf<UFGItemDescriptor>, FItemRate> ProductionConsumption;

	auto ConvertRecipeAmount = [](TSubclassOf<UFGItemDescriptor> Item, float Amount) -> float
	{
		const EResourceForm Form = UFGItemDescriptor::GetForm(Item);
		return (Form == EResourceForm::RF_LIQUID || Form == EResourceForm::RF_GAS) ? Amount / 1000.f : Amount;
	};

	auto AddProduced = [&ProductionConsumption](TSubclassOf<UFGItemDescriptor> Item, float Rate)
	{
		if (Item && Rate > 0.f)
			ProductionConsumption.FindOrAdd(Item).ProducedRate += Rate;
	};

	auto AddConsumed = [&ProductionConsumption](TSubclassOf<UFGItemDescriptor> Item, float Rate)
	{
		if (Item && Rate > 0.f)
			ProductionConsumption.FindOrAdd(Item).ConsumedRate += Rate;
	};

	// Find out via recipe
	if (AFGBuildableManufacturer* Manufacturer = Cast<AFGBuildableManufacturer>(Factory))
	{
		const float CycleTime = Manufacturer->GetProductionCycleTime();
		if (CycleTime <= 0.f) return ProductionConsumption;

		const float CyclesPerMinute = 60.f / CycleTime;
		const float ProducedMultiplier = CyclesPerMinute * Manufacturer->GetCurrentProductionBoost();
		const float ConsumedMultiplier = CyclesPerMinute;

		TSubclassOf<UFGRecipe> Recipe = Manufacturer->GetCurrentRecipe();
		for (const FItemAmount& Product : UFGRecipe::GetProducts(Recipe)) 
		{
			AddProduced(Product.ItemClass, ConvertRecipeAmount(Product.ItemClass, Product.Amount) * ProducedMultiplier);
		}
		for (const FItemAmount& Ingredient : UFGRecipe::GetIngredients(this, Recipe)) 
		{
			AddConsumed(Ingredient.ItemClass, ConvertRecipeAmount(Ingredient.ItemClass, Ingredient.Amount) * ConsumedMultiplier);
		}

		return ProductionConsumption;
	}

	// Find out via current node
	if (AFGBuildableResourceExtractor* Producer = Cast<AFGBuildableResourceExtractor>(Factory))
	{
		const TScriptInterface<IFGExtractableResourceInterface>& Extractable = Producer->GetExtractableResource();
		if (Extractable)
		{
			const TSubclassOf<UFGItemDescriptor> ResourceClass(Extractable->GetResourceClass());
			const float Rate = Producer->GetExtractionPerMinute();

			AddProduced(ResourceClass, Rate); // Miners also, for some reason, divide by 1000
		}
		return ProductionConsumption;
	}

	// Find out via current fuel source
	if (AFGBuildableGeneratorNuclear* NEPlant = Cast<AFGBuildableGeneratorNuclear>(Factory))
	{
		const TSubclassOf<UFGItemDescriptor> FuelClass = NEPlant->GetCurrentFuelClass();
		if (FuelClass)
		{
			const float PowerCapacity = NEPlant->GetPowerProductionCapacity();
			const float EnergyValue = UFGItemDescriptor::GetEnergyValue(FuelClass);
			if (EnergyValue > 0.f)
			{
				const float FuelRate = 60.f * PowerCapacity / EnergyValue;
				const float FixedRate = ConvertRecipeAmount(FuelClass, FuelRate);
				AddConsumed(FuelClass, FixedRate);

				if (FuelClass->IsChildOf(UFGItemDescriptorNuclearFuel::StaticClass()))
				{
					const TSubclassOf<UFGItemDescriptorNuclearFuel> NuclearFuelClass = Cast<UClass>(FuelClass);
					const TSubclassOf<UFGItemDescriptor> WasteClass = UFGItemDescriptorNuclearFuel::GetSpentFuelClass(NuclearFuelClass);
					const int32 WastePerCycle = UFGItemDescriptorNuclearFuel::GetAmountWasteCreated(NuclearFuelClass);
					if (WasteClass && WastePerCycle > 0)
					{
						const float WasteFixedRate = ConvertRecipeAmount(WasteClass, FuelRate * WastePerCycle);
						AddProduced(WasteClass, WasteFixedRate);
					}
				}
			}
		}

		if (NEPlant->GetRequiresSupplementalResource())
		{
			const TSubclassOf<UFGItemDescriptor> SupplementalClass = NEPlant->GetSupplementalResourceClass();
			const float SupplementalRate = NEPlant->GetSupplementalConsumptionRateMaximum() * 60.f;
			AddConsumed(SupplementalClass, SupplementalRate); // For some reason, supplemental is already correctly divided
		}

		return ProductionConsumption;
	}

	// Find out via current fuel source
	if (AFGBuildableGeneratorFuel* Generator = Cast<AFGBuildableGeneratorFuel>(Factory))
	{
		const TSubclassOf<UFGItemDescriptor> FuelClass = Generator->GetCurrentFuelClass();
		if (FuelClass)
		{
			const float PowerCapacity = Generator->GetPowerProductionCapacity();
			const float EnergyValue = UFGItemDescriptor::GetEnergyValue(FuelClass);
			const float FuelRate = 60.f * PowerCapacity / EnergyValue;
			const float FixedRate = ConvertRecipeAmount(FuelClass, FuelRate);
			if (EnergyValue > 0.f)
				AddConsumed(FuelClass, FixedRate);
		}

		if (Generator->GetRequiresSupplementalResource())
		{
			const TSubclassOf<UFGItemDescriptor> SupplementalClass = Generator->GetSupplementalResourceClass();
			const float SupplementalRate = Generator->GetSupplementalConsumptionRateMaximum() * 60.f;
			AddConsumed(SupplementalClass, SupplementalRate); // For some reason, supplemental is already correctly divided
		}

		return ProductionConsumption;
	}

	// Find out via custom accessors
	if (AFGBuildablePowerBooster* APA = Cast<AFGBuildablePowerBooster>(Factory))
	{
		const TSubclassOf<UFGItemDescriptor> FuelClass = APA->GetmCurrentFuelClass();
		const float FuelDuration = APA->GetmCurrentFuelDuration();
		if (FuelClass && FuelDuration > 0.f)
			AddConsumed(FuelClass, 60.f / FuelDuration);
		return ProductionConsumption;
	}

	// Find out via custom accessors
	if (AFGBuildablePortal* Portal = Cast<AFGBuildablePortal>(Factory))
	{
		const TSubclassOf<UFGItemDescriptor> FuelClass = Portal->GetmFuelItemClass();
		const float CycleTime = Portal->GetDefaultProductionCycleTime();
		if (FuelClass && CycleTime > 0.f)
			AddConsumed(FuelClass, 60.f / CycleTime);
		return ProductionConsumption;
	}

	return ProductionConsumption;
};

FItemBalance UFactoryCluster::ComputeItemBalanceSheet(bool bInFlagOverflowAsInefficient) const
{
	bool bFindOutBoundConnections = false; // i'm tired and I want to have an actual first draft ngl

	FItemBalance Result;

	// Find out items in Manufacturers' recipes
	TMap<TSubclassOf<UFGItemDescriptor>, FItemRate> InternalFactory;
	TMap<TSubclassOf<UFGItemDescriptor>, FItemRate> Consumption;
	TMap<TSubclassOf<UFGItemDescriptor>, FItemRate> Production;

	for (const TWeakObjectPtr<AFGBuildableFactory>& WeakManufacturer : Manufacturers)
	{
		// Get manufacturer
		AFGBuildableFactory* Manufacturer = WeakManufacturer.Get();
		if (!Manufacturer) continue;

		TMap<TSubclassOf<UFGItemDescriptor>, FItemRate> ManufacturerSheet = FindProductionConsumption(Manufacturer);

		for (TPair<TSubclassOf<UFGItemDescriptor>, FItemRate> ManufacturerSheetPair : ManufacturerSheet) {
			InternalFactory.FindOrAdd(ManufacturerSheetPair.Key).ProducedRate += ManufacturerSheetPair.Value.ProducedRate;
			InternalFactory.FindOrAdd(ManufacturerSheetPair.Key).ConsumedRate += ManufacturerSheetPair.Value.ConsumedRate;
		}
	}

	// Get ProducerConsumers' production
	for (const TWeakObjectPtr<AFGBuildableFactory>& WeakProducerConsumer : ProducerConsumers)
	{
		// Get producer consumer
		AFGBuildableFactory* ProducerConsumer = WeakProducerConsumer.Get();
		if (!ProducerConsumer) continue;

		TMap<TSubclassOf<UFGItemDescriptor>, FItemRate> ProducerConsumerSheet = FindProductionConsumption(ProducerConsumer);

		for (TPair<TSubclassOf<UFGItemDescriptor>, FItemRate> ProducerConsumerSheetPair : ProducerConsumerSheet) {
			InternalFactory.FindOrAdd(ProducerConsumerSheetPair.Key).ProducedRate += ProducerConsumerSheetPair.Value.ProducedRate;
			Consumption.FindOrAdd(ProducerConsumerSheetPair.Key).ConsumedRate += ProducerConsumerSheetPair.Value.ConsumedRate;
		}
	}

	// Get Consumers' consumption
	for (const TWeakObjectPtr<AFGBuildableFactory>& WeakConsumer : Consumers)
	{
		// Get consumer
		AFGBuildableFactory* Consumer = WeakConsumer.Get();
		if (!Consumer) continue;

		TMap<TSubclassOf<UFGItemDescriptor>, FItemRate> ConsumerSheet = FindProductionConsumption(Consumer);

		for (TPair<TSubclassOf<UFGItemDescriptor>, FItemRate> ConsumerSheetPair : ConsumerSheet) {
			Consumption.FindOrAdd(ConsumerSheetPair.Key).ConsumedRate += ConsumerSheetPair.Value.ConsumedRate;
		}
	}

	// Get Producers' production
	for (const TWeakObjectPtr<AFGBuildableFactory>& WeakProducer : Producers)
	{
		// Get consumer
		AFGBuildableFactory* Producer = WeakProducer.Get();
		if (!Producer) continue;

		TMap<TSubclassOf<UFGItemDescriptor>, FItemRate> ProducerSheet = FindProductionConsumption(Producer);

		for (TPair<TSubclassOf<UFGItemDescriptor>, FItemRate> ProducerSheetPair : ProducerSheet) {
			Production.FindOrAdd(ProducerSheetPair.Key).ProducedRate += ProducerSheetPair.Value.ProducedRate;
		}
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
	for (const TPair<TSubclassOf<UFGItemDescriptor>, FItemRate>& Pair : InternalFactory)
	{
		const TSubclassOf<UFGItemDescriptor>& Item = Pair.Key;
		const float NetRate = Pair.Value.ProducedRate - Pair.Value.ConsumedRate;

		if (NetRate > KINDA_SMALL_NUMBER)
		{
			if (!bFindOutBoundConnections || ExportableViaBoundary.Contains(Item))
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
			if (!bFindOutBoundConnections || ImportableViaBoundary.Contains(Item))
			{
				Result.Import.Add(Item, -NetRate);
			}
			else
			{
				Result.Deficit.Add(Item, -NetRate);
			}
		}
	}

	for (const TPair<TSubclassOf<UFGItemDescriptor>, FItemRate>& Pair : Consumption) {
		const TSubclassOf<UFGItemDescriptor>& Item = Pair.Key;
		const float Rate = Pair.Value.ConsumedRate;

		Result.Consumed.Add(Item, Rate);
	}

	for (const TPair<TSubclassOf<UFGItemDescriptor>, FItemRate>& Pair : Production) {
		const TSubclassOf<UFGItemDescriptor>& Item = Pair.Key;
		const float Rate = Pair.Value.ProducedRate;

		Result.Produced.Add(Item, Rate);
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

TArray<UFactoryCluster*> UFactoryCluster::GetAllValidClusters(const TArray<UFactoryCluster*>& ClusterList) 
{
	TArray<UFactoryCluster*> FilteredClusterList;

	for (UFactoryCluster* Cluster : ClusterList)
	{
		if (!Cluster) continue;

		// Space elevator bypasses filtering
		if (Cluster->bHasSpaceElevator) {
			FilteredClusterList.Add(Cluster);
			continue;
		}
		// Fuel stations also bypass filtering
		else if (Cluster->IsFuelStation()) {
			FilteredClusterList.Add(Cluster);
			continue;
		}

		// Invalid. It cannot ever produce anything.
		if (Cluster->GetValidMembers().Num() <= 1) {
			continue;
		}
		// Producers, consumers, producerconsumers and bounds leading nowhere. They don't, can't do anything.
		else if (Cluster->Manufacturers.Num() <= 0 && Cluster->GetNumListsNotEmpty() <= 1) {
			continue;
		}

		FilteredClusterList.Add(Cluster);
	}

	return FilteredClusterList;
}

int UFactoryCluster::GetNumListsNotEmpty() const {
	int listsNotEmpty = 0;

	if (Manufacturers.Num() > 0) listsNotEmpty++;
	if (Producers.Num() > 0) listsNotEmpty++;
	if (Consumers.Num() > 0) listsNotEmpty++;
	if (ProducerConsumers.Num() > 0) listsNotEmpty++;
	if (Bounds.Num() > 0) listsNotEmpty++;

	return listsNotEmpty;
}

void UFactoryCluster::AddToFactory(AFGBuildableFactory* Buildable) {
	if (!Buildable) return;

	if (Cast<AFGBuildableManufacturer>(Buildable)) {
		Manufacturers.Add(Buildable);
	}
	else if (Cast<AFGBuildableResourceExtractor>(Buildable)) {
		Producers.Add(Buildable);
	}
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
		|| Cast<AFGCentralStorageContainer>(Buildable)
		|| Cast<AFGBuildablePipeReservoir>(Buildable)
		|| Cast<AFGBuildableResourceSink>(Buildable)) {
		Bounds.Add(Buildable);
	}
	else if (Cast<AFGBuildableSpaceElevator>(Buildable)) {
		Bounds.Add(Buildable);
		bHasSpaceElevator = true;
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