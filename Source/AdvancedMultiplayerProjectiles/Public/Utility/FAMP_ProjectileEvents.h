#pragma once

#include "CoreMinimal.h"
#include "Engine/NetSerialization.h"
#include "FAMP_ProjectileEvents.generated.h"

// Why a projectile ended, sent with the server's end message. Codes above Destroyed belong to the game.
namespace AMP_EndCode
{
	// Lifetime ran out, or ended without a game reason.
	inline constexpr uint8 Destroyed = 0;
}

// The server ended a projectile. Client copies (sim proxies by ProjectileID, the firing client's predicted copy by
// PredictionKey) end at Location.
USTRUCT()
struct ADVANCEDMULTIPLAYERPROJECTILES_API FAMP_ProjectileEnd
{
	GENERATED_BODY()

	UPROPERTY()
	FGuid ProjectileID{};

	UPROPERTY()
	int64 PredictionKey{0};

	// Where the server's copy ended.
	UPROPERTY()
	FVector_NetQuantize10 Location{FVector::ZeroVector};

	// AMP_EndCode::Destroyed or a game-defined code.
	UPROPERTY()
	uint8 EndCode{AMP_EndCode::Destroyed};
};

// The server changed a projectile's flight (a knock-back). Client copies are re-placed and catch up from ServerTime.
USTRUCT()
struct ADVANCEDMULTIPLAYERPROJECTILES_API FAMP_ProjectileRedirect
{
	GENERATED_BODY()

	UPROPERTY()
	FGuid ProjectileID{};

	UPROPERTY()
	int64 PredictionKey{0};

	UPROPERTY()
	FVector_NetQuantize10 Location{FVector::ZeroVector};

	UPROPERTY()
	FVector_NetQuantize10 Velocity{FVector::ZeroVector};

	// Server world time (NetworkTimeSync) of the change.
	UPROPERTY()
	float ServerTime{0.f};
};
