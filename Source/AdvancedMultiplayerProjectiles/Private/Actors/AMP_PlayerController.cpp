// Fill out your copyright notice in the Description page of Project Settings.


#include "Actors/AMP_PlayerController.h"

#include "AMP_BlueprintFunctionLibrary.h"
#include "NetworkTimeSyncComponent.h"


AAMP_PlayerController::AAMP_PlayerController(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	NetworkTimeSyncComponent = CreateDefaultSubobject<UNetworkTimeSyncComponent>(TEXT("NetworkTimeSyncComponent"));
}

void AAMP_PlayerController::BeginPlay()
{
	Super::BeginPlay();

	ProjectileManager = UAMP_BlueprintFunctionLibrary::GetAMPManager(GetWorld());
}

UAMP_ProjectileMovementComponent* AAMP_PlayerController::SpawnClientPredictedProjectile(FAMP_ProjectileData ProjectileData)
{
	TArray<FAMP_ProjectileData> ProjectileDataArray = {ProjectileData};

	TArray<UAMP_ProjectileMovementComponent*> NewlySpawnedProjectiles = SpawnClientPredictedProjectiles(ProjectileDataArray);
	if (NewlySpawnedProjectiles.Num() > 0)
	{
		return NewlySpawnedProjectiles[0];
	}
	return nullptr;
}

TArray<UAMP_ProjectileMovementComponent*> AAMP_PlayerController::SpawnClientPredictedProjectiles(TArray<FAMP_ProjectileData>& ProjectileData)
{
	if (!ProjectileManager)
	{
		UE_LOG(LogAMP, Error, TEXT("AAMP_PlayerController::SpawnClientPredictedProjectiles: PredictedProjectileManager is nullptr"));
		return {};
	}

	// Mark as client predicted
	for (FAMP_ProjectileData& Data : ProjectileData)
	{
		// Prevent listen server from marking as clientpredicted
		Data.IsClientPredicted = !HasAuthority();
		
		Data.ClientProjectileSpawnTime = ProjectileManager->GetNetworkTimeSubsystem()->GetServerWorldTime();
		Data.bIsLocallyFired = true;
		
	}
	
	// Spawn Locally
	TArray<UAMP_ProjectileMovementComponent*> SpawnedProjectiles = ProjectileManager->SpawnProjectiles(ProjectileData);

	// Call RPC to spawn on server
	// Don't call from listen server local player
	if (!HasAuthority())
	{
		ServerRPC_SpawnClientPredictedProjectiles(ProjectileData);
	}
	return SpawnedProjectiles; 
}


void AAMP_PlayerController::ServerRPC_SpawnClientPredictedProjectiles_Implementation(const TArray<FAMP_ProjectileData>& ProjectileData)
{
	if (ProjectileManager)
	{
		ProjectileManager->SpawnProjectiles(ProjectileData);
	}
}

bool AAMP_PlayerController::ServerRPC_SpawnClientPredictedProjectiles_Validate(const TArray<FAMP_ProjectileData>& ProjectileData)
{
	return true;
}

void AAMP_PlayerController::ClientRPC_DestroyProjectileSimProxy_Implementation(const FGuid ProjectileID)
{
	if (ProjectileManager)
	{
		ProjectileManager->DestroyProjectileSimProxy(ProjectileID);
	}
}

void AAMP_PlayerController::ClientRPC_SpawnProjectilesSimProxies_Implementation(const TArray<FAMP_ProjectileData>& ProjectileData)
{
	if (ProjectileManager)
	{
		ProjectileManager->SpawnProjectilesSimProxies(ProjectileData);
	}
}
