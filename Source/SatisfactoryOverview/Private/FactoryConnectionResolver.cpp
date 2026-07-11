#include "FactoryConnectionResolver.h"

#include <FactoryCluster.h>
#include <FGFactoryConnectionComponent.h>
#include <Buildables/FGBuildableManufacturer.h>
#include <Buildables/FGBuildableConveyorAttachment.h>
#include <FGPipeConnectionComponent.h>
#include <FGFactoryConnectionComponent.h>
#include <Buildables/FGBuildableConveyorBase.h>


TArray <FResolvedEndpoint> FactoryConnectionResolver::ResolveBeltConnections(UFGFactoryConnectionComponent* StartConnector, TSet<AFGBuildable*>& Visited)
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
			Endpoint.EndpointDirection = FConnectionDirectionMapper::Map(Current->GetDirection());
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
				Endpoint.EndpointDirection = FConnectionDirectionMapper::Map(Current->GetDirection());
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
					Result.Append(ResolveBeltConnections(Conn->GetConnection(), Visited));
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
					Result.Append(ResolveBeltConnections(Connection->GetConnection(), Visited));
			return Result;
		}
		break;
	}
	return Result;
}

TArray<FResolvedEndpoint> FactoryConnectionResolver::ResolveContainer(AFGBuildableStorage* Container, UFGFactoryConnectionComponent* EntryConnector, TSet<AFGBuildable*>& Visited)
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
			PassThroughResult.Append(ResolveBeltConnections(Conn->GetConnection(), Visited));
	}

	// Condition 3: if the next found machine is also a boundary. (Deactivated)
	/* const bool bDownstreamIsTerminal = PassThroughResult.ContainsByPredicate([](const FResolvedEndpoint& Endpoint)
		{
			return Endpoint.BoundType != EFactoryBoundaryType::None;
		});
	if (bDownstreamIsTerminal)
	{
		return { MakeContainerTerminalEndpoint(Container, EntryConnector) };
	}
	*/


	return PassThroughResult;
}

FConnectorResolution FactoryConnectionResolver::ResolvePipeConnections(AFGBuildableFactory* Owner, UFGPipeConnectionComponent* PipeConn, TMap<int32, TArray<AFGBuildableFactory*>> PipeNetworkGroups)
{
	FConnectorResolution Resolution;
	Resolution.Kind = EFactoryConnectionKind::Fluid;
	Resolution.SourcePipeConnector = PipeConn;
	Resolution.SourceOwner = Owner;
	Resolution.SourceDirection = FConnectionDirectionMapper::Map(PipeConn->GetPipeConnectionType());

	FResolvedEndpoint Endpoint;

	const int32 NetworkId = PipeConn->GetPipeNetworkID();
	if (NetworkId == INDEX_NONE)
	{
		return Resolution; // unconnected -- empty Endpoints, same as an unconnected belt connector
	}

	const TArray<AFGBuildableFactory*>* NetworkMembers = PipeNetworkGroups.Find(NetworkId);
	if (!NetworkMembers)
	{
		return Resolution;
	}

	// No traversal needed for pipes
	for (AFGBuildableFactory* Peer : *NetworkMembers)
	{
		if (!Peer || Peer == Owner)
		{
			continue;
		}

		TArray<UFGPipeConnectionComponent*> PeerPipeConnections;
		Peer->GetComponents<UFGPipeConnectionComponent>(PeerPipeConnections);
		for (UFGPipeConnectionComponent* PeerConn : PeerPipeConnections)
		{
			if (!PeerConn || PeerConn->GetPipeNetworkID() != NetworkId)
			{
				continue;
			}
			Endpoint.Buildable = Peer;
			Endpoint.EndpointDirection = FConnectionDirectionMapper::Map(PeerConn->GetPipeConnectionType());
			Endpoint.BoundType = UFactoryCluster::ClassifyTerminal(Peer);
			Resolution.Endpoints.Add(Endpoint);
		}
	}

	return Resolution;
}

FResolvedEndpoint FactoryConnectionResolver::MakeContainerTerminalEndpoint(AFGBuildableFactory* Container, UFGConnectionComponent* EntryConnector)
{
	EConnectionDirection EntryConnectorDirection;

	if (UFGPipeConnectionComponent* EntryPipe = Cast<UFGPipeConnectionComponent>(EntryConnector)) {
		EntryConnectorDirection = FConnectionDirectionMapper::Map(EntryPipe->GetPipeConnectionType());
	}
	else if (UFGFactoryConnectionComponent* EntryBelt = Cast<UFGFactoryConnectionComponent>(EntryConnector)) {
		EntryConnectorDirection = FConnectionDirectionMapper::Map(EntryBelt->GetDirection());
	}
	else {
		EntryConnectorDirection = EConnectionDirection::Any;
	}

	FResolvedEndpoint Endpoint;
	Endpoint.Buildable = Cast<AFGBuildableFactory>(Container);
	Endpoint.EndpointDirection = EntryConnectorDirection;
	Endpoint.BoundType = EFactoryBoundaryType::ContainerBoundary;
	return Endpoint;
}
