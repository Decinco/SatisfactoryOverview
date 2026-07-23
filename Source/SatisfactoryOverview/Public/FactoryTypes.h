#pragma once

#include "CoreMinimal.h"
#include "FGFactoryConnectionComponent.h"
#include "FGPipeConnectionComponent.h"
#include "FactoryTypes.generated.h"

class AFGBuildableFactory;
class UFGFactoryConnectionComponent;

UENUM()
enum class EFactoryConnectionKind : uint8
{
	Item,
	Fluid
};

/**
 * Represents the kind of boundary a factory is.
 */
UENUM()
enum class EFactoryBoundaryType : uint8
{
	None,               // not a boundary
	Boundary,           // vehicle station, AWESOME Sink, or Space Elevator
	Generator,          // generators
	DimensionalDepot,   // dimensional depot
	Extractor,          // extractor
	ContainerBoundary   // some containers, excluded from the factory graph
};

/** 
 * Represents directionality for a connector, kind agnostic.
 */
UENUM()
enum class EConnectionDirection : uint8 
{
	Input,
	Output,
	Any
};

/** Maps kind-specific connection direction/role enums onto the kind-agnostic EConnectionDirection. */
struct FConnectionDirectionMapper
{
	static EConnectionDirection Map(EFactoryConnectionDirection Direction);
	static EConnectionDirection Map(EPipeConnectionType Type);
};
 
/**
 * Represents an endpoint for a belt/pipe.
 */
USTRUCT()
struct FResolvedEndpoint
{
	GENERATED_BODY()

	UPROPERTY()
	TWeakObjectPtr<AFGBuildableFactory> Buildable;

	UPROPERTY()
	EConnectionDirection EndpointDirection = EConnectionDirection::Any;

	/** None for a normal machine endpoint; otherwise the kind of terminal this endpoint stopped at (§2). */
	UPROPERTY()
	EFactoryBoundaryType BoundType = EFactoryBoundaryType::None;

	EFactoryConnectionKind Kind = EFactoryConnectionKind::Item;

	bool operator==(const FResolvedEndpoint& Other) const
	{
		return Buildable == Other.Buildable
			&& EndpointDirection == Other.EndpointDirection
			&& BoundType == Other.BoundType;
	}
};

/**
 * Returned struct representing items involved in a factory and their states.
 */
USTRUCT(BlueprintType)
struct FItemBalance
{
	GENERATED_BODY()

	// Manufacturers
	UPROPERTY()
	TMap<TSubclassOf<class UFGItemDescriptor>, float> Export;

	UPROPERTY()
	TMap<TSubclassOf<class UFGItemDescriptor>, float> Import;

	UPROPERTY()
	TMap<TSubclassOf<class UFGItemDescriptor>, float> Surplus;

	UPROPERTY()
	TMap<TSubclassOf<class UFGItemDescriptor>, float> Deficit;

	// Produced by producers
	UPROPERTY()
	TMap<TSubclassOf<class UFGItemDescriptor>, float> Produced;

	// Consumed by consumers
	UPROPERTY()
	TMap<TSubclassOf<class UFGItemDescriptor>, float> Consumed;
};
 
/**
 * Represents an connection between two factories. IGNORE CONTAINERS, this explicitly exists to calculate the production lines themselves.
 */
USTRUCT()
struct FConnectorResolution
{
	GENERATED_BODY()

	/** One of a manufacturer's own factory connectors (input or output). */
	UPROPERTY()
	TObjectPtr<UFGFactoryConnectionComponent> SourceConnector;

	/** One of a manufacturer's own pipe connectors (input or output). */
	UPROPERTY()
	TObjectPtr<UFGPipeConnectionComponent> SourcePipeConnector;

	UPROPERTY()
	EConnectionDirection SourceDirection = EConnectionDirection::Any;

	/** What SourceConnector resolves to. Empty means dead-ended or unconnected. */
	UPROPERTY()
	TArray<FResolvedEndpoint> Endpoints;

	UPROPERTY()
	EFactoryConnectionKind Kind = EFactoryConnectionKind::Item;

	UPROPERTY()
	TWeakObjectPtr<AFGBuildableFactory> SourceOwner;
};
