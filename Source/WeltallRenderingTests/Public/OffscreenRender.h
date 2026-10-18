#pragma once

#include "CoreMinimal.h"
#include "Presentation/GameScreen.h"
#include "Screens/GameScreen.h"
#include "Rendering/DrawListRendering.h"

class FDrawListTextures;
class FFieldSceneMesh;
class FVramTexture;
class FMoviePictureTexture;
struct FFieldSceneDraw;

/** Draws a 2D layer over black, its game screen (Screen of its own pixels) filling a target of the given size, and reads the pixels back. */
WELTALLRENDERINGTESTS_API TArray<FColor> RenderDrawListOffscreen(const FDrawListTextures& Textures, const FDrawListFrame& Frame, FIntPoint Size, Weltall::FScreenSize Screen);

/** Draws a movie picture over black, its game screen filling a target of the given size, and reads the pixels back. */
WELTALLRENDERINGTESTS_API TArray<FColor> RenderMovieOffscreen(const FMoviePictureTexture& Picture, FIntPoint Size, Weltall::FScreenSize Screen, const Weltall::FScreenRect& Placement);

/** Draws a frame of a field map over black, filling a target of the given size, and reads the pixels back. */
WELTALLRENDERINGTESTS_API TArray<FColor> RenderFieldSceneOffscreen(const FFieldSceneMesh& Mesh, const FVramTexture& Vram, const FFieldSceneDraw& Draw, FIntPoint Size);

/** Draws a screen's captured frame over black, in the part of a target of the given size the picture's shape takes, and reads the pixels back. */
WELTALLRENDERINGTESTS_API TArray<FColor> RenderScreenFrameOffscreen(const IScreenFrame& Frame, FIntPoint Size, Weltall::EDisplayAspect Aspect = Weltall::EDisplayAspect::Window);

/** Runs AddPasses into a black target of the given size on the rendering thread and reads the pixels back. */
WELTALLRENDERINGTESTS_API TArray<FColor> RenderPassesOffscreen(FIntPoint Size, TFunctionRef<void(FRDGBuilder&, FRDGTextureRef)> AddPasses);
