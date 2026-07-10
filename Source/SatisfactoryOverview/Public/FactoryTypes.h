#pragma once

#include "CoreMinimal.h"
#include "FGFactoryConnectionComponent.h" // for EFactoryConnectionDirection
#include "FactoryTypes.generated.h"

class AFGBuildableFactory;
class UFGFactoryConnectionComponent;
 
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
