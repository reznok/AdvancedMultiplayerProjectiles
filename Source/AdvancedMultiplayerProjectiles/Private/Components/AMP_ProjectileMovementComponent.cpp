// Fill out your copyright notice in the Description page of Project Settings.


#include "Components/AMP_ProjectileMovementComponent.h"

#include "Actors/AMP_AMPManager.h"
#include "CollisionQueryParams.h"
#include "Engine/World.h"
#include "GameFramework/PlayerState.h"
#include "Kismet/GameplayStatics.h"
#include "NetworkTimeSubsystem.h"
#include "WorldCollision.h"


// Sets default values for this component's properties
UAMP_ProjectileMovementComponent::UAMP_ProjectileMovementComponent()
{
	// Set this component to be initialized when the game starts, and to be ticked every frame.  You can turn these features
	// off to improve performance if you don't need them.
	PrimaryComponentTick.bCanEverTick = true;
	AdaptivePredictionObjectTypes.Add(UEngineTypes::ConvertToObjectType(ECC_Pawn));

	// ...
}

void UAMP_ProjectileMovementComponent::DestroyProjectile()
{
	if (EndLocation.IsZero() && GetOwner())
	{
		EndLocation = GetOwner()->GetActorLocation();
	}
	if (ProjectileManager)
	{
		// Already ended (and possibly pooled): no second OnProjectileDestroyed
		if (!ProjectileManager->DestroyProjectile(this)) return;
	}
	else
	{
		GetOwner()->Destroy();
	}

	OnProjectileDestroyed.Broadcast();
}

void UAMP_ProjectileMovementComponent::EndProjectileAt(const FVector& Location, const uint8 InEndCode)
{
	EndCode = InEndCode;
	EndLocation = Location;
	DestroyProjectile();
}

void UAMP_ProjectileMovementComponent::ApplyEnd(const FAMP_ProjectileEnd& End)
{
	if (GetOwner())
	{
		GetOwner()->SetActorLocation(End.Location);
	}
	EndProjectileAt(End.Location, End.EndCode);
}

void UAMP_ProjectileMovementComponent::SetFlightVelocity(const FVector& NewVelocity)
{
	Velocity = NewVelocity;
	UpdateComponentVelocity();
	if (!NewVelocity.IsNearlyZero() && GetOwner())
	{
		GetOwner()->SetActorRotation(NewVelocity.Rotation());
	}
}

void UAMP_ProjectileMovementComponent::ApplyRedirect(const FAMP_ProjectileRedirect& Redirect)
{
	if (GetOwner())
	{
		GetOwner()->SetActorLocation(Redirect.Location);
	}
	SetFlightVelocity(Redirect.Velocity);

	// Catch up from the server's moment of the change to now, with the same clamp as a spawn
	const UNetworkTimeSubsystem* TimeSubsystem = ProjectileManager ? ProjectileManager->GetNetworkTimeSubsystem().Get() : nullptr;
	const float Now = TimeSubsystem ? TimeSubsystem->GetServerWorldTime() : Redirect.ServerTime;
	InterpolationTimeRemaining = FMath::Clamp(Now - Redirect.ServerTime, 0.f, MaxPredictionPing / 2 / 1000);

	// The new path may threaten the local player (or stop threatening it): ask again
	bFoundAdaptivePredictionHit = false;
	AdaptivePredictionCheckTimer = 0.f;
	OnProjectileRedirected.Broadcast();
}

// Called when the game starts
void UAMP_ProjectileMovementComponent::BeginPlay()
{
	Super::BeginPlay();
	// ...
	RootComponent = Cast<UPrimitiveComponent>(GetOwner()->GetRootComponent());
	if (!RootComponent && bUseAdaptivePrediction)
	{
		UE_LOG(LogTemp, Warning, TEXT("UAMP_ProjectileMovementComponent::BeginPlay: RootComponent is not a collider, adaptive prediction will be disabled"));
	}
}

void UAMP_ProjectileMovementComponent::SetProjectileData(const FAMP_ProjectileData& NewProjectileData, const TObjectPtr<AAMP_AMPManager> NewPredictedProjectileManager)
{
	ProjectileData = NewProjectileData;
	ProjectileManager = NewPredictedProjectileManager;

	if (!ProjectileManager)
	{
		UE_LOG(LogAMP, Error, TEXT("UAMP_ProjectileMovementComponent::SetProjectileData: ProjectileManager is nullptr"));
		return;
	}

	if (!ProjectileData.Owner)
	{
		UE_LOG(LogAMP, Error, TEXT("UAMP_ProjectileMovementComponent::SetProjectileData: Owner is nullptr"));
		return;
	}
	
	InterpolationTimeRemaining = 0.0f;
	ForwardInterpolationTime = 0.0f;
	EndCode = AMP_EndCode::Destroyed;
	EndLocation = FVector::ZeroVector;
	AdaptivePredictionCheckTimer = 0.f;

	InitializeProjectile();
}

void UAMP_ProjectileMovementComponent::ResetProjectile()
{
	bForwardInterpolationStarted = false;
	bIsCheckingAdaptivePrediction = false;
	bFoundAdaptivePredictionHit = false;
	ReportedHitActors.Reset();
}

void UAMP_ProjectileMovementComponent::CalcInterpolationTime_PlayerPing()
{
	// The server uses the time between the firing client and the server to determine how far ahead the projectile should simulate
	if (ProjectileData.Owner->HasAuthority())
	{
		HalfRTT = (InterpolationType != EAMP_InterpolationType::None && GetNetMode() != NM_Standalone) ? (FMath::Clamp((ProjectileManager->GetServerPredictionTime(ProjectileData.Owner)) * 0.0005f, 0.f, MaxPredictionPing / 1000)) : 0.f;
		InterpolationTimeRemaining = HalfRTT;
	}
	// The client uses the time between the server and its self to determine how far ahead the projectile should simulate
	else
	{
		HalfRTT = (InterpolationType != EAMP_InterpolationType::None && GetNetMode() != NM_Standalone) ? (FMath::Clamp((GetClientPredictionTime()) * 0.0005f, 0.f, MaxPredictionPing / 1000)) : 0.f;
		ForwardInterpolationTime = ProjectileData.SimulationDelay + (HalfRTT * 2.f) * FMath::Clamp(ProjectileData.ForwardPredictionScale, 0.f, 1.f);
		ClientPredictedInterpolationTime = ProjectileData.FiringClientDelay;
		InterpolationTimeRemaining = ClientPredictedInterpolationTime;
	}
}

void UAMP_ProjectileMovementComponent::CalcInterpolationTime_NetworkTimeSync()
{
	// No interp for locally fired projectiles
	if (ProjectileData.bIsLocallyFired) return;

	const bool IsServer = ProjectileData.Owner->HasAuthority();
	
	const float CurrentTime = ProjectileManager->GetNetworkTimeSubsystem()->GetServerWorldTime();
	float LocalClientDelay = CurrentTime - ProjectileData.ServerProjectileSpawnTime;
	float ProjectileLagTime;
	
	if (ProjectileData.ClientProjectileSpawnTime != 0)
	{
		ProjectileLagTime = CurrentTime - ProjectileData.ClientProjectileSpawnTime;
	}
	else
	{
		ProjectileLagTime = CurrentTime - ProjectileData.ServerProjectileSpawnTime;
	}

	// Clamp the lag time to the max prediction ping
	ProjectileLagTime = FMath::Clamp(ProjectileLagTime, 0.f, MaxPredictionPing / 2 / 1000);

	if (!ProjectileData.Owner->HasAuthority())
	{
		ProjectileLagTime += PredictionFudgeFactor * 0.001f;
	}

	InterpolationTimeRemaining += ProjectileLagTime;
	ForwardInterpolationTime = (LocalClientDelay * FMath::Clamp(ProjectileData.ForwardPredictionScale, 0.f, 1.f));
	
	UE_LOG(LogAMP, Verbose, TEXT("[IsServer: %d] UAMP_ProjectileMovementComponent::CalcInterpolationTime_NetworkTimeSync: Forward Interpolation Time: %f"), IsServer, ForwardInterpolationTime);
	UE_LOG(LogAMP, Verbose, TEXT("[IsServer: %d] UAMP_ProjectileMovementComponent::CalcInterpolationTime_NetworkTimeSync: Interpolation Time Remaining: %f"), IsServer, InterpolationTimeRemaining);
}


void UAMP_ProjectileMovementComponent::InitializeProjectile()
{
	// Set Projectile Transform
	GetOwner()->SetActorLocation(AAMP_AMPManager::GetSpawnTransform(ProjectileData).GetLocation());
	GetOwner()->SetActorTickEnabled(true);
	GetOwner()->SetActorHiddenInGame(false);
	GetOwner()->SetActorEnableCollision(true);
	SetUpdatedComponent(GetOwner()->GetRootComponent());
	
	// Set Interpolation Type
	InterpolationType = ProjectileData.InterpolationType;

	// Set Lifetime
	if (ProjectileData.LifeTime > 0)
	{
		bTickLifeTime = true;
		LifeTime = ProjectileData.LifeTime;
	}
	
	Velocity = ProjectileData.Velocity;

	// Set up for Lag Compensation
	switch (ProjectileManager->LagCompensationMethod)
	{
		case EAMP_LagCompensationMethod::NetworkTimeSync:
			CalcInterpolationTime_NetworkTimeSync();
			break;
		// case EAMP_LagCompensationMethod::PlayerPing:
		// 	CalcInterpolationTime_PlayerPing();
		// 	break;
	default:
		UE_LOG(LogAMP, Error, TEXT("UAMP_ProjectileMovementComponent::InitializeProjectile: Lag Compensation Method not set"));
		return;
	}
	
	// ClientPredicted is always applied
	LifeTime -= ClientPredictedInterpolationTime;

	// Fresh flight — clear any actors carried over from a pooled reuse.
	ReportedHitActors.Reset();

	const bool FiredLocally = ProjectileData.IsClientPredicted && !ProjectileData.bIsServerSpawned;
	
	if (!FiredLocally && !bUseAdaptivePrediction)
	{
		// Immediately start forward prediction
		
		bForwardInterpolationStarted = true;
		InterpolationTimeRemaining += ForwardInterpolationTime;
		LifeTime -= ForwardInterpolationTime;
	}

	OnProjectileInitialized.Broadcast();
}

bool UAMP_ProjectileMovementComponent::ShouldApplyAdaptivePrediction()
{
	return IsLocalPlayerInProjectilePath(false);
}

float UAMP_ProjectileMovementComponent::HandleInterpolation(float DeltaTime)
{
	if (InterpolationTimeRemaining <= 0) return 0.0f;
	
	switch(InterpolationType)
	{
		case EAMP_InterpolationType::Smooth:
			return SmoothToServerStateStep(DeltaTime);
		case EAMP_InterpolationType::Snap:
				SnapInterpolation();
			break;
		default:
			break;
	}

	return 0.0f;
}

void UAMP_ProjectileMovementComponent::SnapInterpolation()
{
	for (int x = 0; x < SNAP_INTERPOLATION_SUBSTEPS; x++)
	{
		Super::TickComponent(InterpolationTimeRemaining / SNAP_INTERPOLATION_SUBSTEPS, ELevelTick::LEVELTICK_All, nullptr);
	}
	InterpolationTimeRemaining = 0.f;
}

float UAMP_ProjectileMovementComponent::SmoothToServerStateStep(float DeltaTime)
{
	float Step = 0.f;
	
	if (InterpolationTimeRemaining > 0)
	{
		// Clamp step
		Step = FMath::Clamp(InterpolationTimeRemaining * SmoothingInterpSpeed, 0.f, MaxSmoothingInterpStep);
		InterpolationTimeRemaining -= Step;
		
		if (InterpolationTimeRemaining <= DeltaTime / 2.f)
		{
			Step += InterpolationTimeRemaining;
			InterpolationTimeRemaining = 0.f;
			return Step;
		}
	}
	
	return Step;
}



float UAMP_ProjectileMovementComponent::GetClientPredictionTime() const
{
	// Player Controller of the local player
	const TObjectPtr<APlayerController> PlayerController = UGameplayStatics::GetPlayerController(GetWorld(), 0);

	// Issue with getting the player controller
	if (!IsValid(PlayerController)) return 0.0f;

	const TObjectPtr<APlayerState> PlayerState = PlayerController->PlayerState;

	// Issue with getting the player state
	if (!IsValid(PlayerState)) return 0.0f;

	return PlayerState->ExactPing;
}

void UAMP_ProjectileMovementComponent::BeginInterpolation()
{
	// Don't interp client predicted projectiles on their local machines
	if (!ProjectileData.bIsServerSpawned) return;
	
	if (InterpolationType == EAMP_InterpolationType::Snap)
	{
		SnapInterpolation();
	}
	bForwardInterpolationStarted = true;
}

bool UAMP_ProjectileMovementComponent::IsLocalPlayerInProjectilePath(bool DrawDebug)
{
	FPredictProjectilePathParams PathParams;
	PathParams.StartLocation = GetOwner()->GetActorLocation();
	PathParams.LaunchVelocity = Velocity;
	PathParams.bTraceWithCollision = true;
	PathParams.ProjectileRadius = AdaptivePredictionRadius;
	PathParams.MaxSimTime = AdaptivePredictionHorizon;
	// Object queries report any object of these types, so query-only (overlap) pawns are found too
	PathParams.bTraceWithChannel = false;
	PathParams.ObjectTypes = AdaptivePredictionObjectTypes;
	if (DrawDebug)
	{
		PathParams.DrawDebugType = EDrawDebugTrace::ForDuration;
		PathParams.DrawDebugTime = LifeTime;
	}
	PathParams.OverrideGravityZ = GetGravityZ();
	if (PathParams.OverrideGravityZ == 0.0f)
	{
		// This is dumb, but PredictProjectilePath will use the World gravity if it's 0. This
		// prevents that.
		PathParams.OverrideGravityZ = 0.01f;
	}
	PathParams.ActorsToIgnore = {GetOwner(), ProjectileData.Owner};
	
	FPredictProjectilePathResult PathResult;
	UGameplayStatics::PredictProjectilePath(GetWorld(), PathParams, PathResult);

	if (PathResult.HitResult.bBlockingHit)
	{
		AActor* HitActor = PathResult.HitResult.GetActor();
		if (!HitActor) return false;
		APawn* HitPawn = Cast<APawn>(HitActor);
		if (!HitPawn) return false;
		APlayerController* LocalPlayerController = UGameplayStatics::GetPlayerController(GetWorld(), 0);
		if (!LocalPlayerController) return false;
		return HitPawn == LocalPlayerController->GetPawn();
	}

	return false;
}

// Called every frame
void UAMP_ProjectileMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	if (bTickLifeTime)
	{
		LifeTime -= DeltaTime;
		if (LifeTime <= 0)
		{
			if (ProjectileManager)
			{
				DestroyProjectile();
				return;
			}
		}
	}

	if (GetNetMode() == NM_Client && bUseAdaptivePrediction && !bFoundAdaptivePredictionHit && !ProjectileData.bIsLocallyFired)
	{
		AdaptivePredictionCheckTimer -= DeltaTime;
		if (AdaptivePredictionCheckTimer <= 0.f)
		{
			AdaptivePredictionCheckTimer = AdaptivePredictionCheckInterval;
			if (ShouldApplyAdaptivePrediction())
			{
				bForwardInterpolationStarted = true;
				bFoundAdaptivePredictionHit = true;
				InterpolationTimeRemaining += ForwardInterpolationTime;
				UE_LOG(LogAMP, Verbose, TEXT("UAMP_ProjectileMovementComponent::TickComponent: Adaptive Prediction Hit"));
			}
		}
	}

	const FVector PreMoveLocation = GetOwner() ? GetOwner()->GetActorLocation() : FVector::ZeroVector;

	DeltaTime += HandleInterpolation(DeltaTime);
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// Server-authoritative hit reporting along the segment just travelled.
	// Gate on the net mode, not the firing actor's authority: projectiles keep
	// detecting hits after their shooter is destroyed (e.g. died mid-flight).
	if (GetNetMode() != NM_Client && GetOwner())
	{
		DetectAndReportHits(PreMoveLocation, GetOwner()->GetActorLocation());
	}

	// ...
}

void UAMP_ProjectileMovementComponent::DetectAndReportHits(const FVector& SegmentStart, const FVector& SegmentEnd)
{
	if (!OnProjectileHit.IsBound()) return;

	UWorld* World = GetWorld();
	if (!World) return;

	FCollisionQueryParams Params(SCENE_QUERY_STAT(AMPProjectileHit), false);
	Params.AddIgnoredActor(GetOwner());
	if (ProjectileData.Owner)
	{
		Params.AddIgnoredActor(ProjectileData.Owner);
	}

	FCollisionObjectQueryParams ObjectParams;
	ObjectParams.AddObjectTypesToQuery(ECC_Pawn);

	// SweepMulti needs a non-degenerate segment; nudge the end if the projectile
	// did not move this tick so a resting overlap is still caught.
	const FVector SweepEnd = (SegmentEnd - SegmentStart).IsNearlyZero()
		                         ? SegmentEnd + FVector(0.f, 0.f, KINDA_SMALL_NUMBER)
		                         : SegmentEnd;

	TArray<FHitResult> Hits;
	const bool bAnyHit = World->SweepMultiByObjectType(
		Hits, SegmentStart, SweepEnd, FQuat::Identity, ObjectParams,
		FCollisionShape::MakeSphere(FMath::Max(HitDetectionRadius, 1.f)), Params);

	if (!bAnyHit) return;

	for (const FHitResult& Hit : Hits)
	{
		AActor* HitActor = Hit.GetActor();
		if (!HitActor) continue;

		bool bAlreadyReported = false;
		ReportedHitActors.Add(HitActor, &bAlreadyReported);
		if (bAlreadyReported) continue;

		OnProjectileHit.Broadcast(HitActor, Hit.ImpactPoint);
	}
}


