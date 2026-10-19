#include "Battle/Show/BattleShowVram.h"

#include "Foundation/LittleEndian.h"

#include <array>

namespace Weltall
{
	namespace
	{
		constexpr uint32 ImageBlock = 0x1100;
		constexpr uint32 ClutBlock = 0x1101;
		constexpr std::size_t BlockHeader = 0x10;
	}

	bool UploadBattleUnits(FVram& Vram, std::span<const uint8> Bytes, std::size_t At, const FVramRect& Rect)
	{
		const std::size_t Units = Rect.Width > 0 && Rect.Height > 0 ? std::size_t(Rect.Width) * std::size_t(Rect.Height) : 0;
		if (At + 2 * Units > Bytes.size())
		{
			return false;
		}
		FVramImage Image;
		Image.Rect = Rect;
		Image.Units.resize(Units);
		for (std::size_t Unit = 0; Unit < Units; ++Unit)
		{
			Image.Units[Unit] = LittleEndian::ReadU16(Bytes, At + 2 * Unit);
		}
		Vram.Upload(Image);
		return true;
	}

	bool UploadBattleImageList(FVram& Vram, std::span<const uint8> Bytes, std::size_t At)
	{
		if (At + 4 > Bytes.size())
		{
			return false;
		}
		const uint32 Count = LittleEndian::ReadU32(Bytes, At);
		std::size_t Cursor = At + 4 + 4 * std::size_t{Count};
		for (uint32 Block = 0; Block < Count; ++Block)
		{
			if (Cursor + BlockHeader > Bytes.size())
			{
				return false;
			}
			const uint32 Kind = LittleEndian::ReadU32(Bytes, Cursor);
			if (Kind != ImageBlock && Kind != ClutBlock)
			{
				return true;
			}
			const FVramRect Rect{static_cast<int16>(LittleEndian::ReadS16(Bytes, Cursor + 4) + LittleEndian::ReadS16(Bytes, Cursor + 8)),
				static_cast<int16>(LittleEndian::ReadS16(Bytes, Cursor + 6) + LittleEndian::ReadS16(Bytes, Cursor + 10)), LittleEndian::ReadS16(Bytes, Cursor + 12),
				LittleEndian::ReadS16(Bytes, Cursor + 14)};
			Cursor += BlockHeader;
			if (!UploadBattleUnits(Vram, Bytes, Cursor, Rect))
			{
				return false;
			}
			Cursor += 2 * std::size_t(Rect.Width) * std::size_t(Rect.Height);
		}
		return true;
	}

	void MoveBattleImage(FVram& Vram, const FVramRect& Rect, int32 X, int32 Y, FVramImage& Scratch)
	{
		Scratch.Rect = {static_cast<int16>(X), static_cast<int16>(Y), Rect.Width, Rect.Height};
		Scratch.Units.resize(Rect.Width > 0 && Rect.Height > 0 ? std::size_t(Rect.Width) * std::size_t(Rect.Height) : 0);
		std::size_t Unit = 0;
		for (int32 Row = 0; Row < Rect.Height; ++Row)
		{
			for (int32 Column = 0; Column < Rect.Width; ++Column)
			{
				Scratch.Units[Unit++] = Vram.At(Rect.X + Column, Rect.Y + Row);
			}
		}
		Vram.UploadCopy(Scratch, Rect);
	}

	void CopyGearColumns(FVram& Vram, std::span<const FGearTexturePlace> Places, bool bToEffects, FVramImage& Scratch)
	{
		constexpr std::array<int16, 3> EffectColumns{0x280, 0x240, 0x200};
		constexpr int16 ColumnsY = 0x100;
		constexpr int16 ColumnWidth = 0x40;
		constexpr int16 ColumnHeight = 0x100;
		for (std::size_t Column = 0; Column < EffectColumns.size() && Column < Places.size(); ++Column)
		{
			const FGearTexturePlace& Place = Places[Column];
			if (bToEffects)
			{
				MoveBattleImage(Vram, {Place.X, Place.Y, ColumnWidth, ColumnHeight}, EffectColumns[Column], ColumnsY, Scratch);
			}
			else
			{
				MoveBattleImage(Vram, {EffectColumns[Column], ColumnsY, ColumnWidth, ColumnHeight}, Place.X, Place.Y, Scratch);
			}
		}
	}
}
