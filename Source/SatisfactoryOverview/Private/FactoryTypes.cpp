#include "FactoryTypes.h"
#include "FGFactoryConnectionComponent.h"
#include "FGPipeConnectionComponent.h"

EConnectionDirection FConnectionDirectionMapper::Map(EFactoryConnectionDirection Direction)
{
	EConnectionDirection MappedDirection;

	switch (Direction)
	{
		case EFactoryConnectionDirection::FCD_INPUT:
			MappedDirection = EConnectionDirection::Input;
		case EFactoryConnectionDirection::FCD_OUTPUT:
			MappedDirection = EConnectionDirection::Output;
		default:
			MappedDirection = EConnectionDirection::Any;
	}

	return MappedDirection;
}

EConnectionDirection FConnectionDirectionMapper::Map(EPipeConnectionType Type)
{
	EConnectionDirection MappedDirection;

	switch (Type)
	{
	case EPipeConnectionType::PCT_CONSUMER:
		MappedDirection = EConnectionDirection::Input;
	case EPipeConnectionType::PCT_PRODUCER:
		MappedDirection = EConnectionDirection::Output;
	default:
		MappedDirection = EConnectionDirection::Any;
	}
	
	return MappedDirection;
}
