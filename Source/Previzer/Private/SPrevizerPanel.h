// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

/** Contents of the Previzer dockable tab. Buttons only; all logic lives in FPrevizerActions. */
class SPrevizerPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SPrevizerPanel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	FString GetSceneName() const;
	void CreateScene();

	TSharedPtr<class SEditableTextBox> SceneNameBox;
};
