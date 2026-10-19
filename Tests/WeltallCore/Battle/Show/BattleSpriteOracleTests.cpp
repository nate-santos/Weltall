#include "Battle/BattleContext.h"
#include "Battle/Show/BattleHudSheet.h"
#include "Battle/Show/BattleSceneGraphics.h"
#include "Battle/Show/BattleShowCamera.h"
#include "Battle/Show/BattleShowFloor.h"
#include "Battle/Show/BattleShowNumbers.h"
#include "Battle/Show/BattleShowSprites.h"
#include "Battle/Show/BattleShowTasks.h"
#include "Battle/Show/BattleSpriteFiles.h"
#include "Battle/Show/BattleStageFiles.h"
#include "Foundation/LittleEndian.h"
#include "Game/PsxRandom.h"
#include "Graphics/Gte.h"
#include "Graphics/GteMath.h"
#include "Graphics/SpriteFile.h"
#include "Graphics/Vram.h"
#include "Media/GameFileIndex.h"
#include "Media/PsxExecutable.h"
#include "Support/GameDiscs.h"
#include "Support/BattleSpriteDigest.h"
#include "Support/OracleDigest.h"
#include "Support/XorShift32.h"

#include <catch_amalgamated.hpp>

#include <algorithm>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

using namespace Weltall;
using Weltall::Testing::FOracleDigest;
using Weltall::Testing::FXorShift32;

// Battle's sprites against the original's: the same cases drawn
// from XorShift32, the same frames, the same digest, file addresses as (file, offset).
namespace
{
	using Weltall::Testing::Elsewhere;
	using Weltall::Testing::NoFile;

	int32 Signed(uint32 Value, int32 Bits)
	{
		Value &= (1u << Bits) - 1;
		return (Value >> (Bits - 1)) != 0 ? static_cast<int32>(Value) - (1 << Bits) : static_cast<int32>(Value);
	}

	uint32 ReadU32(std::span<const uint8> Bytes, std::size_t At)
	{
		return static_cast<uint32>(Bytes[At] | Bytes[At + 1] << 8 | Bytes[At + 2] << 16 | static_cast<uint32>(Bytes[At + 3]) << 24);
	}

	/** Whether the bytes are a Battle sprite file as the oracle takes one. */
	bool LooksLikeBattleSprite(std::span<const uint8> Data)
	{
		if (Data.size() < 24)
		{
			return false;
		}
		const uint32 Count = ReadU32(Data, 0);
		if (Count < 3 || Count > 16 || 8 + 4 * Count > Data.size())
		{
			return false;
		}
		if (ReadU32(Data, 4) != 8 + 4 * Count)
		{
			return false;
		}
		for (uint32 Index = 0; Index < Count; ++Index)
		{
			if (ReadU32(Data, 8 + 4 * Index) < ReadU32(Data, 4 + 4 * Index))
			{
				return false;
			}
		}
		return ReadU32(Data, 4 + 4 * Count) <= Data.size() + 8;
	}

	/** The scene's sprites as the oracle takes them: each sprite entry's bytes, up to the next entry's start or the kept data's end. */
	std::vector<std::vector<uint8>> SceneSprites(const FBattleSceneGraphics& Graphics)
	{
		const std::span<const uint8> Bytes = Graphics.Bytes();
		std::vector<uint32> Starts{Graphics.KeptEnd()};
		for (const FBattleSceneEntry& Entry : Graphics.Entries())
		{
			Starts.push_back(Entry.Data);
		}
		std::sort(Starts.begin(), Starts.end());
		Starts.erase(std::unique(Starts.begin(), Starts.end()), Starts.end());
		std::vector<std::vector<uint8>> Found;
		for (const FBattleSceneEntry& Entry : Graphics.Entries())
		{
			if (!Entry.IsSprite() || Entry.Data >= Graphics.KeptEnd())
			{
				continue;
			}
			const uint32 End = *std::upper_bound(Starts.begin(), Starts.end(), Entry.Data);
			const std::span<const uint8> Data = Bytes.subspan(Entry.Data, std::min<std::size_t>(End, Bytes.size()) - Entry.Data);
			if (LooksLikeBattleSprite(Data))
			{
				Found.emplace_back(Data.begin(), Data.end());
			}
		}
		return Found;
	}

	std::vector<uint8> ReadDiscFile(const FIso9660& Disc, int32 Directory, uint32 Number)
	{
		const auto Executable = ReadBootExecutable(Disc);
		const auto Index = Executable ? FGameFileIndex::Read(*Executable) : TResult<FGameFileIndex>(MakeError("no executable"));
		if (!Index)
		{
			return {};
		}
		const FIndexRecord* Record = Index->Find(Directory, static_cast<int32>(Number));
		if (Record == nullptr)
		{
			return {};
		}
		auto Bytes = ReadFileSectors(Disc.Image(), *Record);
		if (!Bytes)
		{
			return {};
		}
		Bytes->resize(static_cast<std::size_t>(Record->Size));
		return std::move(*Bytes);
	}

	/** The disc's files the cases draw from. */
	struct FSpriteOracleData
	{
		/** The effects that are sprite files (five of the 251 are stand-ins). */
		std::vector<int32> Effects;
		FGteArctangents Arctangents;
		std::vector<std::pair<std::vector<uint8>, std::vector<uint8>>> Party;
		std::vector<uint8> Common;
		std::vector<std::vector<std::vector<uint8>>> Scenes;
		std::optional<FBattleHudSheet> Sheet;
		std::vector<FBattleStageFiles> Stages;
	};

	std::optional<FSpriteOracleData> ReadData(const FIso9660& Disc)
	{
		const auto Executable = ReadBootExecutable(Disc);
		if (!Executable)
		{
			return std::nullopt;
		}
		const auto Index = FGameFileIndex::Read(*Executable);
		const auto Arctangents = FGteArctangents::Read(*Executable);
		if (!Index || !Arctangents)
		{
			return std::nullopt;
		}
		FSpriteOracleData Data;
		Data.Arctangents = *Arctangents;
		const auto Files = FBattleSpriteFiles::Load(Disc.Image(), *Index);
		if (!Files)
		{
			return std::nullopt;
		}
		const auto ReadFile = [&](int32 Directory, uint32 Number) -> std::vector<uint8> {
			const FIndexRecord* Record = Index->Find(Directory, static_cast<int32>(Number));
			if (Record == nullptr)
			{
				return {};
			}
			auto Bytes = ReadFileSectors(Disc.Image(), *Record);
			if (!Bytes)
			{
				return {};
			}
			Bytes->resize(static_cast<std::size_t>(Record->Size));
			return std::move(*Bytes);
		};
		for (int32 Character = 0; Character < FBattleSpriteFiles::CharacterCount; ++Character)
		{
			const FBattleCharacterFiles& Numbers = Files->Character(Character);
			Data.Party.emplace_back(ReadFile(FBattleSpriteFiles::PartyDirectory, Numbers.Sprite), ReadFile(FBattleSpriteFiles::PartyDirectory, Numbers.Attacks));
		}
		Data.Common = ReadFile(FBattleSpriteFiles::CommonDirectory, FBattleSpriteFiles::CommonFile);
		for (int32 Effect = 0; Effect < Files->EffectCount(); ++Effect)
		{
			if (LooksLikeBattleSprite(ReadFile(FBattleSpriteFiles::EffectDirectory, static_cast<uint32>(2 * Effect + FBattleSpriteFiles::EffectFirstFile))))
			{
				Data.Effects.push_back(Effect);
			}
		}
		const FIndexRecord* Group = Index->Find(FBattleSceneGraphics::SceneDirectory, 1);
		if (Group == nullptr)
		{
			return std::nullopt;
		}
		for (int32 Scene = 0; Scene < -Group->Size / 2; ++Scene)
		{
			auto Graphics = FBattleSceneGraphics::Load(Disc.Image(), *Index, Scene);
			if (!Graphics)
			{
				return std::nullopt;
			}
			auto Enemies = SceneSprites(*Graphics);
			if (!Enemies.empty())
			{
				Data.Scenes.push_back(std::move(Enemies));
			}
		}
		auto Sheet = FBattleHudSheet::Load(Disc.Image(), *Index);
		if (!Sheet)
		{
			return std::nullopt;
		}
		Data.Sheet = std::move(*Sheet);
		for (int32 Stage = 0; Stage < FBattleStageFiles::Count(*Index); ++Stage)
		{
			auto StageFiles = FBattleStageFiles::Load(Disc.Image(), *Index, Stage);
			if (!StageFiles)
			{
				return std::nullopt;
			}
			Data.Stages.push_back(std::move(*StageFiles));
		}
		return Data;
	}

	struct FCase;

	/** What the original's stubs record, as the oracle records it. */
	class FRecorder final : public IBattleSpriteServices
	{
	public:
		FCase* Case = nullptr;
		bool bPending = false;
		bool bObjectsVisible = true;

		struct FEvent
		{
			std::string Name;
			std::vector<uint32> Values;
		};
		std::vector<FEvent> Events;

		bool FilesPending() override { return bPending; }
		bool ObjectRoot(int32, std::array<int32, 3>&) override { return false; }
		bool BonePlace(int32, int32, const FGteVector&, bool, std::array<int32, 3>&) override { return false; }
		void ReactModel(int32 Slot, uint16 FromSlots, int32 Sequence) override { Add("react", {static_cast<uint32>(Slot), FromSlots, static_cast<uint32>(Sequence)}); }
		void SetObjectsVisible(bool bVisible) override { bObjectsVisible = bVisible; }
		void LoadSpecialModel(int32 Model) override { Add("load", {static_cast<uint32>(Model & 0xFF)}); }
		void RunSpecialModel(int32 Sequence, uint16 Slots) override { Add("run", {static_cast<uint32>(Sequence), Slots}); }
		void FreeSpecialModel() override { Add("free", {}); }
		void RetintModel(const FSprite&) override {}
		void StartExtension(int32 Command, FSprite& Sprite, const FSpriteOperands& Operand, const std::array<int32, 6>& Arguments) override;
		void MoveImage(const FVramRect& Rect, int32 X, int32 Y) override
		{
			Add("move", {static_cast<uint32>(Rect.X), static_cast<uint32>(Rect.Y), static_cast<uint32>(Rect.Width), static_cast<uint32>(Rect.Height), static_cast<uint32>(X),
				static_cast<uint32>(Y)});
		}
		void UploadImages(const FSpriteFile& File, uint32 At) override;
		void RestoreVram() override { Add("restore", {}); }
		void PlaySound(uint32 Sound) override { Add("play", {Sound}); }
		void PlaySystemSound(uint32 Sound) override { Add("system", {Sound}); }
		void StopSound(uint32 Sound) override { Add("stop", {Sound}); }
		void PlaySoundOn(uint32 Sound, int32 Voice) override { Add("on", {Sound, static_cast<uint32>(Voice)}); }
		void SetMusicVolume(int32 Volume, int32 Steps) override { Add("volume", {static_cast<uint32>(Volume), static_cast<uint32>(Steps)}); }
		void RegisterSoundEffects(const FBattleSpriteFile&, const FBattleSpriteSection&) override {}
		void LoadSoundBank(const FBattleSpriteFile&, const FBattleSpriteSection&) override {}

		void Add(const char* Name, std::vector<uint32> Values) { Events.push_back({Name, std::move(Values)}); }
	};

	/** One case's show: the sprite system in battle mode and what the sprites reach. */
	struct FCase
	{
		FSpriteSystem Sprites;
		FPsxRandom Random;
		FGte Gte;
		FBattleCamera Camera;
		FBattleFloor Floor;
		FBattleFadeTask Fade;
		FBattleShakeTask Shake;
		FBattleTintLayers Tints;
		FBattleTintFadeTask TintFade;
		FBattleNumbers Numbers;
		std::unique_ptr<FBattleContext> Context = std::make_unique<FBattleContext>();
		FBattleSprites Battle;
		FRecorder Recorder;
		FVram Vram;
		/** The case's files in the order the oracle loads them, and the sprite files each holds (the section's offset beside). */
		std::vector<std::unique_ptr<FBattleSpriteFile>> Files;
		std::map<const FSpriteFile*, std::pair<uint32, uint32>> Places;
		Testing::FBattleSpriteDigester Digester;

		FCase()
		{
			Digester.Sprites = &Sprites;
			Digester.Battle = &Battle;
			Digester.Place = [this](const FSpriteFile* File, uint32 Offset) { return Place(File, Offset); };
			Recorder.Case = this;
			Fade.Sprites = &Sprites;
			Shake.Sprites = &Sprites;
			TintFade.Sprites = &Sprites;
			TintFade.Layers = &Tints;
			Numbers.Sprites = &Sprites;
			Numbers.Random = &Random;
			Battle.Sprites = &Sprites;
			Battle.Random = &Random;
			Battle.Gte = &Gte;
			Battle.Context = Context.get();
			Battle.Camera = &Camera;
			Battle.Floor = &Floor;
			Battle.Fade = &Fade;
			Battle.Shake = &Shake;
			Battle.TintFade = &TintFade;
			Battle.Numbers = &Numbers;
			Battle.Services = &Recorder;
			Sprites.Random = &Random;
		}

		const FBattleSpriteFile& Load(const std::vector<uint8>& Bytes)
		{
			auto Parsed = FBattleSpriteFile::Parse(Bytes);
			REQUIRE(Parsed);
			Files.push_back(std::make_unique<FBattleSpriteFile>(std::move(*Parsed)));
			FBattleSpriteFile& File = *Files.back();
			const uint32 Number = static_cast<uint32>(Files.size() - 1);
			Places[&File.Sprite()] = {Number, 0};
			for (std::size_t Section = 0; Section < File.Sections().size(); ++Section)
			{
				if (const FSpriteFile* Resource = File.SectionSprite(Section))
				{
					Places[Resource] = {Number, File.Sections()[Section].Offset};
				}
			}
			Battle.WritableFiles.push_back(&File.Sprite());
			return File;
		}

		std::pair<uint32, uint32> Place(const FSpriteFile* File, uint32 Offset) const
		{
			if (File == nullptr)
			{
				return {NoFile, 0};
			}
			const auto Found = Places.find(File);
			if (Found == Places.end())
			{
				return {Elsewhere, 0};
			}
			return {Found->second.first, Found->second.second + Offset};
		}

		int32 IndexOf(const FSprite* Sprite) const { return Digester.IndexOf(Sprite); }
		int32 IndexOfTask(const FSpriteTask* Task) const { return Digester.IndexOfTask(Task); }
		void DigestPlace(FOracleDigest& Out, const FSpriteFile* File, uint32 Offset) const { Digester.DigestPlace(Out, File, Offset); }
		void DigestResource(FOracleDigest& Out, const FSpriteResource& Resource) const { Digester.DigestResource(Out, Resource); }
		void DigestSprite(FOracleDigest& Out, const FSprite& Sprite) const { Digester.DigestSprite(Out, Sprite); }

		void Digest(FOracleDigest& Out)
		{
			Digester.DigestTasks(Out, Fade, Shake, TintFade, Numbers);
			Out.U8(0xFF);
			Out.I16(Camera.Eye[0], Camera.Eye[1], Camera.Eye[2], Camera.Target[0], Camera.Target[1], Camera.Target[2]);
			Out.I16(Camera.DesiredEye[0], Camera.DesiredEye[1], Camera.DesiredEye[2], Camera.DesiredTarget[0], Camera.DesiredTarget[1], Camera.DesiredTarget[2]);
			Out.I32(Camera.Mode, Camera.FramedSlots, Camera.FramingDistance, Camera.Distance);
			Out.I16(Camera.SpriteDistance, Camera.ViewPitch, Camera.ViewYaw, 0);
			Out.I32(Camera.EyePoint[0], Camera.EyePoint[1], Camera.EyePoint[2], Camera.TargetPoint[0], Camera.TargetPoint[1], Camera.TargetPoint[2]);
			Out.I16(Shake.Shake.Offset[0], Shake.Shake.Offset[1], Shake.Shake.Offset[2]);
			for (const FBattleTintLayers::FLayer& Layer : Tints.Layers)
			{
				Out.U8(Layer.bOn, Layer.Mode, Layer.Level, Layer.Color[0], Layer.Color[1], Layer.Color[2]);
			}
			Out.I16(IndexOfTask(Battle.EyeController), IndexOfTask(Battle.TargetController));
			Out.U8(Battle.ControllerCount);
			Out.I16(Battle.SavedDesiredEye[0], Battle.SavedDesiredEye[1], Battle.SavedDesiredEye[2], Battle.SavedDesiredTarget[0], Battle.SavedDesiredTarget[1],
				Battle.SavedDesiredTarget[2]);
			Out.U8(Camera.bIgnoresHeights, Fade.Fade.bBlocked);
			Out.U8(Battle.Play.bCastEnded, Battle.Play.bReloadEffectImages);
			Out.I16(Battle.Play.HitVoiced);
			Out.U8(Battle.bCombatantsHidden);
			Out.I16(IndexOf(Battle.Play.Caster));
			Out.U8(Battle.Play.ScriptMark);
			Out.I32(Battle.CountedWaits, Battle.SpecialModelSteps);
			Out.U8(Battle.bStageHidden);
			Out.U8(Battle.Clear[0], Battle.Clear[1], Battle.Clear[2], Battle.Clear[3], Battle.Clear[0], Battle.Clear[1], Battle.Clear[2], Battle.Clear[3]);
			Out.U8(Battle.Play.bHudHidden, Battle.Play.bMusicLowered, Recorder.bObjectsVisible);
			Out.I32(Gte.OffsetX >> 16, Gte.OffsetY >> 16);
			Out.I32(Sprites.CountedTasks, Sprites.CountedUpdates, Sprites.HitStop);
			Out.U8(Sprites.RequestedExtension);
			Out.I32(Random.Seed());
			DigestResource(Out, Battle.CommonEffects);
			DigestResource(Out, Battle.EffectResource);
			for (const FRecorder::FEvent& Event : Recorder.Events)
			{
				Out.U8(0xEE, Event.Values.size() + 1);
				Out.Raw(std::span<const uint8>(reinterpret_cast<const uint8*>(Event.Name.data()), Event.Name.size()));
				for (const uint32 Value : Event.Values)
				{
					Out.I32(Value);
				}
			}
			for (const FSpriteUpload& Upload : Sprites.Uploads())
			{
				if (!Upload.bNow)
				{
					DigestPlace(Out, Upload.File, Upload.Offset);
					Out.I16(Upload.Rect.X, Upload.Rect.Y, Upload.Rect.Width, Upload.Rect.Height);
				}
			}
			Out.U8(0xFD);
			for (const FSpriteUpload& Upload : Sprites.Uploads())
			{
				if (Upload.bNow)
				{
					DigestPlace(Out, Upload.File, Upload.Offset);
					Out.I16(Upload.Rect.X, Upload.Rect.Y, Upload.Rect.Width, Upload.Rect.Height);
				}
			}
			Out.U8(0xFC);
			Recorder.Events.clear();
			Sprites.FlushUploads(Vram);
		}

	};

	/** An effect's start, its part with the sprites, as the oracle runs it. */
	void StartEffect(FCase& C, FXorShift32& G, FSprite& Actor, const FBattleSpriteFile& Effect)
	{
		C.Battle.Play.bCastEnded = false;
		C.Sprites.ParseResource(C.Battle.EffectResource, Effect.Sprite(), 0x380, 0x100, 0, 0x1F4);
		const FSpriteResource& Resource = C.Battle.EffectResource;
		const bool bResident = Resource.File->IsResident(Resource.Tables);
		const int32 Animation = static_cast<int32>(G.Next() % static_cast<uint32>(Resource.File->AnimationCount(Resource.Tables)));
		FSprite* Sprite = &Actor;
		if (!bResident)
		{
			Actor.AlternateFile = &Effect.Sprite();
			C.Sprites.SetAnimation(Actor, -1);
		}
		else
		{
			Sprite = C.Sprites.SpawnChild(Actor, *Resource.File, Resource.File->AnimationHeader(Resource.Tables, Animation), &C.Battle.EffectResource);
			REQUIRE(Sprite);
		}
		const int32 SoundSet = C.Battle.Install(Effect);
		Sprite->SoundSet = SoundSet;
		Actor.SoundSet = SoundSet;
	}

	void FRecorder::StartExtension(int32 Command, FSprite& Sprite, const FSpriteOperands& Operand, const std::array<int32, 6>& Arguments)
	{
		const uint32 Index = static_cast<uint32>(Case->IndexOf(&Sprite));
		if (Command == 0x19)
		{
			Add("extension", {static_cast<uint32>(Command), Index, static_cast<uint32>(Arguments[0]), static_cast<uint32>(Arguments[1]), static_cast<uint32>(Arguments[2]),
				static_cast<uint32>(Arguments[3]), static_cast<uint32>(Arguments[4]), static_cast<uint32>(Arguments[5])});
		}
		else if (Command == 0x1D)
		{
			Add("extension", {static_cast<uint32>(Command), 0xFFFF, static_cast<uint32>(Arguments[0]), static_cast<uint32>(Arguments[1]), static_cast<uint32>(Arguments[2]),
				static_cast<uint32>(Arguments[3]), static_cast<uint32>(Arguments[4]), 0});
		}
		else if (Command == 0x24 || Command == 0x25)
		{
			const auto [Number, At] = Case->Place(Sprite.ScriptFile, Operand.At);
			Add("extension", {static_cast<uint32>(Command), Index, Number, At});
		}
		else
		{
			Add("extension", {static_cast<uint32>(Command)});
		}
	}

	void FRecorder::UploadImages(const FSpriteFile& File, uint32 At)
	{
		const auto [Number, Offset] = Case->Place(&File, At);
		Add("upload", {Number, Offset});
	}
}

namespace
{
	/**
	 * The oracle's cases: the combatants and an action's actor, then frames that start the actor's
	 * attacks (or, with bEffects, an effect the case read) and the combatants' own animations.
	 */
	std::string RunSpriteCases(const FIso9660& Disc, const FSpriteOracleData& DataRef, uint32 Seed, int32 Cases, bool bEffects)
	{
	const FSpriteOracleData* Data = &DataRef;
	constexpr int32 MostFrames = 120;
	constexpr std::array<std::array<int16, 2>, 3> PartyTextures{{{0x100, 0x1C0}, {0x118, 0x1C0}, {0x130, 0x1C0}}};
	FXorShift32 G(Seed);
	FOracleDigest Whole;
	for (int32 CaseNumber = 0; CaseNumber < Cases; ++CaseNumber)
	{
		INFO("case " << CaseNumber);
		FOracleDigest One;
		auto CasePointer = std::make_unique<FCase>();
		FCase& C = *CasePointer;
		C.Numbers.Sheet = &*Data->Sheet;
		C.Numbers.Reserve();
		C.Numbers.bPlaying = true;
		C.Battle.Arctangents = &Data->Arctangents;
		C.Sprites.Arctangents = &Data->Arctangents;

		// reset()
		C.Random = FPsxRandom(G.Next());
		C.Battle.Begin();
		C.Battle.Play.bHitVoices = G.Next() % 2 != 0;
		const std::array<uint8, 4> Clear{static_cast<uint8>(G.Next() % 2), static_cast<uint8>(G.Next()), static_cast<uint8>(G.Next()), static_cast<uint8>(G.Next())};
		C.Battle.Clear = Clear;

		// stage()
		const FBattleStageFiles& Stage = Data->Stages[G.Next() % 75];
		C.Floor.Reset(Stage.Record.FloorVertices(), Stage.Record.FloorTriangles());

		// camera()
		C.Camera.Init();
		for (FGteVector* Point : {&C.Camera.Eye, &C.Camera.Target, &C.Camera.DesiredEye, &C.Camera.DesiredTarget})
		{
			for (int16& Axis : *Point)
			{
				Axis = static_cast<int16>(Signed(G.Next(), 11));
			}
		}
		C.Camera.Pitch = 0xC0;
		C.Camera.bLocked = G.Next() % 8 == 0;
		C.Camera.FramingDistance = 0;
		C.Camera.SpriteDistance = static_cast<uint16>(0x100 + G.Next() % 0x700);
		C.Camera.Distance = static_cast<int32>(0x100 + G.Next() % 0x1000);
		C.Camera.ViewPitch = static_cast<int16>(G.Next() & 0xFFF);
		C.Camera.ViewYaw = static_cast<int16>(G.Next() & 0xFFF);
		for (std::array<int32, 3>* Point : {&C.Camera.EyePoint, &C.Camera.TargetPoint})
		{
			for (int32& Axis : *Point)
			{
				Axis = Signed(G.Next(), 11) * 0x10000;
			}
		}
		C.Gte.OffsetX = 0xA0 << 16;
		C.Gte.OffsetY = 0xA4 << 16;
		C.Gte.ProjectionDistance = 0x200;

		// The slots.
		const int32 Party = static_cast<int32>(1 + G.Next() % 3);
		std::vector<int32> Characters;
		for (int32 Member = 0; Member < Party; ++Member)
		{
			Characters.push_back(static_cast<int32>(G.Next() % Data->Party.size()));
		}
		const auto& Enemies = Data->Scenes[G.Next() % Data->Scenes.size()];
		const int32 EnemyCount = static_cast<int32>(1 + G.Next() % 5);
		std::vector<int32> Kinds;
		for (int32 Enemy = 0; Enemy < EnemyCount; ++Enemy)
		{
			Kinds.push_back(static_cast<int32>(G.Next() % Enemies.size()));
		}
		for (int32 Slot = 0; Slot < FBattleSprites::SlotCount; ++Slot)
		{
			FBattleSlot& Record = C.Context->Slots[static_cast<std::size_t>(Slot)];
			const bool bOccupied = Slot < Party || (Slot >= 3 && Slot < 3 + EnemyCount);
			Record = FBattleSlot{};
			if (bOccupied)
			{
				Record.Occupant = static_cast<uint8>(Slot < 3 ? Characters[static_cast<std::size_t>(Slot)] : Kinds[static_cast<std::size_t>(Slot - 3)]);
				Record.Presence = Slot >= 3 && G.Next() % 8 == 0 ? 0x80 : 0;
				Record.Position.X = static_cast<uint16>(Signed(G.Next(), 11));
				Record.Position.Z = static_cast<uint16>(Signed(G.Next(), 11));
			}
			else
			{
				Record.Occupant = FBattleSlot::Empty;
			}
		}

		// The files and the combatants.
		std::vector<FSprite*> Combatants;
		for (int32 Slot = 0; Slot < Party; ++Slot)
		{
			const FBattleSpriteFile& File = C.Load(Data->Party[static_cast<std::size_t>(Characters[static_cast<std::size_t>(Slot)])].first);
			FBattleCombatantLook Look;
			Look.ClutY = Slot + 0x1C0;
			Look.TextureX = PartyTextures[static_cast<std::size_t>(Slot)][0];
			Look.TextureY = PartyTextures[static_cast<std::size_t>(Slot)][1];
			Look.Animation = 1;
			Look.bFacesLeft = G.Next() % 2 == 0;
			Combatants.push_back(&C.Battle.CreateCombatant(Slot, File.Sprite(), Look));
		}
		const int32 ActorSlot = static_cast<int32>(G.Next() % static_cast<uint32>(Party));
		const FBattleSpriteFile& Attacks = C.Load(Data->Party[static_cast<std::size_t>(Characters[static_cast<std::size_t>(ActorSlot)])].second);
		const FBattleSpriteFile& Common = C.Load(Data->Common);
		C.Sprites.ParseResource(C.Battle.CommonEffects, Common.Sprite(), 0x380, 0, 0, 0x1D1);
		C.Battle.SystemSoundSet = static_cast<int32>(G.Next() & 0xFFFF);
		std::map<int32, const FBattleSpriteFile*> EnemyFiles;
		for (int32 Number = 0; Number < EnemyCount; ++Number)
		{
			const int32 Kind = Kinds[static_cast<std::size_t>(Number)];
			if (EnemyFiles.find(Kind) == EnemyFiles.end())
			{
				EnemyFiles[Kind] = &C.Load(Enemies[static_cast<std::size_t>(Kind)]);
			}
			FBattleCombatantLook Look;
			Look.ClutY = 3 + Number + 0x1C0;
			Look.TextureX = 0x140 + 0x40 * (Kind % 8);
			Look.TextureY = 0x100;
			Look.Animation = 1;
			Look.bFacesLeft = G.Next() % 2 == 0;
			Combatants.push_back(&C.Battle.CreateCombatant(3 + Number, EnemyFiles[Kind]->Sprite(), Look));
		}

		// Each combatant faces one of the other side.
		for (std::size_t Position = 0; Position < Combatants.size(); ++Position)
		{
			const std::size_t First = Position < static_cast<std::size_t>(Party) ? static_cast<std::size_t>(Party) : 0;
			const std::size_t Count = Position < static_cast<std::size_t>(Party) ? Combatants.size() - static_cast<std::size_t>(Party) : static_cast<std::size_t>(Party);
			Combatants[Position]->Target = Combatants[First + G.Next() % Count];
		}

		// The action.
		FSprite* Actor = Combatants[static_cast<std::size_t>(ActorSlot)];
		std::vector<FSprite*> Targets;
		for (int32 Position = 0; Position < EnemyCount; ++Position)
		{
			if (G.Next() % 2 != 0 || Position == 0)
			{
				Targets.push_back(Combatants[static_cast<std::size_t>(Party + Position)]);
			}
		}
		C.Battle.Play.Actor = Actor;
		Actor->Target = Targets[0];
		C.Battle.Play.Targets = {};
		for (std::size_t Target = 0; Target < Targets.size(); ++Target)
		{
			C.Battle.Play.Targets[Target] = Targets[Target];
		}
		C.Battle.Play.TargetCount = static_cast<int32>(Targets.size());
		uint16 Mask = 0;
		for (const FSprite* Target : Targets)
		{
			Mask = static_cast<uint16>(Mask | 1 << FBattleSprites::SlotOf(*Target));
		}
		C.Battle.Play.TargetMask = Mask;
		const int32 RecordNumber = static_cast<int32>(G.Next() % 32);
		FBattlePerformance& Performance = C.Context->Performances[static_cast<std::size_t>(RecordNumber)];
		constexpr std::array<uint8, 10> Results{0, 1, 2, 3, 4, 5, 6, 7, 10, 0xFF};
		constexpr std::array<uint8, 6> Classes{0, 1, 2, 3, 5, 0xB};
		for (uint16& Amount : Performance.Amounts)
		{
			Amount = static_cast<uint16>(G.Next() % 10000);
		}
		for (uint8& Result : Performance.Results)
		{
			Result = Results[G.Next() % Results.size()];
		}
		for (uint16& Total : Performance.Totals)
		{
			Total = static_cast<uint16>(G.Next());
		}
		for (uint8& Class : Performance.TotalKinds)
		{
			Class = Classes[G.Next() % Classes.size()];
		}
		C.Battle.Play.Record = RecordNumber + 1;
		C.Context->Party.bStoredCombo = G.Next() % 2 != 0;
		Actor->SoundSet = C.Battle.Install(Attacks);
		Actor->AlternateFile = &Attacks.Sprite();
		const int32 AttackCount = Attacks.Sprite().AnimationCount(Attacks.Sprite().Tables());
		const FBattleSpriteFile* Effect = nullptr;
		if (bEffects)
		{
			const int32 Number = Data->Effects[G.Next() % Data->Effects.size()];
			Effect = &C.Load(ReadDiscFile(Disc, FBattleSpriteFiles::EffectDirectory, static_cast<uint32>(2 * Number + FBattleSpriteFiles::EffectFirstFile)));
		}

		// The frames.
		C.Digest(One);
		const uint32 Frames = 1 + G.Next() % MostFrames;
		for (uint32 Frame = 0; Frame < Frames; ++Frame)
		{
			const uint32 Choice = G.Next() % 32;
			if (Choice < 2 && bEffects)
			{
				StartEffect(C, G, *Actor, *Effect);
			}
			else if (Choice < 2)
			{
				C.Sprites.SetAnimation(*Actor, ~static_cast<int32>(G.Next() % static_cast<uint32>(AttackCount)));
			}
			else if (Choice == 2)
			{
				FSprite& Sprite = *Combatants[G.Next() % Combatants.size()];
				const int32 Count = Sprite.Resource->File->AnimationCount(Sprite.Resource->Tables);
				C.Sprites.SetAnimation(Sprite, static_cast<int32>(G.Next() % static_cast<uint32>(Count)));
			}
			else if (Choice == 3)
			{
				Actor->Target = Targets[G.Next() % Targets.size()];
			}
			else if (Choice == 4)
			{
				C.Recorder.bPending = G.Next() % 2 != 0;
			}
			C.Sprites.BuildPendingFrames();
			C.Sprites.RunTasks();
			C.Camera.Step(C.Battle.CameraSubjects(), C.Gte, Data->Arctangents);
			C.Camera.BuildView(C.Shake.Shake.Offset);
			C.Digest(One);
			INFO("frame " << Frame << ": fault " << static_cast<int32>(C.Sprites.Fault()) << ", " << C.Sprites.FaultValue());
			REQUIRE(C.Sprites.Fault() == ESpriteFault::None);
			// Every fade here is a script's, which the Battle flashes setting draws as the player has them.
			if (C.Fade.Fade.bOn && !C.Fade.Fade.bFromEffect)
			{
				FAIL("a fade a sprite script started is not marked as an effect's");
			}
		}
		C.Battle.End();
		Whole.Raw(One.Finish());
	}
	return Whole.Hex();
	}
}

TEST_CASE("Battle's sprites run their scripts as the original's: combatants, attacks and what they start", "[Battle][Oracle][Content]")
{
	REQUIRE_DISC(Disc, EDiscRelease::UsaDisc1);
	const std::optional<FSpriteOracleData> Data = ReadData(*Disc);
	REQUIRE(Data);
	CHECK(RunSpriteCases(*Disc, *Data, 0x5B17E007, 60, false) == "7eb22ffe5f77052bacd75b800415c84595f6955f");
}

TEST_CASE("Battle's effects run their sprites as the original's: the effect's resource, its children and its sections", "[Battle][Oracle][Content]")
{
	REQUIRE_DISC(Disc, EDiscRelease::UsaDisc1);
	const std::optional<FSpriteOracleData> Data = ReadData(*Disc);
	REQUIRE(Data);
	CHECK(RunSpriteCases(*Disc, *Data, 0x5B17E008, 40, true) == "a4512a7565f295d7fcf66db6cb0332c3b8554dcf");
}
