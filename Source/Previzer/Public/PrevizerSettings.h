// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "PrevizerSettings.generated.h"

class UControlRig;
class USkeletalMesh;

/** Asset references used by Previzer tools. Soft references so the plugin compiles and loads without the assets. */
UCLASS(config = Editor, defaultconfig, meta = (DisplayName = "Previzer"))
class PREVIZER_API UPrevizerSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UPrevizerSettings();

	/** Skeletal mesh spawned by "Add Character", added to new previs scenes, and listed in the Place Actors panel. */
	UPROPERTY(config, EditAnywhere, Category = "Characters")
	TSoftObjectPtr<USkeletalMesh> CharacterMesh;

	/** Control Rig added to the character's binding in new previs scenes' Level Sequence, for posing. */
	UPROPERTY(config, EditAnywhere, Category = "Characters")
	TSoftClassPtr<UControlRig> CharacterControlRig;

	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }
};
