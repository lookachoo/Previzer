// Copyright Epic Games, Inc. All Rights Reserved.

#include "PrevizerSettings.h"
#include "ControlRig.h"
#include "Engine/SkeletalMesh.h"

UPrevizerSettings::UPrevizerSettings()
{
	CharacterMesh = TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(TEXT("/Previzer/Content/ControlRig/Characters/Mannequins/Meshes/SKM_Manny.SKM_Manny")));
	CharacterControlRig =TSoftClassPtr<UControlRig>(FSoftObjectPath(TEXT("/Previzer/Content/ControlRig/Characters/Mannequins/Rigs/CR_Mannequin_Body.CR_Mannequin_Body_C")));
}
