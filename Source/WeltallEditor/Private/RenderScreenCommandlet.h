#pragma once

#include "Commandlets/Commandlet.h"
#include "CoreMinimal.h"

#include "RenderScreenCommandlet.generated.h"

/**
 * Renders one of the game's screens, at a moment and size of one's choosing, into a PNG, without a
 * window: the renders the presentation choices are made from and checked against.
 *
 *   UnrealEditor-Cmd Weltall.uproject -run=RenderScreen -Screen=Game|Movie|Field|MenuIcons [-Movie=N] [-EndFrame=N]
 *       [-Map=N [-Entrance=k|-Target=x,z|x,y,z -Layer=L -Yaw=0..7 -Eye=x,y,z -Pitch=D -Projection=H -Zoom=Z
 *       -Background=r,g,b -CullMargins=x,y] -Sort=Painter|Depth|Scenery -Filter=Nearest|Crisp|Bilinear|Upscaled
 *       -SpriteFilter=Nearest|Crisp|Bilinear|Upscaled -AA=None|MSAA|SSAA -Cull=Original|InRange -MovieFilter=Smooth|Upscaled]
 *       [-State=Full] [-Menu=Mode,Parameter -MenuAt=S] [-MenuArea=Screen|Wide] [-Icons=Set] [-Pad=a,b+c,...|-PadFile=Path -PadStep=S] -Seconds=S -Size=WxH -Out=path.png
 *       -AllowCommandletRendering -RenderOffscreen -unattended
 *
 * The game is played from power-on (the publisher's notice, the opening movie, the title's map and
 * whatever the programs lead to). A field map is played as a new game enters it. With a viewpoint
 * (-Entrance, -Target or -Eye) its scenery alone is seen instead, standing still, by its follow
 * camera at rest on the target: its height the floor of walkmesh layer L under (x, z) unless given;
 * the camera's settings and the render state are the field's start values unless given (a map's
 * scripts set them).
 *
 * A game played can be staged (FRenderStaging): -State=Full fills the menus (items, equipment,
 * Deathblows, gears, gold) before its first frame, and -Menu opens a menu of the Menu program's modes
 * (with its parameter: a shop's number, a name's) as the field's scripts ask for one, -MenuAt seconds
 * into play; -MenuArea=Wide draws the menus past the original screen's edges on a wider display;
 * -Icons=Keyboard|Xbox|PlayStation|Nintendo|SteamDeck shows the player's buttons with that set's icons.
 * -Screen=MenuIcons lays out the port's button pictures of every set for review (FMenuIconSheetScreen).
 *
 * Seconds of play pass before the frame is taken, the controller holding the buttons of the pad
 * script's steps in turn (none after its last step, or without one). -PadFile reads the same comma-separated
 * script from a local text file instead of the command line. -Profile=Hz plays them as a display
 * of that rate takes frames, draws each at the size given, and from -From seconds on logs what
 * the frames cost (FFrameProfile); -ProfileGpuAt=S logs the engine's breakdown by pass of the frame at that moment. Without
 * -AllowCommandletRendering a commandlet has no renderer and every frame comes out black.
 * -GameTrace=S optionally logs the normal loop, field position/lock/camera/disc/movie and battle menu/waits at
 * interval S (at least 1/60 second), without changing gameplay state. Keep these game-derived logs local.
 */
UCLASS()
class URenderScreenCommandlet final : public UCommandlet
{
	GENERATED_BODY()

public:
	URenderScreenCommandlet();

	virtual int32 Main(const FString& Params) override;
};
