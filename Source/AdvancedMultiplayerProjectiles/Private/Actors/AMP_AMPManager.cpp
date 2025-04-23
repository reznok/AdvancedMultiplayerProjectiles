// Fill out your copyright notice in the Description page of Project Settings.


#include "Actors/AMP_AMPManager.h"

#include "EngineUtils.h"
#include "AdvancedMultiplayerProjectiles.h"
#include "Components/AMP_ProjectileMovementComponent.h"
#include "GameFramework/PlayerState.h"
#include "Kismet/GameplayStatics.h"
#include "Utility/FAMP_ProjectilePool.h"


// Sets default values
AAMP_AMPManager::AAMP_AMPManager()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	PrimaryActorTick.bAllowTickOnDedicatedServer = true;
}

// Called when the game starts or when spawned
void AAMP_AMPManager::BeginPlay()
{
	if (HasAuthority())
	{
		SetReplicates(true);
	}
	
	SetActorTickEnabled(true);
	
	Super::BeginPlay();
	
	if (bUseProjectilePool)
	{
		AllocateStartingPools();
	}

	// Only Listen Servers and Clients need to know about the local player controller
	if (GetNetMode() >= NM_ListenServer)
	{
		LocalPlayerController = UGameplayStatics::GetPlayerController(GetWorld(), 0);
	}

	NetworkTimeSubsystem = UNetworkTimeSubsystem::Get(this);
}

UAMP_ProjectileMovementComponent* AAMP_AMPManager::SpawnProjectile(FAMP_ProjectileData ProjectileData)
{
	TArray<UAMP_ProjectileMovementComponent*> NewlySpawnedProjectiles = SpawnProjectiles({ProjectileData});

	if (NewlySpawnedProjectiles.Num() > 0)
	{
		return NewlySpawnedProjectiles[0];
	}
	
	return nullptr;
}

TArray<UAMP_ProjectileMovementComponent*> AAMP_AMPManager::SpawnProjectiles(const TArray<FAMP_ProjectileData> ProjectilesData)
{
	TArray<UAMP_ProjectileMovementComponent*> NewlySpawnedProjectiles;
	for (auto Data : ProjectilesData)
	{
		if (const TObjectPtr<UAMP_ProjectileMovementComponent> ProjectileCmp = FireProjectile(Data))
		{
			if (HasAuthority())
			{
				ServerProjectilesBuffer.Add(ProjectileCmp->GetProjectileData());
			}
			NewlySpawnedProjectiles.Add(ProjectileCmp);
		}
	}
	
	return NewlySpawnedProjectiles;	 
}

void AAMP_AMPManager::MultiCast_SpawnProjectilesSimProxies_Implementation(const TArray<FAMP_ProjectileData>& ProjectileData)
{
	// Server and AuthProxy has already spawned it by this point
	if (GetLocalRole() == ROLE_Authority) return;

	SpawnProjectilesSimProxies(ProjectileData);
}

void AAMP_AMPManager::NetRelevant_SpawnProjectilesSimProxies(const TArray<FAMP_ProjectileData>& ProjectileData)
{
	// Only run on the server
	if (GetLocalRole() != ROLE_Authority) return;
	
	
	// Get all player controllers
	for (TActorIterator<AAMP_PlayerController> ActorItr(GetWorld()); ActorItr; ++ActorItr)
	{
		AAMP_PlayerController* PlayerController = *ActorItr;

		TArray<FAMP_ProjectileData> RelevantProjectiles;
		for (auto Data : ProjectileData)
		{
			if (IsPlayerControllerRelevantForProjectile(PlayerController, Data.Location))
			{
				RelevantProjectiles.Add(Data);
			}
		}
		PlayerController->ClientRPC_SpawnProjectilesSimProxies(RelevantProjectiles);
	}
}

void AAMP_AMPManager::SpawnProjectilesSimProxies(const TArray<FAMP_ProjectileData>& ProjectileData)
{
	for (auto Data : ProjectileData)
	{
		if (GetLocalRole() == ROLE_AutonomousProxy && !bShowServerProjectile && !Data.bShowOnClient) return;
		
		float Delay = 0.0f;
		Data.SimulationDelay = Delay;
		
		QueuedProjectiles.Add(TTuple<float, FAMP_ProjectileData>(Delay, Data));
	}
}

void AAMP_AMPManager::MultiCast_DestroyProjectileSimProxy_Implementation(const FGuid ProjectileID)
{
	// Server and AuthProxy has already spawned it by this point
	if (GetLocalRole() == ROLE_Authority) return;

	DestroyProjectileSimProxy(ProjectileID);
}

void AAMP_AMPManager::NetRelevant_DestroyProjectileSimProxy(const FGuid ProjectileID)
{
	if (GetLocalRole() != ROLE_Authority) return;

	// Get all player controllers
	for (TActorIterator<AAMP_PlayerController> ActorItr(GetWorld()); ActorItr; ++ActorItr)
	{
		AAMP_PlayerController* PlayerController = *ActorItr;
		
		TObjectPtr<UAMP_ProjectileMovementComponent> ProjectileComponent = SpawnedProjectiles.FindRef(ProjectileID);
		if (ProjectileComponent && IsPlayerControllerRelevantForProjectile(PlayerController, ProjectileComponent->GetOwner()->GetActorLocation()))
		{
			PlayerController->ClientRPC_DestroyProjectileSimProxy(ProjectileID);
		}
	}
}

void AAMP_AMPManager::DestroyProjectileSimProxy(const FGuid ProjectileID)
{
	// Projectile is already destroyed on the server
	if (GetLocalRole() == ROLE_Authority) return;
	
	TObjectPtr<UAMP_ProjectileMovementComponent> ProjectileComponent = SpawnedProjectiles.FindRef(ProjectileID);
	if (ProjectileComponent)
	{
		ProjectileComponent->DestroyProjectile();
		RemoveProjectile(ProjectileID);
	}
}


void AAMP_AMPManager::QueueProjectile(FAMP_ProjectileData& ProjectileData)
{
	QueuedProjectiles.Add(TTuple<float, FAMP_ProjectileData>(0.f, ProjectileData));
}

void AAMP_AMPManager::ProcessQueuedProjectiles(const float DeltaTime)
{
	TArray<TTuple<float, FAMP_ProjectileData>> ToRemove;
	for (TTuple<float, FAMP_ProjectileData>& SimProjectile : QueuedProjectiles)
	{
		SimProjectile.Get<0>() -= DeltaTime;
		if (SimProjectile.Get<0>() <= 0)
		{
			FireProjectile(SimProjectile.Get<1>());
			ToRemove.Add(SimProjectile);
		}
	}
	
	for (auto Tuple : ToRemove)
	{
		QueuedProjectiles.Remove(Tuple);
	}
}

void AAMP_AMPManager::ProcessServerProjectilesBuffers(float DeltaTime)
{
	if (!HasAuthority()) return;
	
	if (ServerProjectilesBuffer.Num() > 0)
	{
		if (bUseAMP_NetRelevancy)
		{
			NetRelevant_SpawnProjectilesSimProxies(ServerProjectilesBuffer);
		}
		else
		{
			MultiCast_SpawnProjectilesSimProxies(ServerProjectilesBuffer);
		}

		ServerProjectilesBuffer.Empty();
	}
}

void AAMP_AMPManager::SetProjectileDataDefaults(FAMP_ProjectileData& ProjectileData)
{
	if (ProjectileData.Owner == nullptr)
	{
		ProjectileData.Owner = this;
	}
	
	if (ProjectileData.LifeTime == 0)
	{
		ProjectileData.LifeTime = 10.f;
	}
}

TObjectPtr<UAMP_ProjectileMovementComponent> AAMP_AMPManager::FireProjectile(FAMP_ProjectileData& ProjectileData)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_FireProjectile)
	if (GetLocalRole() == ROLE_Authority)
	{
		// Prevent listen server from double spawning projectiles
		if (SpawnedProjectiles.Contains(ProjectileData.ProjectileID))
		{
			return nullptr;
		}
		ProjectileData.ProjectileID = FGuid::NewGuid();
		ProjectileData.bIsServerSpawned = true;
	}

	switch (LagCompensationMethod)
	{
		// case EAMP_LagCompensationMethod::PlayerPing:
		// 	if (GetLocalRole() == ROLE_Authority && ProjectileData.IsClientPredicted && ProjectileData.Owner)
		// 	{
		// 		ProjectileData.FiringClientDelay = GetServerPredictionTime(ProjectileData.Owner) * .0005f;
		// 	}
		// 	break;
		case EAMP_LagCompensationMethod::NetworkTimeSync:
			if (!NetworkTimeSubsystem)
			{
				UE_LOG(LogAMP, Error, TEXT("AAMP_AMPManager::FireProjectile: NetworkTimeSubsystem is null"));
				return nullptr;
			}
		
			if (GetLocalRole() == ROLE_Authority)
			{
				ProjectileData.ServerProjectileSpawnTime = NetworkTimeSubsystem->GetServerWorldTime();
			}
			break;
		default:
			break;
	}
	
	
	if (ProjectileData.ProjectileClass == nullptr)
	{
		UE_LOG(LogAMP, Warning, TEXT("AAMP_AMPManager::FireProjectile: ProjectileClass is null"));
		return nullptr;
	}

	// If the projectile is client predicted and the owner is the local player, and we're not showing server projectiles, don't spawn the projectile
	if (ProjectileData.IsClientPredicted &&
		ProjectileData.bIsServerSpawned &&
		!bShowServerProjectile &&
		(LocalPlayerController && (ProjectileData.Owner == LocalPlayerController || ProjectileData.Owner == LocalPlayerController->GetPawn())))
	{
		// UE_LOG(LogTemp, Warning, TEXT("UPP_PredProjectileFiringComponent::FireProjectile: Not spawning client predicted projectile for local player"));
		return nullptr;
	}

	SetProjectileDataDefaults(ProjectileData);


	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = ProjectileData.Owner;
	SpawnParams.Instigator = Cast<APawn>(ProjectileData.Owner);
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AActor* Projectile;
	
	if (bUseProjectilePool)
	{
		Projectile = GetProjectileFromPool(ProjectileData.ProjectileClass, ProjectileData.Owner);
		Projectile->SetActorTransform(GetSpawnTransform(ProjectileData));
	}
	else
	{
		const FTransform SpawnTransform = GetSpawnTransform(ProjectileData);
		Projectile = GetWorld()->SpawnActor(ProjectileData.ProjectileClass, &SpawnTransform, SpawnParams);
	}

	if (Projectile) // If the projectile was spawned successfully and not immediately destroyed
	{
		if (const TObjectPtr<UAMP_ProjectileMovementComponent> ProjectileComponent = Projectile->FindComponentByClass<UAMP_ProjectileMovementComponent>())
		{
			// Initialize the projectile
			ProjectileComponent->SetProjectileData(ProjectileData, this);
			SpawnedProjectiles.Add(ProjectileData.ProjectileID, ProjectileComponent);

			return ProjectileComponent;
		}
		UE_LOG(LogAMP, Warning, TEXT("AAMP_AMPManager::FireProjectile: Fired an actor that doesn't have a UAMP_ProjectileMovementComponent"));
	}return nullptr;
}

void AAMP_AMPManager::DestroyProjectile(UAMP_ProjectileMovementComponent* DestroyedProjectile)
{
	if (!DestroyedProjectile) return;

	// Check if the projectile is in the spawned projectiles list (aka hasn't been destroyed yet)
	if (!SpawnedProjectiles.Contains(DestroyedProjectile->GetProjectileID())) return;

	// Move projectile to pool or destroy
	if (bUseProjectilePool)
	{;
		ReturnProjectileToPool(DestroyedProjectile->GetOwner());
	}
	else
	{
		DestroyedProjectile->GetOwner()->Destroy();
	}

	// Notify clients to destroy the projectile
	if (HasAuthority() && DestroyedProjectile->NotifyClientsOnDestroy())
	{
		if (bUseAMP_NetRelevancy)
		{
			NetRelevant_DestroyProjectileSimProxy(DestroyedProjectile->GetProjectileID());
		}
		else
		{
			MultiCast_DestroyProjectileSimProxy(DestroyedProjectile->GetProjectileID());
		}
		SpawnedProjectiles.Remove(DestroyedProjectile->GetProjectileID());
	}
}


// Called every frame
void AAMP_AMPManager::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Process client queued projectiles
	ProcessQueuedProjectiles(DeltaTime);
	ProcessServerProjectilesBuffers(DeltaTime);
	
}

AActor* AAMP_AMPManager::GetProjectileFromPool(UClass* ProjectileClass, AActor* NewOwner)
{
	if (!bUseProjectilePool)
	{
		return nullptr;
	}
	
	AActor* Projectile;
	
	if (ProjectilePool.Contains(ProjectileClass))
	{
		Projectile = ProjectilePool[ProjectileClass].GetFromPool();
	}
	else
	{
		ProjectilePool.Add(ProjectileClass, FAMP_ProjectilePool(GetWorld(), ProjectileClass, 1));
		Projectile =  ProjectilePool[ProjectileClass].GetFromPool();
	}

	if (Projectile)
	{
		Projectile->SetOwner(NewOwner);
		return Projectile;
	}

	return nullptr;
}

bool AAMP_AMPManager::ReturnProjectileToPool(AActor* Projectile)
{
	if (!bUseProjectilePool || !Projectile){ return false; }

	if (ProjectilePool.Contains(Projectile->GetClass()))
	{
		ProjectilePool[Projectile->GetClass()].AddToPool(Projectile);
		return true;
	}

	return false;
}

void AAMP_AMPManager::AllocateStartingPools()
{
	for (auto Pair : StartingPoolSizes)
	{
		ProjectilePool.Add(Pair.Key, FAMP_ProjectilePool(GetWorld(), Pair.Key, Pair.Value));
		UE_LOG(LogAMP, Warning, TEXT("AAMP_AMPManager::AllocateStartingPools: Allocated %d %s"), Pair.Value, *Pair.Key->GetName());
	}
}

bool AAMP_AMPManager::IsPlayerControllerRelevantForProjectile(const TObjectPtr<AAMP_PlayerController> PC, const FVector ProjectileLocation)
{
	if (!PC){ return false;}
	if (!bUseAMP_NetRelevancy){ return true;}

	if (PC->GetPawn())
	{
		float Distance = FVector::Dist(PC->GetPawn()->GetActorLocation(), ProjectileLocation);
		// Always use spawn location from ProjectileData
		return Distance <= AMP_NetRelevancyRange;
	}
	return false;
}

FTransform AAMP_AMPManager::GetSpawnTransform(const FAMP_ProjectileData& ProjectileData)
{
	FTransform SpawnTransform;
	
	SpawnTransform.SetLocation(ProjectileData.Location);
	const FRotator Rotation = FRotator::MakeFromEuler(ProjectileData.Rotation);
	SpawnTransform.SetRotation(FQuat(Rotation));
	SpawnTransform.SetScale3D(ProjectileData.Scale);

	return SpawnTransform;
}

float AAMP_AMPManager::GetServerPredictionTime(AActor* Player) const
{
	// Only the server can determine the time between the firing client and the server
	if (!Player->HasAuthority())
	{
		return 0.0f;
	}

	const TObjectPtr<APawn> PlayerPawn = Cast<APawn>(Player);

	// Only worry about projectiles owned by players
	if (!IsValid(PlayerPawn)) return 0.0f;

	// Player Controller of the owning player
	const TObjectPtr<APlayerController> PlayerController = Cast<APlayerController>(PlayerPawn->GetController());

	// Issue with getting the player controller
	if (!IsValid(PlayerController)) return 0.0f;

	const TObjectPtr<APlayerState> PlayerState = PlayerController->PlayerState;

	// Issue with getting the player state
	if (!PlayerState) return 0.0f;

	return PlayerState->ExactPing;
}

