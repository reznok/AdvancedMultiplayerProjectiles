// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Actors/AMP_AMPManager.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "AMP_BlueprintFunctionLibrary.generated.h"

/**
 * 
 */
UCLASS()
class ADVANCEDMULTIPLAYERPROJECTILES_API UAMP_BlueprintFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

	// Get the AMP manager
	// This is a singleton that can be used to spawn predicted projectiles
	// It's recommended to store a reference to the manager in a variable for access
public:
	UFUNCTION(BlueprintPure, Category = "Predicted Projectiles", meta = (WorldContext = "WorldContextObject"))
	static AAMP_AMPManager* GetAMPManager(UObject* WorldContextObject);
};
