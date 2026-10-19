#pragma once

#include "CoreMinimal.h"
#include "Graphics/TextureRegion.h"

#include <initializer_list>
#include <span>

class FFieldSceneMesh;
class FVramTexture;
struct FFieldSceneDraw;
namespace Weltall { class FVram; struct FDrawList; struct FTexturePictureSeed; }

/**
 * Binds the texture regions a frame draws to their replacements and captures: the mesh's pictures, the models' (Models,
 * their regions with the colours their loads gave them, Weltall::GearTextureRegions), each sprite's cell, the panorama's
 * strip, the pictures a sheet's frames draw (Sheets: the battle HUD's, Weltall::MenuSheetTextureRegions), and for anything
 * else drawn from a view without any of them, the pictures VRAM holds in it (Weltall::FVram::Pictures) or else its whole
 * page. Game thread.
 */
WELTALL_API void FollowTextureModSources(FVramTexture& Texture, const Weltall::FVram& Vram, const FFieldSceneMesh* Mesh,
	std::initializer_list<const FFieldSceneDraw*> Draws, std::initializer_list<const Weltall::FDrawList*> Lists,
	std::span<const Weltall::FTexturePictureSeed> Models = {}, std::span<const Weltall::FTextureRegion> Sheets = {});
