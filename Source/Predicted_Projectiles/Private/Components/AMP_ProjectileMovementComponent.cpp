// Fill out your copyright notice in the Description page of Project Settings.


#include "Components/AMP_ProjectileMovementComponent.h"

#include "Actors/AMP_AMPManager.h"
#include "GameFramework/PlayerState.h"
#include "Kismet/GameplayStatics.h"


// Sets default values for this component's properties
UAMP_ProjectileMovementComponent::UAMP_ProjectileMovementComponent()
{
	// Set this component to be initialized when the game starts, and to be ticked every frame.  You can turn these features
	// off to improve performance if you don't need them.
	PrimaryComponentTick.bCanEverTick = true;

	// ...
}

void UAMP_ProjectileMovementComponent::DestroyProjectile()
{
	if (PredictedProjectileManager)
	{
		PredictedProjectileManager->DestroyProjectile(this);
	}
	else
	{
		GetOwner()->Destroy();
	}

	OnProjectileDestroyed.Broadcast();
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

	RootComponent->OnComponentBeginOverlap.AddDynamic(this, &UAMP_ProjectileMovementComponent::OnOverlapBegin);
}

void UAMP_ProjectileMovementComponent::SetProjectileData(const FAMP_ProjectileData& NewProjectileData, const TObjectPtr<AAMP_AMPManager> NewPredictedProjectileManager)
{
	ProjectileData = NewProjectileData;
	PredictedProjectileManager = NewPredictedProjectileManager;
	
	InitializeProjectile();
}

void UAMP_ProjectileMovementComponent::ResetProjectile()
{
	bForwardInterpolationStarted = false;
	bIsCheckingAdaptivePrediction = false;
	bFoundAdaptivePredictionHit = false;
}



void UAMP_ProjectileMovementComponent::InitializeProjectile()
{
	// Set Projectile Transform
	GetOwner()->SetActorLocation(AAMP_AMPManager::GetSpawnTransform(ProjectileData).GetLocation());
	GetOwner()->SetActorTickEnabled(true);
	GetOwner()->SetActorHiddenInGame(false);
	GetOwner()->SetActorEnableCollision(true);
	
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

	// The server uses the time between the firing client and the server to determine how far ahead the projectile should simulate
	if (ProjectileData.Owner->HasAuthority())
	{
		HalfRTT = (InterpolationType != EAMP_InterpolationType::None && GetNetMode() != NM_Standalone) ? (FMath::Clamp((PredictedProjectileManager->GetServerPredictionTime(ProjectileData.Owner) - PredictionFudgeFactor) * 0.0005f, 0.f, MaxPredictionPing / 1000)) : 0.f;
		InterpolationTimeRemaining = HalfRTT;
	}
	// The client uses the time between the server and its self to determine how far ahead the projectile should simulate
	else
	{
		HalfRTT = (InterpolationType != EAMP_InterpolationType::None && GetNetMode() != NM_Standalone) ? (FMath::Clamp((GetClientPredictionTime() - PredictionFudgeFactor) * 0.0005f, 0.f, MaxPredictionPing / 1000)) : 0.f;
		ForwardInterpolationTime = ProjectileData.SimulationDelay + (HalfRTT * 2.f) * FMath::Clamp(ProjectileData.ForwardPredictionScale, 0.f, 1.f);
		ClientPredictedInterpolationTime = ProjectileData.FiringClientDelay;
		InterpolationTimeRemaining = ClientPredictedInterpolationTime;
	}

	// ClientPredicted is always applied
	LifeTime -= ClientPredictedInterpolationTime;

	const bool FiredLocally = ProjectileData.IsClientPredicted && !ProjectileData.bIsServerSpawned;
	
	if (!FiredLocally && !bUseAdaptivePrediction)
	{
		// Immediately start forward prediction
		
		bForwardInterpolationStarted = true;
		InterpolationTimeRemaining += ForwardInterpolationTime;
		LifeTime -= ForwardInterpolationTime;
	}
}

bool UAMP_ProjectileMovementComponent::ShouldApplyAdaptivePrediction()
{
	return false;
}

float UAMP_ProjectileMovementComponent::HandleInterpolation(float DeltaTime)
{
	if (InterpolationTimeRemaining <= 0) return 0.0f;
	
	if (GetNetMode() == NM_Client && bUseAdaptivePrediction && !bForwardInterpolationStarted)
	{
		bIsCheckingAdaptivePrediction = true;
		SimulateProjectileToEndOfLifeTime();
		bIsCheckingAdaptivePrediction = false;
	}
	
	switch(InterpolationType)
	{
		case EAMP_InterpolationType::Smooth:
			return SmoothToServerStateStep(DeltaTime);
			break;
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
		Step = InterpolationTimeRemaining * SmoothingInterpSpeed;
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

void UAMP_ProjectileMovementComponent::SimulateProjectileToEndOfLifeTime()
{
	FVector StartLocation = GetOwner()->GetActorLocation();
	Super::TickComponent(LifeTime, ELevelTick::LEVELTICK_All, nullptr);
	GetOwner()->SetActorLocation(StartLocation);

	if (bFoundAdaptivePredictionHit || ShouldApplyAdaptivePrediction())
	{
		bIsCheckingAdaptivePrediction = false;
		bForwardInterpolationStarted = true;
		InterpolationTimeRemaining += ForwardInterpolationTime;
		// UE_LOG(LogTemp, Warning, TEXT("Adaptive Interpolation Started"));
		BeginInterpolation();
	}
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


void UAMP_ProjectileMovementComponent::OnOverlapBegin(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
                                                         int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	// Adaptive prediction is happening, so don't fire off ProjectileHits
	if (bIsCheckingAdaptivePrediction)
	{
		if (APawn* OtherPawn = Cast<APawn>(OtherActor))
		{
			APlayerController* LocalPlayerController = UGameplayStatics::GetPlayerController(GetWorld(), 0);
			if (OtherPawn == LocalPlayerController->GetPawn())
			{
				bFoundAdaptivePredictionHit = true;
			}
		}
	}

	else
	{
		OnProjectileHit.Broadcast(OtherActor);
	}
}

// Called every frame
void UAMP_ProjectileMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	if (bTickLifeTime)
	{
		LifeTime -= DeltaTime;
		if (LifeTime <= 0)
		{
			if (PredictedProjectileManager)
			{
				// DestroyProjectile();
				return;
			}
		}
	}

	DeltaTime += HandleInterpolation(DeltaTime);
	
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// ...
}


