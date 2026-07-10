#pragma once

#include "CoreMinimal.h"
#include "FGFactoryConnectionComponent.h" // for EFactoryConnectionDirection
#include "FactoryTypes.generated.h"

class AFGBuildableFactory;
class UFGFactoryConnectionComponent;

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
 * Represents an endpoint for a belt/pipe.
 */
USTRUCT()
struct FResolvedEndpoint
{
	GENERATED_BODY()

	UPROPERTY()
	TWeakObjectPtr<AFGBuildableFactory> Buildable;

	UPROPERTY()
	EFactoryConnectionDirection EndpointDirection = EFactoryConnectionDirection::FCD_ANY;

	/** None for a normal machine endpoint; otherwise the kind of terminal this endpoint stopped at (§2). */
	UPROPERTY()
	EFactoryBoundaryType BoundType = EFactoryBoundaryType::None;

	bool operator==(const FResolvedEndpoint& Other) const
	{
		return Buildable == Other.Buildable
			&& EndpointDirection == Other.EndpointDirection
			&& BoundType == Other.BoundType;
	}
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

	UPROPERTY()
	EFactoryConnectionDirection SourceDirection = EFactoryConnectionDirection::FCD_ANY;

	/** What SourceConnector resolves to. Empty means dead-ended or unconnected. */
	UPROPERTY()
	TArray<FResolvedEndpoint> Endpoints;
};
