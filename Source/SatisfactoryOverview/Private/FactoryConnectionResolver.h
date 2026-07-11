

#pragma once

#include "CoreMinimal.h"
#include "FactoryTypes.h"

#include "Buildables/FGBuildableFactory.h"
#include "Buildables/FGBuildableStorage.h"
#include <Buildables/FGBuildablePipeReservoir.h>

/**
 * 
 */
class FactoryConnectionResolver
{
public:
	/** Gets all belt connections **/
	static TArray<FResolvedEndpoint> ResolveBeltConnections(UFGFactoryConnectionComponent* StartConnector, TSet<AFGBuildable*>& Visited);

	/** Gets all pipe connections **/
	static FConnectorResolution ResolvePipeConnections(AFGBuildableFactory* Owner, UFGPipeConnectionComponent* PipeConn, TMap<int32, TArray<AFGBuildableFactory*>> PipeNetworkGroups);

	/** Finds out if a given storage is a boundary */
	static TArray<FResolvedEndpoint> ResolveContainer(AFGBuildableStorage* Container, UFGFactoryConnectionComponent* EntryConnector, TSet<AFGBuildable*>& Visited);

	/** Turns a container or fluid storage into an endpoint */
	static FResolvedEndpoint MakeContainerTerminalEndpoint(AFGBuildableFactory* Container, UFGConnectionComponent* EntryConnector);

	static const bool bTreatContainersAsFactoryEnd = false;
};
