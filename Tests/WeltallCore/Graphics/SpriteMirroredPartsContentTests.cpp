#include "Field/FieldPartySprites.h"
#include "Graphics/Sprite.h"
#include "Graphics/SpriteSystem.h"
#include "Support/FieldOracleMap.h"
#include "Support/GameDiscs.h"

#include <catch_amalgamated.hpp>

#include <array>
#include <optional>
#include <set>
#include <vector>

using namespace Weltall;

namespace
{
	constexpr uint32 MirroredFlag = 0x10;
	constexpr int32 Fei = 0;
	constexpr int32 Billy = 4;
	/** Emeralda grown up: the party's character 10. */
	constexpr int32 Emeralda = 10;

	bool IsMirrored(const FSpritePart& Part)
	{
		return (Part.Flags & MirroredFlag) != 0;
	}

	/** A frame of a file as the kernel's sprite system builds it: its parts as the drawing takes them. */
	std::vector<FSpritePart> BuiltParts(const FSpriteFile& File, int32 Frame, bool bPlacedByArt)
	{
		FSpriteSystem System;
		System.bMirroredPartsPlacedByArt = bPlacedByArt;
		FSprite Sprite;
		System.Create(Sprite, File, 0x100, 0x1E0, 0x280, 0x100);
		System.SetPartCapacity(Sprite, 16);
		System.SetFrame(Sprite, Frame);
		System.BuildPendingFrames();
		return {Sprite.Parts.begin(), Sprite.Parts.begin() + Sprite.PartCount()};
	}

	/**
	 * The axes about which one frame mirrors the other's pieces: for each piece one mirrors and the other does not (the
	 * same size on the same row), its x in each plus its width.
	 */
	std::set<int32> MirrorAxes(const std::vector<FSpritePart>& First, const std::vector<FSpritePart>& Second)
	{
		std::set<int32> Axes;
		for (const FSpritePart& A : First)
		{
			for (const FSpritePart& B : Second)
			{
				if (A.Width == B.Width && A.Height == B.Height && A.Y == B.Y && IsMirrored(A) != IsMirrored(B))
				{
					Axes.insert(A.X + B.X + A.Width);
				}
			}
		}
		return Axes;
	}

	struct FPartyFiles
	{
		std::optional<Testing::FOracleFieldFiles> Files;
		std::optional<FPartySpriteFiles> Party;

		const FSpriteFile& Of(int32 Character)
		{
			REQUIRE_FALSE(Party->Unpack(0, Character));
			return Party->Buffer(0);
		}
	};
}

TEST_CASE("A mirrored sprite piece is drawn where its frame was made to put it: a pair of mirror-image frames mirrors its pieces about one axis", "[Graphics][Content]")
{
	REQUIRE_DISC(Disc, EDiscRelease::UsaDisc1);
	FPartyFiles Files;
	REQUIRE(Testing::LoadOracleFieldFiles(*Disc, Files.Files));
	Files.Party.emplace(Files.Files->Disc->Image(), Files.Files->Index);

	struct FTwins
	{
		int32 Character;
		int32 First;
		int32 Second;
		/** The axes as the disc's placements give them, one a piece, and the one they share once the pieces are moved. */
		std::set<int32> AsShipped;
		int32 Axis;
	};
	// Billy's head shake (his head with a lock of hair, then with his collar) and a stride of Emeralda's run away from the
	// view: pieces whose cells are padded by 0 to 3 columns, each mirrored that much to the right of where its twin puts it.
	const std::array<FTwins, 4> Twins{{
		{Billy, 2, 8, {0, 2}, 0},
		{Billy, 4, 6, {-2, 0}, -2},
		{Billy, 3, 7, {-1, 0}, -1},
		{Emeralda, 8, 12, {1, 2, 3}, 0},
	}};
	for (const FTwins& Pair : Twins)
	{
		INFO("character " << Pair.Character << ", frames " << Pair.First << " and " << Pair.Second);
		const FSpriteFile& File = Files.Of(Pair.Character);
		CHECK(MirrorAxes(BuiltParts(File, Pair.First, false), BuiltParts(File, Pair.Second, false)) == Pair.AsShipped);
		CHECK(MirrorAxes(BuiltParts(File, Pair.First, true), BuiltParts(File, Pair.Second, true)) == std::set<int32>{Pair.Axis});
	}
}

TEST_CASE("Fei's arm stays on his shoulder as he runs away from the view, and only his mirrored pieces move", "[Graphics][Content]")
{
	REQUIRE_DISC(Disc, EDiscRelease::UsaDisc1);
	FPartyFiles Files;
	REQUIRE(Testing::LoadOracleFieldFiles(*Disc, Files.Files));
	Files.Party.emplace(Files.Files->Disc->Image(), Files.Files->Index);
	const FSpriteFile& File = Files.Of(Fei);

	// The run's back view, its second cel: the arm (the first piece) is a mirrored cell whose art is 5 columns of 8.
	constexpr int32 RunAway = 67;
	const std::vector<FSpritePart> Shipped = BuiltParts(File, RunAway, false);
	const std::vector<FSpritePart> Placed = BuiltParts(File, RunAway, true);
	REQUIRE(Shipped.size() == 6);
	REQUIRE(IsMirrored(Shipped[0]));
	CHECK(Shipped[0].Width == 8);
	CHECK(Shipped[0].X == 9);
	CHECK(Placed[0].X == 6);

	// Every frame: a piece moves only when mirrored, at most three texels to the left, and nothing else changes.
	int32 Moved = 0;
	for (int32 Frame = 1; Frame <= File.FrameCount(File.Tables()); ++Frame)
	{
		INFO("frame " << Frame);
		const std::vector<FSpritePart> Before = BuiltParts(File, Frame, false);
		const std::vector<FSpritePart> After = BuiltParts(File, Frame, true);
		REQUIRE(Before.size() == After.size());
		for (std::size_t Part = 0; Part < Before.size(); ++Part)
		{
			const int32 Shift = Before[Part].X - After[Part].X;
			CHECK(Shift >= 0);
			CHECK(Shift <= (IsMirrored(Before[Part]) ? 3 : 0));
			CHECK(Before[Part].Y == After[Part].Y);
			CHECK(Before[Part].U == After[Part].U);
			CHECK(Before[Part].Width == After[Part].Width);
			Moved += Shift != 0 ? 1 : 0;
		}
	}
	CHECK(Moved == 18);
}
