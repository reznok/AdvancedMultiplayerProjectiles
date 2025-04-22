#pragma once
#include "CoreMinimal.h"
#include "EAMP_InterpolationType.generated.h"


UENUM(Blueprintable)
enum class EAMP_InterpolationType : uint8
{
	None,
	Snap,
	Smooth
};