#pragma once

#include "Foundation/Types.h"
#include "Game/PsxRandom.h"
#include "Graphics/Gte.h"
#include "Graphics/Texture.h"

#include <array>
#include <vector>

namespace Weltall
{
	struct FGteArctangents;

	/**
	 * One triangle of the screen the default transition breaks: half of a 16 x 16 cell of the field's
	 * last picture, which spins, flies off and darkens once its delay has run out. Positions are in
	 * the transition's own view (32 units a screen pixel at the start); velocities are 16.16.
	 */
	struct FBattleScreenShard
	{
		FGteVector Rotation{};
		FGteVector Spin{};
		std::array<int32, 3> Position{};
		/** Steps before it starts moving. */
		int32 Delay = 0;
		std::array<int32, 3> Velocity{};
		/** Added to the vertical velocity every step. */
		int32 Fall = 0;
		/** Each draw buffer's copy of its colour: the step darkens the copy of the buffer it draws into. */
		std::array<std::array<uint8, 3>, 2> Colors{};
	};

	/**
	 * The default transition's shattered screen (battle kinds 0 and 5 on): the field's last picture,
	 * stored at (2C0h, 100h) with every texel semi-transparent, cut into 20 x 14 cells of two triangles
	 * each. Making it draws seven random numbers per triangle, 3,920 in all, before the battle's setup
	 * draws any.
	 */
	class FBattleShatter
	{
	public:
		static constexpr int32 Sides = 2;
		static constexpr int32 Rows = 14;
		static constexpr int32 Columns = 20;
		static constexpr int32 ShardCount = Sides * Rows * Columns;
		/** Where the transition keeps the picture it breaks. */
		static constexpr int32 PictureX = 0x2C0;
		static constexpr int32 PictureY = 0x100;
		/** A shard is drawn only while it is farther than this. */
		static constexpr int32 NearestDrawn = 0x3F;
		/** The projection the shards are drawn with, apart from the battle's. */
		static constexpr int32 ScreenCentreX = 0xA0;
		static constexpr int32 ScreenCentreY = 0x70;
		static constexpr int32 ProjectionDistance = 0x200;

		/** The two triangles' corners about the shard's position: side 0 the cell's upper left, side 1 its lower right. */
		static constexpr std::array<std::array<FGteVector, 3>, Sides> Corners{{
			{{{-0xA0, -0xA0, 0}, {0x160, -0xA0, 0}, {-0xA0, 0x160, 0}}},
			{{{0xA0, -0x160, 0}, {0xA0, 0xA0, 0}, {-0x160, 0xA0, 0}}},
		}};

		/** Breaks the screen, drawing its random numbers from Random. */
		WELTALLCORE_API void Create(FPsxRandom& Random, const FGteArctangents& Arctangents);
		/** One step: each shard waits out its delay, then moves, spins, slows, falls and darkens in the buffer drawn. */
		WELTALLCORE_API void Step(int32 Buffer);

		const FBattleScreenShard& Shard(int32 Side, int32 Row, int32 Column) const { return Shards[static_cast<std::size_t>(Index(Side, Row, Column))]; }
		/** The steps taken since it was made. */
		int32 Steps() const { return StepCount; }

		/** A corner's texel in the stored picture's page (16-bit texels, 64 wide from column 40h * (Column / 4)). */
		WELTALLCORE_API static std::array<std::array<uint8, 2>, 3> Texels(int32 Side, int32 Row, int32 Column);
		/** The texture page word a shard samples: the picture's 16-bit page with blend mode 1. */
		WELTALLCORE_API static uint16 TexturePage(int32 Column);

		static constexpr int32 Index(int32 Side, int32 Row, int32 Column) { return (Side * Rows + Row) * Columns + Column; }

		/** The matrix a shard is drawn with: the game's own rotation of its angles, at its position. */
		WELTALLCORE_API static FGteMatrix DrawMatrix(const FBattleScreenShard& Shard);
		/**
		 * The ordering table's entry a shard is linked into: a quarter of its third corner's depth, shifted down by
		 * six. The far entries are drawn first, and within an entry the shard linked last (they are linked side by
		 * side, row by row, column by column).
		 */
		WELTALLCORE_API static int32 DrawEntry(const FBattleScreenShard& Shard, int32 Side);
		/** The pixel of the picture each corner shows, the picture's top left at (0, 0); a column past the screen's sides shows the picture there. */
		WELTALLCORE_API static std::array<std::array<int32, 2>, 3> PicturePixels(int32 Side, int32 Row, int32 Column);

	private:
		std::array<FBattleScreenShard, ShardCount> Shards{};
		int32 StepCount = 0;
	};

	/**
	 * The shattered picture past the screen's sides, for a display wider than the original's: the same
	 * triangles in the columns beyond the screen's twenty, made and stepped as the screen's are (those
	 * farther out break later). Their random numbers come from a generator of their own, so the game's
	 * draws stay the original's.
	 */
	class FBattleShatterWings
	{
	public:
		/** The columns to either side: 240 pixels, the screen's width at 32:9. */
		static constexpr int32 ColumnsEachSide = 15;
		static constexpr int32 FirstColumn = -ColumnsEachSide;
		static constexpr int32 EndColumn = FBattleShatter::Columns + ColumnsEachSide;

		WELTALLCORE_API void Create(FPsxRandom Random, const FGteArctangents& Arctangents);
		WELTALLCORE_API void Step(int32 Buffer);

		/** Column from FirstColumn to EndColumn - 1, outside the screen's own columns. */
		const FBattleScreenShard& Shard(int32 Side, int32 Row, int32 Column) const { return Shards[static_cast<std::size_t>(Index(Side, Row, Column))]; }

	private:
		static constexpr int32 Index(int32 Side, int32 Row, int32 Column)
		{
			const int32 Wing = Column < 0 ? Column + ColumnsEachSide : Column - FBattleShatter::Columns + ColumnsEachSide;
			return (Side * FBattleShatter::Rows + Row) * 2 * ColumnsEachSide + Wing;
		}

		std::vector<FBattleScreenShard> Shards;
	};

	/**
	 * The default transition's loop: 86 frames of two vertical blanks each, the field's picture breaking
	 * over a background that fades from white by 12 a frame in the buffer drawn, while the battle's
	 * setup runs in stages as the disc delivers its files (with the files at once: frames 1 to 4).
	 */
	class FBattleDefaultTransition
	{
	public:
		static constexpr int32 FrameCount = 0x56;
		/** The setup's stages the loop runs, one a frame once the files it needs are read: 1 to 4, then 5 when done. */
		static constexpr int32 LastStage = 5;

		/** Takes the picture and breaks it (the shatter's random numbers). */
		WELTALLCORE_API void Start(FPsxRandom& Random, const FGteArctangents& Arctangents);
		/**
		 * One frame: the buffers swap, the setup's next stage runs when its files are in (returned: 1
		 * the combatants' files, 2 the stage's, 3 the formation and the party, 4 the items, the order and
		 * the menus; 0 none this frame), the shards step, the drawn buffer's background darkens.
		 */
		WELTALLCORE_API int32 Frame();
		/** Whether the loop has ended: every frame run and every stage done. */
		bool IsDone() const { return FramesLeft == 0 && Stage == LastStage; }

		const FBattleShatter& Shatter() const { return Pieces; }
		const FBattleShatterWings& Wings() const { return WingPieces; }
		/** The draw buffer the last frame drew into. */
		int32 Buffer() const { return DrawBuffer; }
		/** Each draw buffer's background colour. */
		const std::array<std::array<uint8, 3>, 2>& Backgrounds() const { return BackgroundColors; }

	private:
		FBattleShatter Pieces;
		FBattleShatterWings WingPieces;
		int32 FramesLeft = 0;
		int32 Stage = 1;
		int32 DrawBuffer = 0;
		std::array<std::array<uint8, 3>, 2> BackgroundColors{};
	};

	/** The meshes the battle makes of the screen's picture: the transitions its setup draws itself (battle kinds 1 to 4), and the screen wave. */
	enum class EBattleScreenMesh : uint8
	{
		/** Kind 1: the picture in triangles that come nearer and dim. */
		Triangles,
		/** Kind 4: the same triangles standing where they are. */
		TrianglesStill,
		/** Kind 2: the picture as a mesh that ripples about its middle, ever higher, while it comes nearer. */
		Wave,
		/** Kind 3: the ripples run outward. */
		WaveSine,
		/** The screen wave (effect 218): ripples that run outward ever faster and higher, slowly nearer, then darken. */
		ScreenWave,
	};

	/** A triangle of a setup transition's mesh as a frame draws it. */
	struct FBattleMeshTriangle
	{
		/** The matrix its corners go through. */
		FGteMatrix Matrix;
		std::array<FGteVector, 3> Corners{};
		/** Each corner's colour, the same in its three channels, which modulates the picture (80h leaves it as it is). */
		std::array<uint8, 3> Colors{};
		/** The pixel of the picture each corner shows, the picture's top left at (0, 0). */
		std::array<std::array<int32, 2>, 3> Pixels{};
		/** The frame draws it: a triangle that has come too near is not. */
		bool bDrawn = true;
	};

	/** Where the GTE puts a mesh's triangle: its corners on the screen and the ordering table's entry, when it is linked. */
	struct FBattleMeshLink
	{
		std::array<std::array<int16, 2>, 3> Screen{};
		int32 Entry = 0;
		bool bLinked = false;
	};

	/**
	 * The mesh a setup transition or the screen wave makes of the screen's last picture (stored as the
	 * default transition stores it), drawn in frames of two vertical blanks: 32 x 32 cells of two flat
	 * triangles each, added to what lies behind them, or 16 x 16 cells of two triangles shaded a corner,
	 * averaged with it. The setup's transitions draw it over black for 82 frames; the screen wave never
	 * clears the screen (FBattleScreenWave). It draws no random number. Columns past the screen's own
	 * (below 0, or from Columns()) are the same mesh continued to either side, for a display wider than
	 * the original's.
	 */
	class FBattleScreenMesh
	{
	public:
		static constexpr int32 Sides = 2;
		/** The projection the mesh is drawn with: the default transition's. */
		static constexpr int32 ScreenCentreX = FBattleShatter::ScreenCentreX;
		static constexpr int32 ScreenCentreY = FBattleShatter::ScreenCentreY;
		static constexpr int32 ProjectionDistance = FBattleShatter::ProjectionDistance;

		WELTALLCORE_API void Create(EBattleScreenMesh InKind);
		/** A frame's step, before its drawing into Buffer. */
		WELTALLCORE_API void Step(int32 Buffer);

		EBattleScreenMesh Kind() const { return Transition; }
		bool IsWave() const { return Transition != EBattleScreenMesh::Triangles && Transition != EBattleScreenMesh::TrianglesStill; }
		int32 Rows() const { return IsWave() ? 14 : 7; }
		int32 Columns() const { return IsWave() ? 20 : 10; }
		/** The columns to either side that reach the screen's width at 32:9. */
		int32 WingColumns() const { return IsWave() ? 15 : 8; }
		/** How its triangles blend with what lies behind them. */
		EBlendMode Blend() const { return IsWave() ? EBlendMode::Average : EBlendMode::Add; }

		/**
		 * The triangles of columns FirstColumn to EndColumn - 1 as the frame draws them into Buffer, in the order
		 * it links them: side by side, row by row, column by column. Reuses Triangles' memory.
		 */
		WELTALLCORE_API void Draw(int32 Buffer, int32 FirstColumn, int32 EndColumn, std::vector<FBattleMeshTriangle>& Triangles) const;
		/** A drawn triangle through the GTE as the transition's draw runs it. */
		WELTALLCORE_API FBattleMeshLink Link(const FBattleMeshTriangle& Triangle) const;

		/** The steps taken. */
		int32 Steps() const { return Count; }
		/** How far ahead of the eye the mesh lies. */
		int32 Depth() const { return Distance; }
		/** The triangles' colour in each draw buffer's packets. */
		const std::array<uint8, 2>& BufferColors() const { return Colors; }
		/** The wave's brightness, phase, height and speed toward the eye. */
		int32 Brightness() const { return Light; }
		int32 Phase() const { return WavePhase; }
		int32 Amplitude() const { return WaveHeight; }
		int32 Speed() const { return Approach; }

	private:
		EBattleScreenMesh Transition = EBattleScreenMesh::Triangles;
		int32 Count = 0;
		int32 Distance = 0;
		std::array<uint8, 2> Colors{};
		int32 Light = 0;
		int32 WavePhase = 0;
		int32 WaveHeight = 0;
		int32 Approach = 0;
	};
}
