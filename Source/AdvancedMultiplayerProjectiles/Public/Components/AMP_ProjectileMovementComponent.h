// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Utility/FAMP_ProjectileData.h"
#include "Components/ActorComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "AMP_ProjectileMovementComponent.generated.h"

class AAMP_AMPManager;
class AAMP_AMPManager;
struct FAMP_ProjectileData;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnProjectileHit, AActor*, HitActor, FVector, HitLocation);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnProjectileDestroyed);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnProjectileInitialized);

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class ADVANCEDMULTIPLAYERPROJECTILES_API UAMP_ProjectileMovementComponent : public UProjectileMovementComponent
{
	GENERATED_BODY()

public:
	// Sets default values for this component's properties
	UAMP_ProjectileMovementComponent();

	UFUNCTION(BlueprintPure)
	bool IsProjectileCheckingAdaptivePrediction() const { return bIsCheckingAdaptivePrediction; }

	UFUNCTION()
	FGuid GetProjectileID() const { return ProjectileData.ProjectileID; }

	UFUNCTION()
	FAMP_ProjectileData GetProjectileData() { return ProjectileData; };

	UFUNCTION()
	bool NotifyClientsOnDestroy() const { return bServerNotifiesClientsOnDestroy; }

	UFUNCTION(BlueprintCallable)
	void DestroyProjectile();

	UPROPERTY(BlueprintAssignable)
	FOnProjectileHit OnProjectileHit;

	UPROPERTY(BlueprintAssignable)
	FOnProjectileDestroyed OnProjectileDestroyed;

	UPROPERTY(BlueprintAssignable)
	FOnProjectileInitialized OnProjectileInitialized;

protected:
	// Called when the game starts
	virtual void BeginPlay() override;

	virtual void InitializeProjectile();

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float LifeTime{0.f};
	
	bool bTickLifeTime{false};

	// Whether the server will notify clients when this projectile is destroyed
	// This is useful for projectiles that are destroyed on the server but may not have been seen on clients
	// Results in a lot more RPC calls
	UPROPERTY(EditAnywhere)
	bool bServerNotifiesClientsOnDestroy{true};

	UPROPERTY(EditAnywhere)
	bool bUseAdaptivePrediction{true};
	
	bool bFoundAdaptivePredictionHit{false};
	bool bIsCheckingAdaptivePrediction{false};

	// Intentionally left blank overridable function for custom adaptive prediction detections
	UFUNCTION(BlueprintCallable)
	virtual bool ShouldApplyAdaptivePrediction();

	// How far behind the sim proxy is in time
	float HalfRTT{0.f};

	// The amount of time to simulate forward to match the firing client
	// This is roughly equal to 1/2 RTT for the firing client
	// This gets added on the server and all other clients
	float ClientPredictedInterpolationTime{0.f};

	// Equal to roughly the RTT, the amount a client should forward interpolate, predicting the movement of the projectile
	float ForwardInterpolationTime{0.f};
	
	bool bAppliedClientPredictionInterpolationTime{false};
	bool bAppliedForwardInterpolationTime{false};
	float InterpolationTimeRemaining{0.f};

	virtual float HandleInterpolation(float DeltaTime);

	EAMP_InterpolationType InterpolationType = EAMP_InterpolationType::Smooth;

	// The speed at which the projectile will interpolate to the server state
	// 1 is instant, 0 is no interpolation
	// The lower this is the smoother is feels, but the less accurate it is
	// Todo: Allow setting a # of seconds to interp over instead
	UPROPERTY(EditAnywhere)
	float SmoothingInterpSpeed = .1f;

	// Whether or not to actually interpolate. For non adaptive, this is always true at spawn
	bool bForwardInterpolationStarted{false};
	
	UPROPERTY(BlueprintReadOnly)
	FAMP_ProjectileData ProjectileData{};

	// Snaps the projectile immediately to the server state.
	// Will still trigger overlaps along the way
	virtual void SnapInterpolation();

	// Get an increased DeltaTime step to catch up to the server state
	// https://fish-networking.gitbook.io/docs/manual/guides/lag-compensation/projectiles
	virtual float SmoothToServerStateStep(float DeltaTime);

	// Get the's 1/2 ping (one-way travel time) in seconds between the local client and the server
	virtual float GetClientPredictionTime() const;
	
	virtual void BeginInterpolation();

	// User Configurable

	// The maximum allowed time (milliseconds) behind the server state a projectile will adjust to in seconds
	// Lower this value to the maximum ping the game will support
	UPROPERTY(EditAnywhere)
	float MaxPredictionPing{250.f};

	// The amount of time in seconds to add to the player's ping to account for prediction fudge
	// This is how Unreal Tournament does it
	// https://github.com/JimmieKJ/unrealTournament/blob/cac883d9143e72d21c046bfa4a0915b3ad533dad/UnrealTournament/Source/UnrealTournament/Private/UTPlayerController.cpp#L281
	UPROPERTY(EditAnywhere)
	float PredictionFudgeFactor{.05f};
	
	virtual bool IsLocalPlayerInProjectilePath(bool DrawDebug = false);

public:
	// Called every frame
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	virtual void SetProjectileData(const FAMP_ProjectileData& NewProjectileData, TObjectPtr<AAMP_AMPManager> NewPredictedProjectileManager);

	// Called when projectile is sent to pool, resets the projectile to its default state
	virtual void ResetProjectile();
	
private:
	UPROPERTY()
	TObjectPtr<UPrimitiveComponent> RootComponent;

	UPROPERTY()
	TObjectPtr<AAMP_AMPManager> ProjectileManager;

	const int SNAP_INTERPOLATION_SUBSTEPS = 10;

	void CalcInterpolationTime_PlayerPing();
	void CalcInterpolationTime_NetworkTimeSync();
	
};
