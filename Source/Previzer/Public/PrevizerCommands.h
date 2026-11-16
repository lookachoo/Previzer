// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Framework/Commands/Commands.h"
#include "PrevizerStyle.h"

class FPrevizerCommands : public TCommands<FPrevizerCommands>
{
public:

	FPrevizerCommands()
		: TCommands<FPrevizerCommands>(TEXT("Previzer"), NSLOCTEXT("Contexts", "Previzer", "Previzer Plugin"), NAME_None, FPrevizerStyle::GetStyleSetName())
	{
	}

	// TCommands<> interface
	virtual void RegisterCommands() override;

public:
	TSharedPtr< FUICommandInfo > OpenPluginWindow;
};