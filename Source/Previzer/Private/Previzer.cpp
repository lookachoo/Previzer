// Copyright Epic Games, Inc. All Rights Reserved.

#include "Previzer.h"
#include "PrevizerActions.h"
#include "PrevizerStyle.h"
#include "PrevizerCommands.h"
#include "PrevizerSettings.h"
#include "ActorFactories/ActorFactorySkeletalMesh.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Editor.h"
#include "IPlacementModeModule.h"
#include "LevelEditor.h"
#include "SPrevizerPanel.h"
#include "Widgets/Docking/SDockTab.h"
#include "ToolMenus.h"

static const FName PrevizerTabName("Previzer");
static const FName PrevizerPlacementCategory("Previzer");

#define LOCTEXT_NAMESPACE "FPrevizerModule"

void FPrevizerModule::StartupModule()
{
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module
	
	FPrevizerStyle::Initialize();
	FPrevizerStyle::ReloadTextures();

	FPrevizerCommands::Register();
	
	PluginCommands = MakeShareable(new FUICommandList);

	PluginCommands->MapAction(
		FPrevizerCommands::Get().OpenPluginWindow,
		FExecuteAction::CreateRaw(this, &FPrevizerModule::PluginButtonClicked),
		FCanExecuteAction());

	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FPrevizerModule::RegisterMenus));
	
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(PrevizerTabName, FOnSpawnTab::CreateRaw(this, &FPrevizerModule::OnSpawnPluginTab))
		.SetDisplayName(LOCTEXT("FPrevizerTabTitle", "Previzer"))
		.SetMenuType(ETabSpawnerMenuType::Hidden);

	FPrevizerActions::ApplyFirstRunDefaults();

	// The character's FAssetData is only available once the Asset Registry has scanned plugin content.
	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
	if (AssetRegistry.IsLoadingAssets())
	{
		AssetRegistry.OnFilesLoaded().AddRaw(this, &FPrevizerModule::RegisterPlacementItems);
	}
	else
	{
		RegisterPlacementItems();
	}
}

void FPrevizerModule::RegisterPlacementItems()
{
	IPlacementModeModule& PlacementMode = IPlacementModeModule::Get();
	PlacementMode.RegisterPlacementCategory(FPlacementCategoryInfo(
		LOCTEXT("PlacementCategory", "Previzer"), PrevizerPlacementCategory, TEXT("PMPrevizer"), /*SortOrder*/ 5));

	const FSoftObjectPath CharacterPath = GetDefault<UPrevizerSettings>()->CharacterMesh.ToSoftObjectPath();
	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
	const FAssetData CharacterAsset = AssetRegistry.GetAssetByObjectPath(CharacterPath);
	if (!CharacterAsset.IsValid())
	{
		UE_LOG(LogPrevizer, Warning, TEXT("Character mesh not found at '%s'; it will not appear in the Place Actors panel. Set it in Project Settings > Plugins > Previzer."), *CharacterPath.ToString());
		return;
	}

	UActorFactory* Factory = GEditor ? GEditor->FindActorFactoryByClass(UActorFactorySkeletalMesh::StaticClass()) : nullptr;
	TSharedRef<FPlaceableItem> CharacterItem = MakeShared<FPlaceableItem>(Factory, CharacterAsset);
	CharacterItem->DisplayName = LOCTEXT("CharacterPlaceableItem", "Character");
	PlacementMode.RegisterPlaceableItem(PrevizerPlacementCategory, CharacterItem);
}

void FPrevizerModule::UnregisterPlacementItems()
{
	if (FModuleManager::Get().IsModuleLoaded("AssetRegistry"))
	{
		FModuleManager::GetModuleChecked<FAssetRegistryModule>("AssetRegistry").Get().OnFilesLoaded().RemoveAll(this);
	}

	if (IPlacementModeModule::IsAvailable())
	{
		IPlacementModeModule::Get().UnregisterPlacementCategory(PrevizerPlacementCategory);
	}
}

void FPrevizerModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.

	UnregisterPlacementItems();

	UToolMenus::UnRegisterStartupCallback(this);

	UToolMenus::UnregisterOwner(this);

	FPrevizerStyle::Shutdown();

	FPrevizerCommands::Unregister();

	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(PrevizerTabName);
}

TSharedRef<SDockTab> FPrevizerModule::OnSpawnPluginTab(const FSpawnTabArgs& SpawnTabArgs)
{
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			SNew(SPrevizerPanel)
		];
}

void FPrevizerModule::PluginButtonClicked()
{
	FGlobalTabmanager::Get()->TryInvokeTab(PrevizerTabName);
}

void FPrevizerModule::RegisterMenus()
{
	// Owner will be used for cleanup in call to UToolMenus::UnregisterOwner
	FToolMenuOwnerScoped OwnerScoped(this);

	{
		UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Window");
		{
			FToolMenuSection& Section = Menu->FindOrAddSection("WindowLayout");
			Section.AddMenuEntryWithCommandList(FPrevizerCommands::Get().OpenPluginWindow, PluginCommands);
		}
	}

	{
		UToolMenu* ToolbarMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.LevelEditorToolBar.PlayToolBar");
		{
			FToolMenuSection& Section = ToolbarMenu->FindOrAddSection("PluginTools");
			{
				FToolMenuEntry& Entry = Section.AddEntry(FToolMenuEntry::InitToolBarButton(FPrevizerCommands::Get().OpenPluginWindow));
				Entry.SetCommandList(PluginCommands);
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FPrevizerModule, Previzer)