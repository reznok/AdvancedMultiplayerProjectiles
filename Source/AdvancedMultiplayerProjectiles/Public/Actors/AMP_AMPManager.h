// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "NetworkTimeSubsystem.h"
#include "GameFramework/Actor.h"
#include "Utility/FAMP_ProjectileData.h"
#include "Actors/AMP_PlayerController.h"
#include "Utility/EAMP_LagCompensationMethod.h"
#include "Utility/FAMP_ProjectilePool.h"
#include "AMP_AMPManager.generated.h"

class UNetworkTimeSyncComponent;
class AAMP_PlayerController;
class UAMP_ProjectileMovementComponent;

UCLASS()
class ADVANCEDMULTIPLAYERPROJECTILES_API AAMP_AMPManager : public AInfo
{
	GENERATED_BODY()

public:
	// Sets default values for this actor's properties
	AAMP_AMPManager();

	// Whether to show the server spawned projectile on the predicting client
	// Only use for debugging
	UPROPERTY(EditAnywhere)
	bool bShowServerProjectile{false};

	// Whether to use a pool of projectiles
	UPROPERTY(EditAnywhere)
	bool bUseProjectilePool{true};

	UPROPERTY(EditAnywhere)
	TMap<UClass*, int32> StartingPoolSizes;

	// Whether to use net relevancy for projectiles
	// Requires players to have an AMP_PlayerController
	UPROPERTY(EditAnywhere)
	bool bUseAMP_NetRelevancy{true};
	
	UPROPERTY(EditAnywhere)
	float AMP_NetRelevancyRange{10000.f};

	UPROPERTY(EditAnywhere)
	EAMP_LagCompensationMethod LagCompensationMethod{EAMP_LagCompensationMethod::NetworkTimeSync};

	// Spawn a single projectile
	UFUNCTION(BlueprintCallable)
	virtual UAMP_ProjectileMovementComponent* SpawnProjectile(FAMP_ProjectileData ProjectileData);

	// Spawn multiple projectiles at once
	UFUNCTION(BlueprintCallable)
	virtual TArray<UAMP_ProjectileMovementComponent*> SpawnProjectiles(const TArray<FAMP_ProjectileData> ProjectilesData);

	// Tell sim proxies to spawn projectile
	UFUNCTION(NetMulticast, UnReliable)
	virtual void MultiCast_SpawnProjectilesSimProxies(const TArray<FAMP_ProjectileData>& ProjectileData);

	// Runs on server, finds all PlayerControllers near queued projectiles and tells them to spawn the projectile
	virtual void NetRelevant_SpawnProjectilesSimProxies(const TArray<FAMP_ProjectileData>& ProjectileData);
	
	// Runs on client
	virtual void SpawnProjectilesSimProxies(const TArray<FAMP_ProjectileData>& ProjectileData);

	// Tell sim proxies to destroy a spawned projectile
	UFUNCTION(NetMulticast, UnReliable)
	virtual void MultiCast_DestroyProjectileSimProxy(const FGuid ProjectileID);

	// Runs on server, finds all PlayerControllers near destroyed projectiles and tells them to destroy the projectiles
	virtual void NetRelevant_DestroyProjectileSimProxy(const FGuid ProjectileID);

	// Runs on client
	virtual void DestroyProjectileSimProxy(const FGuid ProjectileID);

	// Server entry point for destroying projectiles
	void DestroyProjectile(UAMP_ProjectileMovementComponent* DestroyedProjectile);

	TObjectPtr<UNetworkTimeSubsystem> GetNetworkTimeSubsystem() const { return NetworkTimeSubsystem; }
	
protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

	// Queued projectiles to be spawned on the client after a delay
	// The float is the simulation delay
	TArray<TTuple<float, FAMP_ProjectileData>> QueuedProjectiles;
	
	// Queue that get spawned after their simulation delay
	virtual void QueueProjectile(FAMP_ProjectileData& ProjectileData);

	// Process the queued projectiles, firing if the delay has expired
	virtual void ProcessQueuedProjectiles(float DeltaTime);

	// Process the server spawned and destroyed projectiles buffers
	virtual void ProcessServerProjectilesBuffers(float DeltaTime);

	virtual void SetProjectileDataDefaults(FAMP_ProjectileData& ProjectileData);
	
	// Remove projectile from SpawnedProjectiles
	void RemoveProjectile(const FGuid& ProjectileID) { SpawnedProjectiles.Remove(ProjectileID);};
	
	virtual TObjectPtr<UAMP_ProjectileMovementComponent> FireProjectile(FAMP_ProjectileData& ProjectileData);

	// Check if the player controller is relevant for the projectile
	// Even for Destroying, it's fine to use ProjectileData's spawn location
	virtual bool IsPlayerControllerRelevantForProjectile(const TObjectPtr<AAMP_PlayerController> PC, const FVector ProjectileLocation);

public:
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	UFUNCTION(BlueprintCallable)
	AActor* GetProjectileFromPool(UClass* ProjectileClass, AActor* NewOwner = nullptr);

	//	Return projectile to pool
	// @param Projectile - Projectile to return to pool
	// @return - Whether the projectile was successfully returned to the pool
	UFUNCTION(BlueprintCallable)
	bool ReturnProjectileToPool(AActor* Projectile);

	static FTransform GetSpawnTransform(const FAMP_ProjectileData& ProjectileData);

	// Get the's 1/2 ping (one-way travel time) in seconds between firing client and the server
	// Returns 0 for non-player firing actors (ie: server pawns)
	virtual float GetServerPredictionTime(AActor* Player) const;
	

private:
	UPROPERTY()
	TMap<FGuid, TObjectPtr<UAMP_ProjectileMovementComponent>> SpawnedProjectiles;

	// Buffer of projectiles that were spawned on server and need to be spawned on clients
	UPROPERTY()
	TArray<FAMP_ProjectileData> ServerProjectilesBuffer;
	
	UPROPERTY()
	TMap<UClass*, FAMP_ProjectilePool> ProjectilePool;

	void AllocateStartingPools();

	UPROPERTY()
	TObjectPtr<APlayerController> LocalPlayerController{nullptr};

	UPROPERTY()
	TObjectPtr<UNetworkTimeSubsystem> NetworkTimeSubsystem;

};
