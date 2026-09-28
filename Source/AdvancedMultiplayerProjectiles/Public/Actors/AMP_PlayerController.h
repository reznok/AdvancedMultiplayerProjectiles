// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Utility/FAMP_ProjectileEvents.h"
#include "AMP_PlayerController.generated.h"

class UNetworkTimeSyncComponent;
class UAMP_ProjectileMovementComponent;
struct FAMP_ProjectileData;
class AAMP_AMPManager;
/**
 * 
 */
UCLASS()
class ADVANCEDMULTIPLAYERPROJECTILES_API AAMP_PlayerController : public APlayerController
{
	GENERATED_BODY()

	// Add object initializer
	AAMP_PlayerController(const FObjectInitializer& ObjectInitializer);
	
	UPROPERTY(EditAnywhere)
	UNetworkTimeSyncComponent* NetworkTimeSyncComponent{nullptr};

	virtual void BeginPlay() override;

public:
	// Tell sim proxies to spawn projectile
	UFUNCTION(Client, UnReliable)
	virtual void ClientRPC_SpawnProjectilesSimProxies(const TArray<FAMP_ProjectileData>& ProjectileData);

	// Tell this client that projectiles ended (one tick's batch)
	UFUNCTION(Client, UnReliable)
	virtual void ClientRPC_EndProjectilesSimProxies(const TArray<FAMP_ProjectileEnd>& Ends);

	// Tell this client that projectiles were redirected (one tick's batch)
	UFUNCTION(Client, UnReliable)
	virtual void ClientRPC_RedirectProjectilesSimProxies(const TArray<FAMP_ProjectileRedirect>& Redirects);

	UFUNCTION(BlueprintCallable)
	virtual UAMP_ProjectileMovementComponent* SpawnClientPredictedProjectile(FAMP_ProjectileData ProjectileData);
	
	UFUNCTION(BlueprintCallable)
	virtual TArray<UAMP_ProjectileMovementComponent*> SpawnClientPredictedProjectiles(TArray<FAMP_ProjectileData>& ProjectileData);

	UFUNCTION(Server, Unreliable, WithValidation)
	virtual void ServerRPC_SpawnClientPredictedProjectiles(const TArray<FAMP_ProjectileData>& ProjectileData);

	// Spawn the client predicted projectiles on the server
	// Default implementation is very insecure, the client controls all projectile data. Override this function!
	virtual void ServerRPC_SpawnClientPredictedProjectiles_Implementation(const TArray<FAMP_ProjectileData>& ProjectileData);
	
	// Validate the spawn client predicted projectiles
	// Returns True by default, designed to be overridden 
	virtual bool ServerRPC_SpawnClientPredictedProjectiles_Validate(const TArray<FAMP_ProjectileData>& ProjectileData);
	
private:
	UPROPERTY()
	TObjectPtr<AAMP_AMPManager> ProjectileManager;
};
