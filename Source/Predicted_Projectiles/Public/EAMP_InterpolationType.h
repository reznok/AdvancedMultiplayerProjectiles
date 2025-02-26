#pragma once
#include "CoreMinimal.h"
#include "EAMP_InterpolationType.generated.h"


UENUM()
enum class EAMP_InterpolationType : uint8
{
	None,
	Snap,
	Smooth
};