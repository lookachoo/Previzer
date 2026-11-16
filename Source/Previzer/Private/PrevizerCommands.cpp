// Copyright Epic Games, Inc. All Rights Reserved.

#include "PrevizerCommands.h"

#define LOCTEXT_NAMESPACE "FPrevizerModule"

void FPrevizerCommands::RegisterCommands()
{
	UI_COMMAND(OpenPluginWindow, "Previzer", "Bring up Previzer window", EUserInterfaceActionType::Button, FInputChord());
}

#undef LOCTEXT_NAMESPACE
