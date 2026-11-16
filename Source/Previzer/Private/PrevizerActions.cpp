// Copyright Epic Games, Inc. All Rights Reserved.

#include "PrevizerActions.h"
#include "ActorEditorUtils.h"
#include "Animation/SkeletalMeshActor.h"
#include "EditMode/ControlRigEditMode.h"
#include "EditorModeManager.h"
#include "EngineUtils.h"
#include "Framework/Docking/TabManager.h"
#include "Graph/MovieGraphNamedResolution.h"
#include "EditorModes.h"
#include "HighResScreenshot.h"
#include "Landscape.h"
#include "LandscapeInfo.h"
#include "Settings/LevelEditorViewportSettings.h"
#include "MoviePipelineBasicConfig.h"
#include "MoviePipelineEditorBlueprintLibrary.h"
#include "MoviePipelineQueue.h"
#include "MoviePipelineQueueSubsystem.h"
#include "ILevelEditor.h"
#include "LevelEditor.h"
#include "LevelEditorViewport.h"
#include "LevelViewportActions.h"
#include "MovieScene.h"
#include "MovieSceneToolHelpers.h"
#include "SLevelViewport.h"
#include "TimerManager.h"
#include "AssetToolsModule.h"
#include "CineCameraActor.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "ControlRig.h"
#include "ControlRigSequencerEditorLibrary.h"
#include "Editor.h"
#include "Engine/DirectionalLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkyLight.h"
#include "Factories/Factory.h"
#include "FileHelpers.h"
#include "Framework/Notifications/NotificationManager.h"
#include "HAL/PlatformProcess.h"
#include "LevelEditorSubsystem.h"
#include "LevelSequence.h"
#include "LevelSequenceActor.h"
#include "LevelSequenceEditorSubsystem.h"
#include "PrevizerSettings.h"
#include "ScopedTransaction.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Subsystems/EditorActorSubsystem.h"
#include "Widgets/Notifications/SNotificationList.h"

DEFINE_LOG_CATEGORY(LogPrevizer);

#define LOCTEXT_NAMESPACE "PrevizerActions"

namespace PrevizerActions
{
	static const TCHAR* FabUrl = TEXT("https://www.fab.com");

	// Scenes are user content, so they live in the project, never in the plugin. Each scene gets its own folder.
	static const TCHAR* SceneRootFolder = TEXT("/Game/Scenes");

	// Epic's recommended asset naming prefixes.
	static const TCHAR* LevelPrefix = TEXT("L_");
	static const TCHAR* LevelSequencePrefix = TEXT("LS_");

	// ULevelSequenceFactoryNew is not exported from LevelSequenceEditor in UE 5.8 (header is Private), so load it by name.
	static const TCHAR* LevelSequenceFactoryClassPath = TEXT("/Script/LevelSequenceEditor.LevelSequenceFactoryNew");

	static const TCHAR* CharacterActorLabel = TEXT("Character");

	static const FVector DefaultCameraLocation(-500.0, 0.0, 150.0);
	static const FRotator DefaultSunRotation(-45.0, -30.0, 0.0);

	template<typename ActorType>
	static ActorType* SpawnAtOrigin(const FRotator& Rotation = FRotator::ZeroRotator)
	{
		UEditorActorSubsystem* ActorSubsystem = GEditor->GetEditorSubsystem<UEditorActorSubsystem>();
		return Cast<ActorType>(ActorSubsystem->SpawnActorFromClass(ActorType::StaticClass(), FVector::ZeroVector, Rotation));
	}

	static void Notify(const FText& Message, bool bSuccess)
	{
		FNotificationInfo Info(Message);
		Info.ExpireDuration = bSuccess ? 4.0f : 8.0f;
		TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info);
		if (Item.IsValid())
		{
			Item->SetCompletionState(bSuccess ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
		}
	}

	static bool Fail(const FText& Message)
	{
		UE_LOG(LogPrevizer, Error, TEXT("%s"), *Message.ToString());
		Notify(Message, false);
		return false;
	}

	static ULevelSequence* CreateLevelSequenceAsset(const FString& PackagePath, const FString& AssetName)
	{
		UClass* FactoryClass = LoadClass<UFactory>(nullptr, LevelSequenceFactoryClassPath);
		if (!FactoryClass)
		{
			UE_LOG(LogPrevizer, Error, TEXT("Could not find factory class %s."), LevelSequenceFactoryClassPath);
			return nullptr;
		}

		UFactory* Factory = NewObject<UFactory>(GetTransientPackage(), FactoryClass);
		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
		return Cast<ULevelSequence>(AssetTools.CreateAsset(AssetName, PackagePath, ULevelSequence::StaticClass(), Factory));
	}

	/** Adds a Control Rig track (UPrevizerSettings::CharacterControlRig) to the character's binding so it can be posed in Sequencer. */
	static void AddCharacterControlRig(ULevelSequence* Sequence, const FMovieSceneBindingProxy& CharacterBinding)
	{
		const TSoftClassPtr<UControlRig>& RigPath = GetDefault<UPrevizerSettings>()->CharacterControlRig;
		UClass* RigClass = RigPath.LoadSynchronous();
		if (!RigClass)
		{
			UE_LOG(LogPrevizer, Warning, TEXT("Character Control Rig not found at '%s'; the character was added to the sequence without one. Set it in Project Settings > Plugins > Previzer."), *RigPath.ToString());
			return;
		}

		UWorld* World = GEditor->GetEditorWorldContext().World();
		if (!UControlRigSequencerEditorLibrary::FindOrCreateControlRigTrack(World, Sequence, RigClass, CharacterBinding))
		{
			UE_LOG(LogPrevizer, Warning, TEXT("Failed to add Control Rig %s to the character's binding."), *RigClass->GetName());
		}
	}

	/** Opens Folder in the OS file browser, creating it first so there is something to open before the first capture. */
	static void ExploreFolder(const FString& Folder)
	{
		const FString FullPath = FPaths::ConvertRelativePathToFull(Folder);
		IFileManager::Get().MakeDirectory(*FullPath, /*Tree*/ true);
		FPlatformProcess::ExploreFolder(*FullPath);
	}

	/** The viewport looking through a Cine Camera (left pane of the previs layout), falling back to the active viewport. */
	static FLevelEditorViewportClient* FindCameraViewportClient()
	{
		FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>("LevelEditor");
		TSharedPtr<ILevelEditor> LevelEditor = LevelEditorModule.GetFirstLevelEditor();
		if (!LevelEditor.IsValid())
		{
			return nullptr;
		}

		for (const TSharedPtr<SLevelViewport>& Viewport : LevelEditor->GetViewports())
		{
			if (Viewport.IsValid() && Cast<ACineCameraActor>(Viewport->GetLevelViewportClient().GetActiveActorLock().Get()))
			{
				return &Viewport->GetLevelViewportClient();
			}
		}

		TSharedPtr<SLevelViewport> ActiveViewport = LevelEditor->GetActiveViewportInterface();
		return ActiveViewport.IsValid() ? &ActiveViewport->GetLevelViewportClient() : nullptr;
	}

	/** Movie Render Queue's own named profiles (MovieGraphProjectSettings.cpp, UE 5.8) so the UI shows them by name. */
	static FMovieGraphNamedResolution GetMovieRenderResolution(EPrevizerResolution Resolution)
	{
		const FIntPoint Size = FPrevizerActions::GetResolutionSize(Resolution);
		switch (Resolution)
		{
		case EPrevizerResolution::QHD1440: return FMovieGraphNamedResolution(TEXT("1440p (QHD)"), Size, TEXT("Quad high definition"));
		case EPrevizerResolution::UHD2160: return FMovieGraphNamedResolution(TEXT("2160p (4K UHD)"), Size, TEXT("Ultra high definition (4k)"));
		case EPrevizerResolution::HD1080:
		default:                           return FMovieGraphNamedResolution(TEXT("1080p (FHD)"), Size, TEXT("Full high definition"));
		}
	}

	static const FVector DefaultPerspectiveLocation(-600.0, 600.0, 400.0);

	/** Left pane looks through Camera (and follows Sequencer camera cuts); right pane is a free perspective view. */
	static void AssignPrevisViewports(TWeakObjectPtr<ACineCameraActor> WeakCamera)
	{
		FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>("LevelEditor");
		TSharedPtr<ILevelEditor> LevelEditor = LevelEditorModule.GetFirstLevelEditor();
		if (!LevelEditor.IsValid())
		{
			return;
		}

		for (const TSharedPtr<SLevelViewport>& Viewport : LevelEditor->GetViewports())
		{
			if (!Viewport.IsValid())
			{
				continue;
			}

			// FEditorViewportLayoutTwoPanes names its panes "<Layout>.Viewport0" (left) and "<Layout>.Viewport1" (right).
			const FString ConfigKey = Viewport->GetConfigKey().ToString();
			const bool bIsLeft = ConfigKey.EndsWith(TEXT(".Viewport0"));
			const bool bIsRight = ConfigKey.EndsWith(TEXT(".Viewport1"));
			if (!bIsLeft && !bIsRight)
			{
				continue;
			}

			FLevelEditorViewportClient& Client = Viewport->GetLevelViewportClient();
			Client.SetViewportType(LVT_Perspective);
			Client.SetAllowCinematicControl(bIsLeft);

			if (bIsLeft)
			{
				Client.SetActorLock(WeakCamera.Get());
			}
			else
			{
				Client.SetActorLock(nullptr);
				Client.SetViewLocation(DefaultPerspectiveLocation);
				Client.SetViewRotation((FVector::ZeroVector - DefaultPerspectiveLocation).Rotation());
			}
			Client.Invalidate();
		}
	}
}

void FPrevizerActions::OpenFabWebsite()
{
	FString Error;
	FPlatformProcess::LaunchURL(PrevizerActions::FabUrl, nullptr, &Error);
	if (!Error.IsEmpty())
	{
		UE_LOG(LogPrevizer, Warning, TEXT("Failed to open %s: %s"), PrevizerActions::FabUrl, *Error);
	}
}

bool FPrevizerActions::CreateNewPrevisScene(const FString& InSceneName)
{
	using namespace PrevizerActions;

	if (!GEditor)
	{
		return false;
	}

	if (GEditor->PlayWorld)
	{
		return Fail(LOCTEXT("StopPIE", "Stop Play In Editor before creating a new previs scene."));
	}

	const FString SceneName = InSceneName.TrimStartAndEnd();
	if (SceneName.IsEmpty())
	{
		return Fail(LOCTEXT("NameEmpty", "Enter a scene name first."));
	}

	FText InvalidReason;
	if (!FName::IsValidXName(SceneName, INVALID_OBJECTNAME_CHARACTERS INVALID_LONGPACKAGE_CHARACTERS, &InvalidReason))
	{
		return Fail(FText::Format(LOCTEXT("NameInvalid", "'{0}' can't be used as a scene name: {1}"), FText::FromString(SceneName), InvalidReason));
	}

	const FString SceneFolder = FString(SceneRootFolder) / SceneName;
	const FString LevelName = LevelPrefix + SceneName;
	const FString SequenceName = LevelSequencePrefix + SceneName;
	const FString LevelPackageName = SceneFolder / LevelName;
	const FString SequencePackageName = SceneFolder / SequenceName;

	// Check both up front so nothing gets half-created.
	for (const FString& PackageName : { LevelPackageName, SequencePackageName })
	{
		if (FPackageName::DoesPackageExist(PackageName))
		{
			return Fail(FText::Format(LOCTEXT("NameTaken", "{0} already exists. Choose a different scene name."), FText::FromString(PackageName)));
		}
	}

	// ULevelEditorSubsystem::NewLevel closes the current level without saving, so give the user a chance to save.
	if (!FEditorFileUtils::SaveDirtyPackages(/*bPromptUserToSave*/ true, /*bSaveMapPackages*/ true, /*bSaveContentPackages*/ true))
	{
		UE_LOG(LogPrevizer, Log, TEXT("New previs scene cancelled by user."));
		return false;
	}

	ULevelEditorSubsystem* LevelEditorSubsystem = GEditor->GetEditorSubsystem<ULevelEditorSubsystem>();
	if (!LevelEditorSubsystem || !LevelEditorSubsystem->NewLevel(LevelPackageName, /*bIsPartitionedWorld*/ false))
	{
		return Fail(FText::Format(LOCTEXT("NewLevelFailed", "Failed to create level {0}. See Output Log."), FText::FromString(LevelPackageName)));
	}

	ULevelSequence* Sequence = CreateLevelSequenceAsset(SceneFolder, SequenceName);
	if (!Sequence)
	{
		return Fail(FText::Format(LOCTEXT("SequenceFailed", "Created level {0}, but failed to create its Level Sequence. See Output Log."), FText::FromString(LevelName)));
	}

	CreateFlatLandscape();
	SpawnDefaultLighting();
	AActor* Character = SpawnCharacter();

	UEditorActorSubsystem* ActorSubsystem = GEditor->GetEditorSubsystem<UEditorActorSubsystem>();
	if (ALevelSequenceActor* SequenceActor = Cast<ALevelSequenceActor>(ActorSubsystem->SpawnActorFromClass(ALevelSequenceActor::StaticClass(), FVector::ZeroVector)))
	{
		SequenceActor->SetSequence(Sequence);
		SequenceActor->SetActorLabel(SequenceName);
	}

	// A normal level actor, bound as a possessable. (ULevelSequenceEditorSubsystem::CreateCamera makes a Replaceable
	// binding in UE 5.8, whose camera is only a temporary preview and is never saved in the level.)
	ACineCameraActor* Camera = Cast<ACineCameraActor>(ActorSubsystem->SpawnActorFromClass(ACineCameraActor::StaticClass(), DefaultCameraLocation, FRotator::ZeroRotator));

	// Opening the sequence makes it the active Sequencer, which AddActors needs.
	GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(Sequence);

	if (ULevelSequenceEditorSubsystem* SequenceEditorSubsystem = GEditor->GetEditorSubsystem<ULevelSequenceEditorSubsystem>())
	{
		if (Camera)
		{
			const TArray<FMovieSceneBindingProxy> CameraBindings = SequenceEditorSubsystem->AddActors({ Camera });
			if (CameraBindings.Num() > 0)
			{
				UMovieScene* MovieScene = Sequence->GetMovieScene();
				MovieSceneToolHelpers::CreateCameraCutSectionForCamera(MovieScene, CameraBindings[0].BindingID, MovieScene->GetPlaybackRange().GetLowerBoundValue());
			}
			else
			{
				UE_LOG(LogPrevizer, Warning, TEXT("Could not add the Cine Camera to Level Sequence %s."), *SequenceName);
			}
		}

		if (Character)
		{
			const TArray<FMovieSceneBindingProxy> CharacterBindings = SequenceEditorSubsystem->AddActors({ Character });
			if (CharacterBindings.Num() > 0)
			{
				AddCharacterControlRig(Sequence, CharacterBindings[0]);
			}
			else
			{
				UE_LOG(LogPrevizer, Warning, TEXT("Could not add the character to Level Sequence %s."), *SequenceName);
			}
		}
	}

	// Adding a Control Rig track switches the editor into Animation mode, which opens the Anim Outliner/Details tabs and
	// replaces Selection mode (the transform gizmo). Go back to Selection mode; posing re-enters Animation mode from
	// Sequencer. Selection mode must be re-activated explicitly: the editor only restores it when no mode is active, and
	// Sequencer's own mode stays active while the sequence is open.
	GLevelEditorModeTools().DeactivateMode(FControlRigEditMode::ModeName);
	GLevelEditorModeTools().ActivateDefaultMode();

	SetupPrevisViewports(Camera);

	LevelEditorSubsystem->SaveCurrentLevel();
	UEditorLoadingAndSavingUtils::SavePackages({ Sequence->GetPackage() }, /*bOnlyDirty*/ false);

	UE_LOG(LogPrevizer, Log, TEXT("Created previs scene %s with sequence %s."), *LevelPackageName, *SequencePackageName);
	Notify(FText::Format(LOCTEXT("SceneCreated", "Created previs scene {0}"), FText::FromString(LevelName)), true);
	return true;
}

void FPrevizerActions::SpawnDefaultLighting()
{
	using namespace PrevizerActions;

	if (!GEditor)
	{
		return;
	}

	// Everything is Movable so previs never needs a lighting build.
	if (ADirectionalLight* Sun = SpawnAtOrigin<ADirectionalLight>(DefaultSunRotation))
	{
		UDirectionalLightComponent* SunComponent = Sun->GetComponent();
		SunComponent->SetMobility(EComponentMobility::Movable);
		SunComponent->SetAtmosphereSunLight(true);
	}

	SpawnAtOrigin<ASkyAtmosphere>();

	if (ASkyLight* SkyLight = SpawnAtOrigin<ASkyLight>())
	{
		USkyLightComponent* SkyLightComponent = SkyLight->GetLightComponent();
		SkyLightComponent->SetMobility(EComponentMobility::Movable);
		SkyLightComponent->SetRealTimeCapture(true);
	}

	SpawnAtOrigin<AExponentialHeightFog>();
}

ALandscape* FPrevizerActions::CreateFlatLandscape()
{
	using namespace PrevizerActions;

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		return nullptr;
	}

	// Mirrors FLandscapeEditorDetailCustomization_NewLandscape::OnCreateButtonClicked (UE 5.8) with its default settings.
	const int32 QuadsPerSection = 63;
	const int32 SectionsPerComponent = 1;
	const FIntPoint ComponentCount(8, 8);
	const FVector Scale(100.0, 100.0, 100.0);

	const int32 QuadsPerComponent = SectionsPerComponent * QuadsPerSection;
	const int32 SizeX = ComponentCount.X * QuadsPerComponent + 1;
	const int32 SizeY = ComponentCount.Y * QuadsPerComponent + 1;
	const FVector CenterOffset = FVector(-ComponentCount.X * QuadsPerComponent / 2.0, -ComponentCount.Y * QuadsPerComponent / 2.0, 0.0) * Scale;

	ALandscape* Landscape = World->SpawnActor<ALandscape>(CenterOffset, FRotator::ZeroRotator);
	if (!Landscape)
	{
		Fail(LOCTEXT("LandscapeFailed", "Failed to create the landscape."));
		return nullptr;
	}
	Landscape->SetActorRelativeScale3D(Scale);

	// Empty height data imports as a flat surface at the actor's Z; no material layers.
	TMap<FGuid, TArray<uint16>> HeightDataPerLayer;
	HeightDataPerLayer.Add(FGuid(), TArray<uint16>());
	TMap<FGuid, TArray<FLandscapeImportLayerInfo>> MaterialLayersPerLayer;
	MaterialLayersPerLayer.Add(FGuid(), TArray<FLandscapeImportLayerInfo>());

	Landscape->Import(FGuid::NewGuid(), 0, 0, SizeX - 1, SizeY - 1, SectionsPerComponent, QuadsPerSection,
		HeightDataPerLayer, TEXT(""), MaterialLayersPerLayer, ELandscapeImportAlphamapType::Additive, TArrayView<const FLandscapeLayer>());

	FActorLabelUtilities::SetActorLabelUnique(Landscape, ALandscape::StaticClass()->GetName());
	if (ULandscapeInfo* LandscapeInfo = Landscape->GetLandscapeInfo())
	{
		LandscapeInfo->UpdateLayerInfoMap(Landscape);
	}
	return Landscape;
}

void FPrevizerActions::EnterLandscapeMode()
{
	GLevelEditorModeTools().ActivateMode(FBuiltinEditorModes::EM_Landscape);
}

void FPrevizerActions::EnterSelectMode()
{
	GLevelEditorModeTools().ActivateDefaultMode();
}

bool FPrevizerActions::IsPreviewSelectedCamerasEnabled()
{
	return GetDefault<ULevelEditorViewportSettings>()->bPreviewSelectedCameras;
}

void FPrevizerActions::SetPreviewSelectedCamerasEnabled(bool bEnabled)
{
	ULevelEditorViewportSettings* Settings = GetMutableDefault<ULevelEditorViewportSettings>();
	Settings->bPreviewSelectedCameras = bEnabled;
	Settings->SaveConfig();
}

void FPrevizerActions::OpenPlaceActors()
{
	// Same as FLevelEditorActionCallbacks::OpenPlaceActors (Window > Place Actors).
	FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>("LevelEditor");
	if (TSharedPtr<FTabManager> TabManager = LevelEditorModule.GetLevelEditorTabManager())
	{
		TabManager->TryInvokeTab(LevelEditorTabIds::PlacementBrowser);
	}
}

void FPrevizerActions::ApplyFirstRunDefaults()
{
	// Stored per user, per project, next to the editor's own per-user settings.
	static const TCHAR* Section = TEXT("Previzer");
	static const TCHAR* AppliedKey = TEXT("bFirstRunDefaultsApplied");

	bool bApplied = false;
	GConfig->GetBool(Section, AppliedKey, bApplied, GEditorPerProjectIni);
	if (bApplied)
	{
		return;
	}

	SetPreviewSelectedCamerasEnabled(false);

	GConfig->SetBool(Section, AppliedKey, true, GEditorPerProjectIni);
	GConfig->Flush(false, GEditorPerProjectIni);
}

AActor* FPrevizerActions::SpawnCharacter()
{
	using namespace PrevizerActions;

	if (!GEditor)
	{
		return nullptr;
	}

	const TSoftObjectPtr<USkeletalMesh>& MeshPath = GetDefault<UPrevizerSettings>()->CharacterMesh;
	USkeletalMesh* Mesh = MeshPath.LoadSynchronous();
	if (!Mesh)
	{
		const FText Message = FText::Format(
			LOCTEXT("CharacterMissing", "Character mesh not found at '{0}'. Set it in Project Settings > Plugins > Previzer."),
			FText::FromString(MeshPath.ToString()));
		UE_LOG(LogPrevizer, Warning, TEXT("%s"), *Message.ToString());
		Notify(Message, false);
		return nullptr;
	}

	const FScopedTransaction Transaction(LOCTEXT("AddCharacterTransaction", "Add Character"));

	ASkeletalMeshActor* Character = SpawnAtOrigin<ASkeletalMeshActor>();
	if (Character)
	{
		Character->GetSkeletalMeshComponent()->SetSkeletalMeshAsset(Mesh);
		FActorLabelUtilities::SetActorLabelUnique(Character, CharacterActorLabel);
	}
	return Character;
}

void FPrevizerActions::SetupPrevisViewports(ACineCameraActor* Camera)
{
	FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>("LevelEditor");
	TSharedPtr<ILevelEditor> LevelEditor = LevelEditorModule.GetFirstLevelEditor();
	TSharedPtr<SLevelViewport> ActiveViewport = LevelEditor.IsValid() ? LevelEditor->GetActiveViewportInterface() : nullptr;
	if (!ActiveViewport.IsValid())
	{
		UE_LOG(LogPrevizer, Warning, TEXT("No level viewport found; skipping viewport layout."));
		return;
	}

	// Same as the viewport's Layout > Two Panes (horizontal) menu entry; bound by FLevelViewportTabContent.
	const TSharedPtr<FUICommandList>& ViewportCommands = ActiveViewport->GetCommandList();
	if (!ViewportCommands.IsValid() || !ViewportCommands->TryExecuteAction(FLevelViewportCommands::Get().ViewportConfig_TwoPanesH.ToSharedRef()))
	{
		UE_LOG(LogPrevizer, Warning, TEXT("Could not switch the viewport layout to two side-by-side panes."));
		return;
	}

	// The new panes are created by the layout change; configure them once they exist.
	TWeakObjectPtr<ACineCameraActor> WeakCamera(Camera);
	GEditor->GetTimerManager()->SetTimerForNextTick([WeakCamera]()
	{
		PrevizerActions::AssignPrevisViewports(WeakCamera);
	});
}

FIntPoint FPrevizerActions::GetResolutionSize(EPrevizerResolution Resolution)
{
	switch (Resolution)
	{
	case EPrevizerResolution::QHD1440: return FIntPoint(2560, 1440);
	case EPrevizerResolution::UHD2160: return FIntPoint(3840, 2160);
	case EPrevizerResolution::HD1080:
	default:                           return FIntPoint(1920, 1080);
	}
}

void FPrevizerActions::TakeHighResScreenshot(EPrevizerResolution Resolution)
{
	using namespace PrevizerActions;

	FLevelEditorViewportClient* Client = FindCameraViewportClient();
	if (!Client)
	{
		Fail(LOCTEXT("NoViewport", "No level viewport found to take a screenshot from."));
		return;
	}

	const FIntPoint Size = GetResolutionSize(Resolution);
	if (!GetHighResScreenshotConfig().SetResolution(Size.X, Size.Y))
	{
		Fail(FText::Format(LOCTEXT("ScreenshotTooLarge", "A {0}x{1} screenshot is too large for this machine."), Size.X, Size.Y));
		return;
	}

	// The engine shows its own notification with a link to the saved file.
	Client->TakeHighResScreenShot();
}

void FPrevizerActions::OpenMovieRenderQueue()
{
	// IMovieRenderPipelineEditorModule::MoviePipelineQueueTabName (UE 5.8).
	FGlobalTabmanager::Get()->TryInvokeTab(FName(TEXT("MoviePipelineQueue")));
}

void FPrevizerActions::OpenScreenshotsFolder()
{
	PrevizerActions::ExploreFolder(FPaths::ScreenShotDir());
}

void FPrevizerActions::OpenRendersFolder()
{
	// Matches the "{project_dir}/Saved/MovieRenders/" default in UMoviePipelineBasicConfig (UE 5.8).
	PrevizerActions::ExploreFolder(FPaths::ProjectSavedDir() / TEXT("MovieRenders"));
}

void FPrevizerActions::QueueMovieRender(EPrevizerResolution Resolution)
{
	using namespace PrevizerActions;

	if (!GEditor)
	{
		return;
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	ULevelSequence* Sequence = nullptr;
	for (TActorIterator<ALevelSequenceActor> It(World); It && !Sequence; ++It)
	{
		Sequence = It->GetSequence();
	}
	if (!Sequence)
	{
		Fail(LOCTEXT("NoSequence", "This level has no Level Sequence to render. Create the scene with New Previs Scene first."));
		return;
	}

	// Movie Render Queue renders the saved level and sequence from disk.
	if (!FEditorFileUtils::SaveDirtyPackages(/*bPromptUserToSave*/ true, /*bSaveMapPackages*/ true, /*bSaveContentPackages*/ true))
	{
		return;
	}

	UMoviePipelineQueueSubsystem* QueueSubsystem = GEditor->GetEditorSubsystem<UMoviePipelineQueueSubsystem>();
	UMoviePipelineQueue* Queue = QueueSubsystem ? QueueSubsystem->GetQueue() : nullptr;
	if (!Queue)
	{
		Fail(LOCTEXT("NoQueue", "Movie Render Queue is not available. Make sure the Movie Render Queue plugin is enabled."));
		return;
	}

	const FSoftObjectPath SequencePath(Sequence);
	for (UMoviePipelineExecutorJob* ExistingJob : Queue->GetJobs())
	{
		if (ExistingJob && ExistingJob->Sequence == SequencePath)
		{
			Queue->DeleteJob(ExistingJob);
		}
	}

	UMoviePipelineExecutorJob* Job = UMoviePipelineEditorBlueprintLibrary::CreateJobFromSequence(Queue, Sequence);
	UMoviePipelineBasicConfig* Config = Job ? Job->SetupBasicConfiguration() : nullptr;
	if (!Config)
	{
		Fail(LOCTEXT("JobFailed", "Failed to add the sequence to Movie Render Queue. See Output Log."));
		return;
	}

	Config->OutputResolution = GetMovieRenderResolution(Resolution);
	OpenMovieRenderQueue();

	Notify(FText::Format(LOCTEXT("JobQueued", "Added {0} to Movie Render Queue at {1}. Click Render to start."),
		FText::FromString(Sequence->GetName()), FText::FromName(Config->OutputResolution.ProfileName)), true);
}

#undef LOCTEXT_NAMESPACE
