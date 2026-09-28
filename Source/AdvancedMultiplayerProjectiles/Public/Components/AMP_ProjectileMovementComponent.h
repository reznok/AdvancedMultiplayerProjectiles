// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Utility/FAMP_ProjectileData.h"
#include "Engine/EngineTypes.h"
#include "Utility/FAMP_ProjectileEvents.h"
#include "Components/ActorComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "AMP_ProjectileMovementComponent.generated.h"

class AAMP_AMPManager;
class AAMP_AMPManager;
struct FAMP_ProjectileData;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnProjectileHit, AActor*, HitActor, FVector, HitLocation);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnProjectileDestroyed);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnProjectileInitialized);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnProjectileRedirected);

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

	// The key shared with the other copy of a client-predicted shot (0 = none)
	int64 GetPredictionKey() const { return ProjectileData.PredictionKey; }

	// This machine fired it (the firing client's predicted copy, or a listen server's own shot)
	bool IsLocallyFired() const { return ProjectileData.bIsLocallyFired; }

	// Why the current (or last) flight ended; valid inside OnProjectileDestroyed
	uint8 GetEndCode() const { return EndCode; }

	// Where the current (or last) flight ended; valid inside OnProjectileDestroyed
	FVector GetEndLocation() const { return EndLocation; }

	// Server: end this flight at Location with a game-defined code (sent to every client copy)
	UFUNCTION(BlueprintCallable)
	void EndProjectileAt(const FVector& Location, uint8 InEndCode);

	// Client: the server ended this flight (snaps to the server's end location, then destroys)
	virtual void ApplyEnd(const FAMP_ProjectileEnd& End);

	// Client: the server redirected this flight (re-places it and catches up from the server's time)
	virtual void ApplyRedirect(const FAMP_ProjectileRedirect& Redirect);

	// Sets the flight velocity and faces it
	void SetFlightVelocity(const FVector& NewVelocity);

	AAMP_AMPManager* GetProjectileManager() const { return ProjectileManager; }

	UPROPERTY(BlueprintAssignable)
	FOnProjectileRedirected OnProjectileRedirected;

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

	// Client: whether this (remote) projectile threatens the local player, so it is shown ahead by the local one-way delay.
	// Asked every AdaptivePredictionCheckInterval until it answers true once (again after a redirect). Default: a path
	// trace for the local pawn (IsLocalPlayerInProjectilePath); override with the game's own threat test.
	UFUNCTION(BlueprintCallable)
	virtual bool ShouldApplyAdaptivePrediction();

	// Default adaptive test: object types the path trace looks for (the local pawn must be one of them)
	UPROPERTY(EditAnywhere)
	TArray<TEnumAsByte<EObjectTypeQuery>> AdaptivePredictionObjectTypes;

	// Default adaptive test: radius (uu) of the swept path
	UPROPERTY(EditAnywhere)
	float AdaptivePredictionRadius{100.f};

	// Default adaptive test: seconds of flight traced ahead
	UPROPERTY(EditAnywhere)
	float AdaptivePredictionHorizon{1.f};

	// Seconds between adaptive tests (0 = every tick)
	UPROPERTY(EditAnywhere)
	float AdaptivePredictionCheckInterval{0.f};

	float AdaptivePredictionCheckTimer{0.f};

	uint8 EndCode{AMP_EndCode::Destroyed};
	FVector EndLocation{FVector::ZeroVector};
	// EndLocation was decided for this flight (EndProjectileAt, or the first DestroyProjectile)
	bool bEndLocationSet{false};

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

	// The maximum time in seconds a single interp step will take
	UPROPERTY(EditAnywhere)
	float MaxSmoothingInterpStep = 0.1f;

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
	float PredictionFudgeFactor{0.f};
	
	virtual bool IsLocalPlayerInProjectilePath(bool DrawDebug = false);

	// Server-authoritative hit detection. Sweeps the segment the projectile
	// moved this tick for Pawn overlaps and broadcasts OnProjectileHit once per
	// newly-encountered actor (owner + projectile excluded). Consumers run all
	// gameplay from this on authority only — the projectile owns no gameplay
	// state. Runs only when OnProjectileHit is bound, so unbound projectiles
	// pay nothing and existing behaviour is unchanged.
	virtual void DetectAndReportHits(const FVector& SegmentStart, const FVector& SegmentEnd);

	// Radius (uu) of the server-side hit-detection sweep. Clamped to >= 1.
	UPROPERTY(EditAnywhere)
	float HitDetectionRadius{20.f};

	// Actors already reported via OnProjectileHit this flight, so each is
	// reported at most once. Cleared on (re)initialize and pool reset.
	UPROPERTY()
	TSet<TObjectPtr<AActor>> ReportedHitActors;

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
