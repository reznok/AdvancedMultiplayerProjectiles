// Fill out your copyright notice in the Description page of Project Settings.


#include "AMP_BlueprintFunctionLibrary.h"

#include "EngineUtils.h"
#include "AdvancedMultiplayerProjectiles.h"

AAMP_AMPManager* UAMP_BlueprintFunctionLibrary::GetAMPManager(UObject* WorldContextObject)
{
	// One manager per world: the first one found (an if, not a one-iteration loop: clang -Wunreachable-code-loop-increment)
	TActorIterator<AAMP_AMPManager> ActorItr(WorldContextObject->GetWorld());
	if (ActorItr)
	{
		return *ActorItr;
	}
	UE_LOG(LogAMP, Error, TEXT("UAMP_BlueprintFunctionLibrary::GetAMPManager: No UAMP_PredictedProjectileManager found in the world"));
	return nullptr;
}
