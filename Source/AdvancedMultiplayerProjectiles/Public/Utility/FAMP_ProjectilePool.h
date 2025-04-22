#pragma once
#include "CoreMinimal.h"
#include "AdvancedMultiplayerProjectiles.h"
#include "Components/AMP_ProjectileMovementComponent.h"

#include "FAMP_ProjectilePool.generated.h"

/**
 * 
 */
USTRUCT()
struct ADVANCEDMULTIPLAYERPROJECTILES_API FAMP_ProjectilePool
{
	GENERATED_BODY()
	
	TArray<TObjectPtr<AActor>> Pool{};

	FAMP_ProjectilePool() = default;

	FAMP_ProjectilePool(UWorld* _World, UClass* _ProjectileClass, int32 StartingPoolSize = 0)
	{
		World = _World;
		ProjectileClass = _ProjectileClass;

		if (World == nullptr || ProjectileClass == nullptr)
		{
			UE_LOG(LogAMP, Error, TEXT("FAMP_ProjectilePool::FAMP_ProjectilePool: World or ProjectileClass is nullptr"));
			return;
		}

		for (int32 i = 0; i < StartingPoolSize; i++)
		{
			Pool.Add(CreatePoolObject());
		}
	}
	
	UPROPERTY()
	TObjectPtr<UWorld> World{};

	UPROPERTY()
	TObjectPtr<UClass> ProjectileClass{};

	void AddToPool(const TObjectPtr<AActor>& Projectile)
	{
		DeactivateActor(Projectile);
		
		if (Pool.Contains(Projectile))
		{
			// UE_LOG(LogAMP, Warning, TEXT("FAMP_ProjectilePool::AddToPool: Projectile is already in the pool"));
			return;
		}
		
		// UE_LOG(LogTemp, Warning, TEXT("UPP_PredProjectileMovementComponent::TickComponent: Projectile returned to pool"));
		Pool.Add(Projectile);
	}

	TObjectPtr<AActor> GetFromPool()
	{
		if (Pool.IsEmpty())
		{
			TObjectPtr<AActor> Projectile = CreatePoolObject();
			ActivateActor(Projectile);
			return Projectile;
		}
		
		TObjectPtr<AActor> Projectile = Pool.Pop();
		ActivateActor(Projectile);
		return Projectile;
	}

	TObjectPtr<AActor> CreatePoolObject() const
	{
		TObjectPtr<AActor> SpawnedActor = World->SpawnActor(ProjectileClass);
		DeactivateActor(SpawnedActor);

		// (LogPredictedProjectiles, Warning, TEXT("FPP_ProjectilePool::CreatePoolObject: Created a new projectile in the pool"));
		return SpawnedActor;
	}

	static void ActivateActor(const TObjectPtr<AActor>& Actor)
	{		
		const TObjectPtr<UAMP_ProjectileMovementComponent> MovementComponent = Actor->FindComponentByClass<UAMP_ProjectileMovementComponent>();
		if (MovementComponent)
		{
			MovementComponent->SetComponentTickEnabled(true);
		}
	}

	static void DeactivateActor(const TObjectPtr<AActor>& Actor)
	{
		Actor->SetActorHiddenInGame(true);
		Actor->SetActorEnableCollision(false);
		Actor->SetActorTickEnabled(false);
		const TObjectPtr<UAMP_ProjectileMovementComponent> MovementComponent = Actor->FindComponentByClass<UAMP_ProjectileMovementComponent>();
		if (MovementComponent)
		{
			MovementComponent->SetComponentTickEnabled(false);
			MovementComponent->ResetProjectile();
		}
	}
};