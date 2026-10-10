#pragma once
#include "CoreMinimal.h"
#include "EAMP_InterpolationType.h"
#include "FAMP_ProjectileData.generated.h"

USTRUCT()
struct FFloatVector{
	GENERATED_BODY()
	
	UPROPERTY()
	float X{0.f};
	
	UPROPERTY()
	float Y{0.f};
	
	UPROPERTY()
	float Z{0.f};
};

/**
 * 
 */
USTRUCT(Blueprintable)
struct ADVANCEDMULTIPLAYERPROJECTILES_API FAMP_ProjectileData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TObjectPtr<AActor> Owner{nullptr};
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	UClass* ProjectileClass{nullptr};

	// Type of Interpolation to use for the projectile
	// None: No interpolation or corrections will happen
	// Snap: The projectile will immediately snap to the server state, skipping intermediate steps but still sweeping
	// Smooth: The projectile will smoothly interpolate to the server state
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	EAMP_InterpolationType InterpolationType{EAMP_InterpolationType::None};

	// How far ahead the projectile should simulate on clients
	// 1 means it will simulate 100% of the RTT ahead of the server, allowing the client to dodge projectiles. However, other clients are likely to
	// incorrectly see the projectile hit a player that dodged it.
	// 0 means it will simulate exactly where the server tells it to. This will result in a player getting hit even though on their screen they dodged it,
	// but other players will see accurate hits.
	// Summary: Higher feels better for defender, lower feels better for attacker
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float ForwardPredictionScale{1.0f};

	// 72 Bits
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FVector_NetQuantize10 Location{FVector::ZeroVector};

	// 72 Bits
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FVector_NetQuantize10 Rotation{FVector::ZeroVector};

	// 72 Bits
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FVector_NetQuantize10 Scale{FVector::OneVector};

	// 72 Bits
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FVector_NetQuantize10 Velocity{FVector::ZeroVector};

	// How long in seconds projectile is alive. 0 means infinite
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float LifeTime{0};

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bShowOnClient{false};

	// Set by clients, should not be replicated
	float SimulationDelay{0.f};

	// The delay from the client firing the projectile to the server spawning it (PlayerPing)
	UPROPERTY()
	float FiringClientDelay{0.f};

	// The time the projectile was spawned on the client (NetworkTimeSync)
	UPROPERTY()
	float ClientProjectileSpawnTime{0.f};

	// The time the projectile was spawned on the server (NetworkTimeSync)
	UPROPERTY()
	float ServerProjectileSpawnTime{0.f};

	UPROPERTY()
	FGuid ProjectileID{};
	
	// Whether a client predicts firing this projectile or not
	UPROPERTY()
	bool IsClientPredicted{false};

	// Whether the projectile was spawned by the server or not
	UPROPERTY()
	bool bIsServerSpawned{false};

	// Used for local client prediction
	// Not replicated
	bool bIsLocallyFired{false};

	// Optional caller key shared by the two copies of one client-predicted shot: the firing client's local copy
	// (AAMP_AMPManager::SpawnLocalPredictedProjectiles) and the server's authoritative copy. The server's end and
	// redirect messages carry it, so the firing client's copy ends and turns with the server's. 0 = none. Must be unique
	// per shot across all players.
	UPROPERTY()
	int64 PredictionKey{0};

	// Game-defined value carried to every copy with the spawn (e.g. a target id)
	UPROPERTY()
	int32 Payload{0};

	inline bool operator==(const FAMP_ProjectileData& Other) const
	{
		return ProjectileID == Other.ProjectileID;
	}
};