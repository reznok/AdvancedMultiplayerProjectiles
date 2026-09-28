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

	// The manager is a single global actor that fans cosmetic sim-proxy spawns
	// out to clients via its NetMulticast RPCs. As an AInfo it has no world
	// location, so without bAlwaysRelevant a distant client could be culled and
	// never receive the multicast. Mark it always-relevant + replicated so the
	// multicast path (used when the project does not use AAMP_PlayerController —
	// see NetRelevant_SpawnProjectilesSimProxies) reliably reaches every client.
	bReplicates = true;
	bAlwaysRelevant = true;

	// Unreliable multicasts wait for the actor's next net update, and UE keeps only net.MaxRPCPerNetUpdate (default 2)
	// calls to one function per update, silently dropping the rest. AInfo's 10 Hz would hold several ticks' messages per
	// update and lose most of them under fire; at 100 Hz the manager replicates every server frame (dedicated servers tick
	// slower than that), and each tick sends one batch per message kind (ProcessServerProjectilesBuffers).
	SetNetUpdateFrequency(100.f);
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

	// Only Listen Servers and Clients need to know about the local player controller (resolved again on use: a client's
	// controller can arrive after the manager begins play)
	GetLocalPlayerController();

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

	// Per-client, distance-culled delivery via AMP's own PlayerController RPC.
	// This only reaches controllers that derive from AAMP_PlayerController, so we
	// iterate the base APlayerController and cast — projects that subclass the AMP
	// controller keep the culled per-client path.
	int32 NumAMPControllers = 0;
	for (TActorIterator<APlayerController> ActorItr(GetWorld()); ActorItr; ++ActorItr)
	{
		AAMP_PlayerController* PlayerController = Cast<AAMP_PlayerController>(*ActorItr);
		if (!PlayerController) continue;
		++NumAMPControllers;

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

	// Fallback for projects that do NOT use AAMP_PlayerController (e.g. a GMC-based
	// controller): the per-client RPC above reaches nobody, so the cosmetic
	// projectile would never appear on any client. Deliver it class-agnostically
	// via the always-relevant manager's NetMulticast so every client — including
	// the firer — spawns the sim-proxy copy. Distance culling is not applied on
	// this path (identical to bUseAMP_NetRelevancy == false).
	if (NumAMPControllers == 0)
	{
		MultiCast_SpawnProjectilesSimProxies(ProjectileData);
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

TArray<UAMP_ProjectileMovementComponent*> AAMP_AMPManager::SpawnLocalPredictedProjectiles(TArray<FAMP_ProjectileData>& ProjectilesData)
{
	TArray<UAMP_ProjectileMovementComponent*> Spawned;
	if (GetLocalRole() == ROLE_Authority)
	{
		UE_LOG(LogAMP, Warning, TEXT("AAMP_AMPManager::SpawnLocalPredictedProjectiles: client only (the server spawns its own copy)"));
		return Spawned;
	}
	const float Now = NetworkTimeSubsystem ? NetworkTimeSubsystem->GetServerWorldTime() : 0.f;
	for (FAMP_ProjectileData& Data : ProjectilesData)
	{
		Data.IsClientPredicted = true;
		Data.bIsServerSpawned = false;
		Data.bIsLocallyFired = true;
		Data.ClientProjectileSpawnTime = Now;
		// Local id: never matches a server id, and keeps every local copy distinct in SpawnedProjectiles
		Data.ProjectileID = FGuid::NewGuid();
		if (UAMP_ProjectileMovementComponent* ProjectileCmp = FireProjectile(Data))
		{
			if (Data.PredictionKey != 0)
			{
				// A live key spawned again is a caller bug (e.g. spawning on a replayed move)
				ensureMsgf(!PredictedProjectiles.Contains(Data.PredictionKey),
					TEXT("AAMP_AMPManager::SpawnLocalPredictedProjectiles: prediction key %lld is already live"), Data.PredictionKey);
				PredictedProjectiles.Add(Data.PredictionKey, ProjectileCmp);
			}
			Spawned.Add(ProjectileCmp);
		}
	}
	return Spawned;
}

void AAMP_AMPManager::MultiCast_EndProjectilesSimProxies_Implementation(const TArray<FAMP_ProjectileEnd>& Ends)
{
	// The server already ended them
	if (GetLocalRole() == ROLE_Authority) return;

	for (const FAMP_ProjectileEnd& End : Ends)
	{
		EndProjectileSimProxy(End);
	}
}

void AAMP_AMPManager::NetRelevant_EndProjectilesSimProxies(const TArray<FAMP_ProjectileEnd>& Ends)
{
	if (GetLocalRole() != ROLE_Authority) return;

	// Mirror of NetRelevant_SpawnProjectilesSimProxies: per-client culled delivery for AAMP_PlayerController, with a
	// class-agnostic NetMulticast fallback when the project uses a different controller base
	int32 NumAMPControllers = 0;
	for (TActorIterator<APlayerController> ActorItr(GetWorld()); ActorItr; ++ActorItr)
	{
		AAMP_PlayerController* PlayerController = Cast<AAMP_PlayerController>(*ActorItr);
		if (!PlayerController) continue;
		++NumAMPControllers;

		TArray<FAMP_ProjectileEnd> RelevantEnds;
		for (const FAMP_ProjectileEnd& End : Ends)
		{
			if (IsPlayerControllerRelevantForProjectile(PlayerController, End.Location))
			{
				RelevantEnds.Add(End);
			}
		}
		if (RelevantEnds.Num() > 0)
		{
			PlayerController->ClientRPC_EndProjectilesSimProxies(RelevantEnds);
		}
	}

	if (NumAMPControllers == 0)
	{
		MultiCast_EndProjectilesSimProxies(Ends);
	}
}

void AAMP_AMPManager::EndProjectileSimProxy(const FAMP_ProjectileEnd& End)
{
	// Projectile is already ended on the server
	if (GetLocalRole() == ROLE_Authority) return;

	// Its spawn may have arrived in the same bunch and still wait in the queue: fire it first (the firing client skips it),
	// so it ends here instead of flying on after this message
	FireQueuedProjectile(End.ProjectileID);
	UAMP_ProjectileMovementComponent* ProjectileComponent = FindProjectile(End.ProjectileID, End.PredictionKey);
	if (ProjectileComponent)
	{
		ProjectileComponent->ApplyEnd(End);
	}
}

UAMP_ProjectileMovementComponent* AAMP_AMPManager::FireQueuedProjectile(const FGuid& ProjectileID)
{
	if (!ProjectileID.IsValid()) return nullptr;

	const int32 Index = QueuedProjectiles.IndexOfByPredicate([&ProjectileID](const TTuple<float, FAMP_ProjectileData>& Queued)
	{
		return Queued.Get<1>().ProjectileID == ProjectileID;
	});
	if (Index == INDEX_NONE) return nullptr;

	FAMP_ProjectileData Data = QueuedProjectiles[Index].Get<1>();
	QueuedProjectiles.RemoveAt(Index);
	return FireProjectile(Data);
}

UAMP_ProjectileMovementComponent* AAMP_AMPManager::FindProjectile(const FGuid& ProjectileID, const int64 PredictionKey) const
{
	if (const TObjectPtr<UAMP_ProjectileMovementComponent>* ById = SpawnedProjectiles.Find(ProjectileID))
	{
		return *ById;
	}
	if (PredictionKey != 0)
	{
		if (const TObjectPtr<UAMP_ProjectileMovementComponent>* ByKey = PredictedProjectiles.Find(PredictionKey))
		{
			return *ByKey;
		}
	}
	return nullptr;
}

APlayerController* AAMP_AMPManager::GetLocalPlayerController()
{
	if (!LocalPlayerController && (GetNetMode() == NM_Client || GetNetMode() == NM_ListenServer))
	{
		LocalPlayerController = UGameplayStatics::GetPlayerController(GetWorld(), 0);
	}
	return LocalPlayerController;
}

void AAMP_AMPManager::RedirectProjectile(UAMP_ProjectileMovementComponent* Projectile, const FVector& NewVelocity)
{
	if (!Projectile || GetLocalRole() != ROLE_Authority) return;

	Projectile->SetFlightVelocity(NewVelocity);

	FAMP_ProjectileRedirect Redirect;
	Redirect.ProjectileID = Projectile->GetProjectileID();
	Redirect.PredictionKey = Projectile->GetPredictionKey();
	Redirect.Location = Projectile->GetOwner()->GetActorLocation();
	Redirect.Velocity = NewVelocity;
	Redirect.ServerTime = NetworkTimeSubsystem ? NetworkTimeSubsystem->GetServerWorldTime() : 0.f;

	// Sent with the tick's batch, after its spawns (a projectile spawned this tick exists on clients first)
	ServerRedirectBuffer.Add(Redirect);
}

void AAMP_AMPManager::MultiCast_RedirectProjectilesSimProxies_Implementation(const TArray<FAMP_ProjectileRedirect>& Redirects)
{
	if (GetLocalRole() == ROLE_Authority) return;

	for (const FAMP_ProjectileRedirect& Redirect : Redirects)
	{
		RedirectProjectileSimProxy(Redirect);
	}
}

void AAMP_AMPManager::NetRelevant_RedirectProjectilesSimProxies(const TArray<FAMP_ProjectileRedirect>& Redirects)
{
	if (GetLocalRole() != ROLE_Authority) return;

	int32 NumAMPControllers = 0;
	for (TActorIterator<APlayerController> ActorItr(GetWorld()); ActorItr; ++ActorItr)
	{
		AAMP_PlayerController* PlayerController = Cast<AAMP_PlayerController>(*ActorItr);
		if (!PlayerController) continue;
		++NumAMPControllers;

		TArray<FAMP_ProjectileRedirect> RelevantRedirects;
		for (const FAMP_ProjectileRedirect& Redirect : Redirects)
		{
			if (IsPlayerControllerRelevantForProjectile(PlayerController, Redirect.Location))
			{
				RelevantRedirects.Add(Redirect);
			}
		}
		if (RelevantRedirects.Num() > 0)
		{
			PlayerController->ClientRPC_RedirectProjectilesSimProxies(RelevantRedirects);
		}
	}

	if (NumAMPControllers == 0)
	{
		MultiCast_RedirectProjectilesSimProxies(Redirects);
	}
}

void AAMP_AMPManager::RedirectProjectileSimProxy(const FAMP_ProjectileRedirect& Redirect)
{
	if (GetLocalRole() == ROLE_Authority) return;

	// Its spawn may have arrived in the same bunch and still wait in the queue: fire it first (the firing client skips it),
	// so it still turns here
	FireQueuedProjectile(Redirect.ProjectileID);
	if (UAMP_ProjectileMovementComponent* ProjectileComponent = FindProjectile(Redirect.ProjectileID, Redirect.PredictionKey))
	{
		ProjectileComponent->ApplyRedirect(Redirect);
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

	// One call per message kind per tick, never one per projectile: UE keeps at most net.MaxRPCPerNetUpdate (default 2)
	// calls to one unreliable multicast per net update and silently drops the rest. Order matters: a projectile spawned
	// and redirected or ended within one tick reaches clients spawn first (the client fires a queued spawn early when its
	// end or redirect arrives in the same bunch: FireQueuedProjectile).
	const bool bSending = ServerProjectilesBuffer.Num() > 0 || ServerRedirectBuffer.Num() > 0 || ServerEndBuffer.Num() > 0;

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

	if (ServerRedirectBuffer.Num() > 0)
	{
		if (bUseAMP_NetRelevancy)
		{
			NetRelevant_RedirectProjectilesSimProxies(ServerRedirectBuffer);
		}
		else
		{
			MultiCast_RedirectProjectilesSimProxies(ServerRedirectBuffer);
		}

		ServerRedirectBuffer.Empty();
	}

	if (ServerEndBuffer.Num() > 0)
	{
		if (bUseAMP_NetRelevancy)
		{
			NetRelevant_EndProjectilesSimProxies(ServerEndBuffer);
		}
		else
		{
			MultiCast_EndProjectilesSimProxies(ServerEndBuffer);
		}

		ServerEndBuffer.Empty();
	}

	if (bSending)
	{
		// Replicate this frame even when the server ticks faster than NetUpdateFrequency (PIE, listen servers): queued
		// multicasts leave with the next net update, and a second tick's batch waiting for the same update could exceed
		// the per-update limit again.
		ForceNetUpdate();
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

	// If the projectile is client predicted and the owner is the local player, and we're not showing server projectiles, don't spawn the projectile.
	// The firing client also knows its shots by their live predicted copy: its pawn may already be gone (dead, unpossessed)
	// while its shots still fly, and a second copy here would take the server's end message (found by id first) while the
	// predicted copy flew on.
	APlayerController* LocalPC = GetLocalPlayerController();
	const bool bLocalPrediction = ProjectileData.PredictionKey != 0 && PredictedProjectiles.Contains(ProjectileData.PredictionKey);
	if (ProjectileData.IsClientPredicted &&
		ProjectileData.bIsServerSpawned &&
		!bShowServerProjectile &&
		(bLocalPrediction || (LocalPC && (ProjectileData.Owner == LocalPC || ProjectileData.Owner == LocalPC->GetPawn()))))
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

bool AAMP_AMPManager::DestroyProjectile(UAMP_ProjectileMovementComponent* DestroyedProjectile)
{
	if (!DestroyedProjectile) return false;

	// Not in the list: already destroyed (e.g. a client copy that ended locally before the server's message arrived)
	const FGuid ProjectileID = DestroyedProjectile->GetProjectileID();
	if (!SpawnedProjectiles.Contains(ProjectileID)) return false;

	// Read everything before pooling resets the component
	FAMP_ProjectileEnd End;
	End.ProjectileID = ProjectileID;
	End.PredictionKey = DestroyedProjectile->GetPredictionKey();
	End.Location = DestroyedProjectile->GetEndLocation();
	End.EndCode = DestroyedProjectile->GetEndCode();
	const bool bNotifyClients = HasAuthority() && DestroyedProjectile->NotifyClientsOnDestroy();

	// Every machine forgets the id, so a late message for it can never reach this pooled actor's next flight
	SpawnedProjectiles.Remove(ProjectileID);
	if (End.PredictionKey != 0 && !HasAuthority() && PredictedProjectiles.FindRef(End.PredictionKey) == DestroyedProjectile)
	{
		PredictedProjectiles.Remove(End.PredictionKey);
	}

	// Move projectile to pool or destroy
	if (bUseProjectilePool)
	{
		ReturnProjectileToPool(DestroyedProjectile->GetOwner());
	}
	else
	{
		DestroyedProjectile->GetOwner()->Destroy();
	}

	if (bNotifyClients)
	{
		// Sent with the tick's batch, after its spawns and redirects (ProcessServerProjectilesBuffers)
		ServerEndBuffer.Add(End);
	}
	return true;
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

