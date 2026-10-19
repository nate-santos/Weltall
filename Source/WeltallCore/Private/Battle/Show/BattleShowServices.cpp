#include "Battle/Show/BattleShow.h"

#include "Battle/Show/BattleShowVram.h"
#include "Graphics/SpriteDrawing.h"

namespace Weltall
{
	namespace
	{
		/** The system commands that start an extension's effect. */
		constexpr int32 WaveCommand = 0x19;
		constexpr int32 ShardsCommand = 0x1D;
		constexpr int32 ScrollCommand = 0x24;
		constexpr int32 SecondScrollCommand = 0x25;
		constexpr int32 ScreenWaveCommand = 0x54;
		/**
		 * The screen wave: the screen it captures and where it puts the capture, the screen area it clears after, and the
		 * battle frame's catch-up steps after its blanks.
		 */
		constexpr FVramRect CapturedScreen{0, 0, 0x140, 0xE0};
		constexpr int16 CaptureX = 0x2C0;
		constexpr int16 CaptureY = 0x100;
		constexpr FVramRect ClearedScreen{0, 0, 0x140, 0x1C0};
		constexpr int32 MostCatchUpSteps = 4;
		/** The gear places whose columns the wave keeps from its capture. */
		constexpr std::array<std::size_t, 2> KeptPlaces{1, 2};
		constexpr int16 ColumnWidth = 0x40;
		constexpr int16 ColumnHeight = 0x100;
		/** Every captured texel is made opaque against the blending's rule (bit 15). */
		constexpr uint16 OpaqueBit = 0x8000;

		void ReadVram(const FVram& Vram, const FVramRect& Rect, FVramImage& Into)
		{
			Into.Rect = Rect;
			Into.Units.resize(static_cast<std::size_t>(Rect.Width) * static_cast<std::size_t>(Rect.Height));
			std::size_t Unit = 0;
			for (int32 Row = 0; Row < Rect.Height; ++Row)
			{
				for (int32 Column = 0; Column < Rect.Width; ++Column)
				{
					Into.Units[Unit++] = Vram.At(Rect.X + Column, Rect.Y + Row);
				}
			}
		}
	}

	bool FBattleShow::ObjectRoot(int32 Slot, std::array<int32, 3>& Translation)
	{
		return Objects.Root(Slot, Translation);
	}

	void FBattleShow::ReactModel(int32 Slot, uint16 FromSlots, int32 Sequence)
	{
		Objects.React(Slot, FromSlots, Sequence);
	}

	void FBattleShow::SetObjectsVisible(bool bVisible)
	{
		Objects.SetVisible(bVisible);
	}

	void FBattleShow::LoadSpecialModel(int32 Model)
	{
		Objects.LoadSpecialModel(Model);
	}

	void FBattleShow::RunSpecialModel(int32 Sequence, uint16 Slots)
	{
		Objects.RunSpecialModel(Sequence, Slots);
	}

	void FBattleShow::FreeSpecialModel()
	{
		Objects.FreeSpecialModel();
	}

	void FBattleShow::StartExtension(int32 Command, FSprite& Sprite, const FSpriteOperands&, const std::array<int32, 6>& Arguments)
	{
		// Each command's entry lies in one module: with another in memory it runs whatever code is there. EXT6's screen wave runs
		// frames of its own, outside the battle's.
		const int32 Module = Command == WaveCommand ? 2 : Command == ShardsCommand ? 3 : Command == ScrollCommand ? 4 : Command == SecondScrollCommand ? 5 : 6;
		if (Module != LoadedExtension)
		{
			ReportFault(EBattleShowFault::Extension);
			return;
		}
		switch (Command)
		{
		case ScreenWaveCommand:
			StartScreenWave();
			return;
		case WaveCommand:
			Extensions.StartWave(Sprite, Arguments);
			return;
		case ShardsCommand:
			if (!Extensions.StartShards(Sprite, SpriteModelMatrix(Sprite), {Arguments[0], Arguments[1], Arguments[2], Arguments[3], Arguments[4]}, ModelDrawing))
			{
				ReportFault(EBattleShowFault::Extension);
			}
			return;
		default:
			Extensions.StartScroll(Sprite, Module);
			return;
		}
	}

	void FBattleShow::StartScreenWave()
	{
		if (Objects.Tables == nullptr)
		{
			ReportFault(EBattleShowFault::File);
			return;
		}
		// Two gear places' columns kept, the screen captured to the effects' place opaque, the columns put back once the wave
		// is over and the screen cleared; the clear colours it dims come back as they were.
		std::array<FVramImage, KeptPlaces.size()> Kept;
		for (std::size_t Index = 0; Index < KeptPlaces.size(); ++Index)
		{
			const FGearTexturePlace& Place = Objects.Tables->GearPlaces[KeptPlaces[Index]];
			ReadVram(Vram, {Place.X, Place.Y, ColumnWidth, ColumnHeight}, Kept[Index]);
			Vram.NoteReadback(Kept[Index].Rect);
		}
		FVramImage Capture;
		ReadVram(Vram, CapturedScreen, Capture);
		for (uint16& Unit : Capture.Units)
		{
			Unit = static_cast<uint16>(Unit | OpaqueBit);
		}
		Capture.Rect.X = CaptureX;
		Capture.Rect.Y = CaptureY;
		Vram.Upload(Capture);
		for (const FVramImage& Column : Kept)
		{
			Vram.Upload(Column);
		}
		FVramImage Cleared;
		Cleared.Rect = ClearedScreen;
		Cleared.Units.assign(static_cast<std::size_t>(ClearedScreen.Width) * static_cast<std::size_t>(ClearedScreen.Height), 0);
		Vram.UploadFill(Cleared);
		// Its frames flip the draw buffers an even number of times from the first, clearing both ordering tables: the
		// frame's links so far are lost, and it leaves a black screen-sized tile behind the links still to come.
		Battle.DrawBuffer = 0;
		Links.clear();
		bCoveredByWave = true;
		WaveHold.Start();
		PendingCatchUp = MostCatchUpSteps;
	}

	void FBattleShow::MoveImage(const FVramRect& Rect, int32 X, int32 Y)
	{
		MoveBattleImage(Vram, Rect, X, Y, MoveScratch);
	}

	void FBattleShow::UploadImages(const FSpriteFile& File, uint32 At)
	{
		if (!UploadBattleImageList(Vram, File.From(0), At))
		{
			ReportFault(EBattleShowFault::Vram);
		}
	}

	void FBattleShow::RestoreVram()
	{
		if (Objects.Tables == nullptr)
		{
			ReportFault(EBattleShowFault::File);
			return;
		}
		CopyGearColumns(Vram, Objects.Tables->GearPlaces, false, MoveScratch);
	}

	void FBattleShow::PlaySound(uint32 Sound)
	{
		if (Audio != nullptr)
		{
			Audio->PlayEffect(Sound);
		}
	}

	void FBattleShow::PlaySystemSound(uint32 Sound)
	{
		if (Audio != nullptr)
		{
			Audio->PlaySystemEffect(Sound);
		}
	}

	void FBattleShow::StopSound(uint32 Sound)
	{
		if (Audio != nullptr)
		{
			Audio->StopEffect(Sound);
		}
	}

	void FBattleShow::PlaySoundOn(uint32 Sound, int32 Voice)
	{
		if (Audio != nullptr)
		{
			Audio->PlayEffectOn(Sound, Voice);
		}
	}

	void FBattleShow::SetMusicVolume(int32 Volume, int32 Steps)
	{
		if (Audio != nullptr)
		{
			Audio->SetMusicVolume(Volume, Steps);
		}
	}

	void FBattleShow::RegisterSoundEffects(const FBattleSpriteFile& File, const FBattleSpriteSection& Section)
	{
		if (Audio != nullptr)
		{
			Audio->RegisterEffects(File.Sprite().From(Section.Offset).first(Section.Size));
		}
	}

	void FBattleShow::LoadSoundBank(const FBattleSpriteFile& File, const FBattleSpriteSection& Section)
	{
		UnloadSystemSoundBank();
		if (Audio != nullptr)
		{
			Audio->LoadBattleBank(File.Sprite().From(Section.Offset).first(Section.Size));
		}
	}
}
