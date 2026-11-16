// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

PREVIZER_API DECLARE_LOG_CATEGORY_EXTERN(LogPrevizer, Log, All);

/** Output resolutions offered for screenshots and movie renders. Match Movie Render Queue's built-in profiles. */
enum class EPrevizerResolution : uint8
{
	HD1080,		// 1920x1080
	QHD1440,	// 2560x1440 ("2K")
	UHD2160,	// 3840x2160 ("4K")
};

/** All Previzer tool logic. UI (Slate panel, menus, hotkeys) should only call into this. */
class PREVIZER_API FPrevizerActions
{
public:
	/**
	 * Opens https://www.fab.com in the user's default web browser.
	 * The editor's integrated Fab tab (UE 5.8 Fab plugin) has no public API to open it, so it is not used here.
	 */
	static void OpenFabWebsite();

	/**
	 * Creates /Game/Scenes/<SceneName>/L_<SceneName> (non-World-Partition level) and LS_<SceneName> (Level Sequence) and opens
	 * the level. Adds a flat landscape, default lighting, a character and a Cine Camera at the origin, places the sequence in the level via a
	 * Level Sequence Actor, opens it in Sequencer, binds the camera (with a camera cut) and the character (with its Control
	 * Rig), and sets up the two-pane camera/perspective viewport layout. Prompts to save unsaved work first; returns false
	 * if the name is invalid or taken, the user cancels, or anything fails.
	 */
	static bool CreateNewPrevisScene(const FString& SceneName);

	/** Spawns a directional light (atmosphere sun), sky atmosphere, real-time sky light, and height fog at the origin. */
	static void SpawnDefaultLighting();

	/**
	 * Creates a flat landscape centered on the origin with its surface at Z=0, using the Landscape Mode "New Landscape"
	 * defaults (8x8 components, 63 quads per section, scale 100 = roughly 504 m across).
	 */
	static class ALandscape* CreateFlatLandscape();

	/** Switches the level editor to Landscape Mode (sculpt, paint, manage). */
	static void EnterLandscapeMode();

	/** Switches the level editor back to the default Selection Mode. */
	static void EnterSelectMode();

	/** Editor Preferences > Level Editor > Viewports > Preview Selected Cameras (picture-in-picture camera preview). */
	static bool IsPreviewSelectedCamerasEnabled();
	static void SetPreviewSelectedCamerasEnabled(bool bEnabled);

	/**
	 * Applies Previzer's preferred editor defaults once per user (e.g. Preview Selected Cameras off). Later changes
	 * the user makes are kept.
	 */
	static void ApplyFirstRunDefaults();

	/** Opens the Place Actors panel (where the Previzer category with the character lives). */
	static void OpenPlaceActors();

	/** Spawns the character skeletal mesh (UPrevizerSettings::CharacterMesh) at 0,0,0. Logs a warning and returns null if missing. */
	static AActor* SpawnCharacter();

	/**
	 * Switches the level editor to two side-by-side viewports: left looks through Camera (and follows Sequencer
	 * camera cuts), right is a free perspective view.
	 */
	static void SetupPrevisViewports(class ACineCameraActor* Camera);

	static FIntPoint GetResolutionSize(EPrevizerResolution Resolution);

	/** Takes a High Resolution Screenshot through the camera viewport (left pane), saved to Saved/Screenshots. */
	static void TakeHighResScreenshot(EPrevizerResolution Resolution);

	/** Opens the Movie Render Queue window. */
	static void OpenMovieRenderQueue();

	/** Opens the folder High Res Screenshots are saved to (FPaths::ScreenShotDir) in the file browser. */
	static void OpenScreenshotsFolder();

	/** Opens Movie Render Queue's default output folder (Saved/MovieRenders) in the file browser. */
	static void OpenRendersFolder();

	/**
	 * Adds the current level's Level Sequence to the Movie Render Queue at the given resolution (replacing any queued
	 * job for that sequence) and opens the queue window, ready to render.
	 */
	static void QueueMovieRender(EPrevizerResolution Resolution);
};
