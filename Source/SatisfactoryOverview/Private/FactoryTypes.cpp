#include "FactoryTypes.h"
#include "FGFactoryConnectionComponent.h"
#include "FGPipeConnectionComponent.h"

EConnectionDirection FConnectionDirectionMapper::Map(EFactoryConnectionDirection Direction)
{
	switch (Direction)
	{
		case EFactoryConnectionDirection::FCD_INPUT:
			return EConnectionDirection::Input;
		case EFactoryConnectionDirection::FCD_OUTPUT:
			return EConnectionDirection::Output;
		default:
			return EConnectionDirection::Any;
	}
}

EConnectionDirection FConnectionDirectionMapper::Map(EPipeConnectionType Type)
{
	switch (Type)
	{
	case EPipeConnectionType::PCT_CONSUMER:
		return EConnectionDirection::Input;
	case EPipeConnectionType::PCT_PRODUCER:
		return EConnectionDirection::Output;
	default:
		return EConnectionDirection::Any;
	}
}
