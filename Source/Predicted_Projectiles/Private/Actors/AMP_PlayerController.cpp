// Fill out your copyright notice in the Description page of Project Settings.


#include "Actors/AMP_PlayerController.h"

#include "AMP_BlueprintFunctionLibrary.h"


void AAMP_PlayerController::BeginPlay()
{
	Super::BeginPlay();

	PredictedProjectileManager = UAMP_BlueprintFunctionLibrary::GetAMPManager(GetWorld());
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
	TArray<UAMP_ProjectileMovementComponent*> SpawnedProjectiles;

	// Mark as client predicted
	for (FAMP_ProjectileData& Data : ProjectileData)
	{
		Data.IsClientPredicted = true;
	}
	
	// Spawn Locally
	if (PredictedProjectileManager)
	{
		SpawnedProjectiles = PredictedProjectileManager->SpawnProjectiles(ProjectileData);
	}
	
	ServerRPC_SpawnClientPredictedProjectiles(ProjectileData);
	return SpawnedProjectiles;
}


void AAMP_PlayerController::ServerRPC_SpawnClientPredictedProjectiles_Implementation(const TArray<FAMP_ProjectileData>& ProjectileData)
{
	if (PredictedProjectileManager)
	{
		PredictedProjectileManager->SpawnProjectiles(ProjectileData);
	}
}

bool AAMP_PlayerController::ServerRPC_SpawnClientPredictedProjectiles_Validate(const TArray<FAMP_ProjectileData>& ProjectileData)
{
	return true;
}

void AAMP_PlayerController::ClientRPC_DestroyProjectileSimProxy_Implementation(const FGuid ProjectileID)
{
	if (PredictedProjectileManager)
	{
		PredictedProjectileManager->DestroyProjectileSimProxy(ProjectileID);
	}
}

void AAMP_PlayerController::ClientRPC_SpawnProjectilesSimProxies_Implementation(const TArray<FAMP_ProjectileData>& ProjectileData)
{
	if (PredictedProjectileManager)
	{
		PredictedProjectileManager->SpawnProjectilesSimProxies(ProjectileData);
	}
}
