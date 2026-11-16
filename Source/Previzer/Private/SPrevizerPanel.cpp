// Copyright Epic Games, Inc. All Rights Reserved.

#include "SPrevizerPanel.h"
#include "PrevizerActions.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SPrevizerPanel"

namespace
{
	TSharedRef<SWidget> MakeActionButton(const FText& Label, const FText& Tooltip, TFunction<void()> Action)
	{
		return SNew(SButton)
			.Text(Label)
			.ToolTipText(Tooltip)
			.HAlign(HAlign_Center)
			.OnClicked_Lambda([Action]()
			{
				Action();
				return FReply::Handled();
			});
	}

	/** One button per resolution in a row. TooltipFormat gets the resolution label as {0}. */
	TSharedRef<SWidget> MakeResolutionRow(TFunction<void(EPrevizerResolution)> Action, const FText& TooltipFormat)
	{
		const TPair<EPrevizerResolution, FText> Options[] =
		{
			{ EPrevizerResolution::HD1080,  LOCTEXT("Res1080", "1080p") },
			{ EPrevizerResolution::QHD1440, LOCTEXT("Res2K", "2K") },
			{ EPrevizerResolution::UHD2160, LOCTEXT("Res4K", "4K") },
		};

		TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
		for (const TPair<EPrevizerResolution, FText>& Option : Options)
		{
			const EPrevizerResolution Resolution = Option.Key;
			const FIntPoint Size = FPrevizerActions::GetResolutionSize(Resolution);
			const FText SizeText = FText::Format(LOCTEXT("ResSize", "{0} ({1}x{2})"), Option.Value, Size.X, Size.Y);

			Row->AddSlot()
			.FillWidth(1.0f)
			.Padding(0.0f, 0.0f, 2.0f, 0.0f)
			[
				MakeActionButton(Option.Value, FText::Format(TooltipFormat, SizeText), [Action, Resolution]() { Action(Resolution); })
			];
		}
		return Row;
	}
}

void SPrevizerPanel::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SNew(SScrollBox)
		+ SScrollBox::Slot()
		.Padding(8.0f)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 4.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("SceneHeader", "Scene"))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 2.0f)
			[
				SAssignNew(SceneNameBox, SEditableTextBox)
				.HintText(LOCTEXT("SceneNameHint", "Add a scene name"))
				.OnTextCommitted_Lambda([this](const FText&, ETextCommit::Type CommitType)
				{
					if (CommitType == ETextCommit::OnEnter && !GetSceneName().IsEmpty())
					{
						CreateScene();
					}
				})
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 2.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("NewPrevisScene", "New Previs Scene"))
				.ToolTipText(LOCTEXT("NewPrevisSceneTooltip", "Create a new scene with lighting, a character, a camera and a Level Sequence"))
				.HAlign(HAlign_Center)
				.IsEnabled_Lambda([this]() { return !GetSceneName().IsEmpty(); })
				.OnClicked_Lambda([this]()
				{
					CreateScene();
					return FReply::Handled();
				})
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 12.0f, 0.0f, 4.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("CharactersHeader", "Characters"))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 2.0f)
			[
				MakeActionButton(
					LOCTEXT("AddCharacter", "Add Character"),
					LOCTEXT("AddCharacterTooltip", "Add another character at 0,0,0. You can also drag one in from Place Actors > Previzer."),
					[]() { FPrevizerActions::SpawnCharacter(); })
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 2.0f)
			[
				MakeActionButton(
					LOCTEXT("OpenPlaceActors", "Open Place Actors"),
					LOCTEXT("OpenPlaceActorsTooltip", "Open the Place Actors panel to drag characters, lights, shapes and cameras into the level"),
					&FPrevizerActions::OpenPlaceActors)
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 12.0f, 0.0f, 4.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("EditModeHeader", "Edit Mode"))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 2.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.Padding(0.0f, 0.0f, 2.0f, 0.0f)
				[
					MakeActionButton(
						LOCTEXT("SelectMode", "Select"),
						LOCTEXT("SelectModeTooltip", "Back to the normal mode for selecting and moving things"),
						&FPrevizerActions::EnterSelectMode)
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					MakeActionButton(
						LOCTEXT("LandscapeMode", "Landscape"),
						LOCTEXT("LandscapeModeTooltip", "Sculpt and paint the ground (Landscape Mode)"),
						&FPrevizerActions::EnterLandscapeMode)
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 12.0f, 0.0f, 4.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("CameraHeader", "Camera"))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 2.0f)
			[
				SNew(SCheckBox)
				.ToolTipText(LOCTEXT("PreviewCamerasTooltip", "Show a small picture-in-picture view through a camera when it's selected"))
				.IsChecked_Lambda([]() { return FPrevizerActions::IsPreviewSelectedCamerasEnabled() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([](ECheckBoxState NewState) { FPrevizerActions::SetPreviewSelectedCamerasEnabled(NewState == ECheckBoxState::Checked); })
				[
					SNew(STextBlock)
					.Text(LOCTEXT("PreviewCameras", "Preview Selected Camera"))
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 12.0f, 0.0f, 4.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("RenderHeader", "Render"))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 2.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("ScreenshotLabel", "Still frame (High Res Screenshot)"))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 2.0f)
			[
				MakeResolutionRow([](EPrevizerResolution Resolution) { FPrevizerActions::TakeHighResScreenshot(Resolution); },
					LOCTEXT("ScreenshotTooltip", "Save a {0} screenshot of the camera view to Saved/Screenshots"))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 8.0f, 0.0f, 2.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("MovieLabel", "Movie (Movie Render Queue)"))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 2.0f)
			[
				MakeResolutionRow([](EPrevizerResolution Resolution) { FPrevizerActions::QueueMovieRender(Resolution); },
					LOCTEXT("MovieTooltip", "Add this scene's Level Sequence to Movie Render Queue at {0} and open it"))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 2.0f)
			[
				MakeActionButton(
					LOCTEXT("OpenMRQ", "Open Movie Render Queue"),
					LOCTEXT("OpenMRQTooltip", "Open the Movie Render Queue window"),
					&FPrevizerActions::OpenMovieRenderQueue)
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 8.0f, 0.0f, 2.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.Padding(0.0f, 0.0f, 2.0f, 0.0f)
				[
					MakeActionButton(
						LOCTEXT("OpenScreenshots", "Screenshots Folder"),
						LOCTEXT("OpenScreenshotsTooltip", "Open the folder your High Res Screenshots are saved in"),
						&FPrevizerActions::OpenScreenshotsFolder)
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					MakeActionButton(
						LOCTEXT("OpenRenders", "Renders Folder"),
						LOCTEXT("OpenRendersTooltip", "Open the folder Movie Render Queue saves renders to"),
						&FPrevizerActions::OpenRendersFolder)
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 12.0f, 0.0f, 4.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("ContentHeader", "Content"))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 2.0f)
			[
				MakeActionButton(
					LOCTEXT("OpenFabWebsite", "Open Fab"),
					LOCTEXT("OpenFabWebsiteTooltip", "Open https://www.fab.com in your web browser"),
					&FPrevizerActions::OpenFabWebsite)
			]
		]
	];
}

FString SPrevizerPanel::GetSceneName() const
{
	return SceneNameBox.IsValid() ? SceneNameBox->GetText().ToString().TrimStartAndEnd() : FString();
}

void SPrevizerPanel::CreateScene()
{
	if (FPrevizerActions::CreateNewPrevisScene(GetSceneName()))
	{
		SceneNameBox->SetText(FText::GetEmpty());
	}
}

#undef LOCTEXT_NAMESPACE
