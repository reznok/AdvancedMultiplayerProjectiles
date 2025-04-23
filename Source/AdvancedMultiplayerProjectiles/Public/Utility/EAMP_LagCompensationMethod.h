#pragma once
#include "CoreMinimal.h"
#include "EAMP_LagCompensationMethod.generated.h"


UENUM()
enum class EAMP_LagCompensationMethod : uint8
{
	// Use player's ping to determine projectile delay. All ping calcs are done on the server.
	// Slightly less accurate, but a lot harder to manipulate for cheating clients
	// PlayerPing,

	// Use the server's ping to determine projectile delay. Client and Server sends timestamps for projectiles.
	// More accurate, but easier to manipulate for cheating clients
	NetworkTimeSync
};