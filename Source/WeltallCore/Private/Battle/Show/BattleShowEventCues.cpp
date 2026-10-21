#include "Battle/BattleContext.h"
#include "Battle/BattleEvent.h"
#include "Battle/BattleEventState.h"
#include "Battle/Show/BattleEventFiles.h"
#include "Battle/Show/BattleShow.h"
#include "Battle/Show/BattleShowPlayback.h"
#include "Foundation/LittleEndian.h"
#include "Graphics/SpriteFile.h"

#include <algorithm>

namespace Weltall
{
	namespace
	{
		/** Where a `seds` file keeps its set's id. */
		constexpr std::size_t SoundSetIdOffset = 0x14;
		/** A walk's and a jump's animations, and how near its goal a walk ends. */
		constexpr int32 EventWalkAnimation = 2;
		constexpr int32 EventJumpAnimation = 3;
		constexpr int32 EventWalkReach = 8;
		/** The part count a sprite's built frame holds in its build flags. */
		constexpr uint32 BuiltPartsMask = 0xFC;
		/** Every slot, for a special attack's targets. */
		constexpr uint16 EverySlot = 0xFFFF;

		int16 Signed(uint16 Value)
		{
			return static_cast<int16>(Value);
		}

		uint16 SoundSetOf(std::span<const uint8> Seds)
		{
			return Seds.size() >= SoundSetIdOffset + 2 ? LittleEndian::ReadU16(Seds, SoundSetIdOffset) : uint16{0};
		}
	}

	void FBattlePlayback::FEventMotionEnd::OnAnimationEvent(FSprite& Sprite)
	{
		FinishBattleEventMotion(*Owner.Show.Context, static_cast<uint8>(FBattleSprites::SlotOf(Sprite)));
		Sprite.Listener = nullptr;
	}

	void FBattlePlayback::BeginEvent(const FBattleEventFiles& Files)
	{
		EventFiles = &Files;
		if (Show.Audio != nullptr)
		{
			Show.Audio->RegisterEffects(Files.Sounds());
		}
		bEventSounds = true;
		Show.LoadSystemSoundBank();
	}

	int32 FBattlePlayback::EventSoundSet() const
	{
		return bEventSounds && EventFiles != nullptr ? SoundSetOf(EventFiles->Sounds()) : -1;
	}

	int32 FBattlePlayback::EventObjectOf(const FSpriteFile* File) const
	{
		const auto Found = std::find_if(EventObjectFiles.begin(), EventObjectFiles.end(), [File](const FEventObjectFile& Kept) { return Kept.File.get() == File; });
		return Found != EventObjectFiles.end() ? Found->Record : -1;
	}

	FSprite* FBattlePlayback::EventSprite(uint16 Slot)
	{
		FSprite* Sprite = SlotSprite(Slot);
		if (Sprite == nullptr)
		{
			// An empty slot: the original works on whatever the slot's cleared pointer reaches.
			Fail(static_cast<uint8>(EBattleShowFault::EndedTask));
		}
		return Sprite;
	}

	void FBattlePlayback::PlayEventCue(const FBattleEventCue& Cue)
	{
		const std::array<uint16, 5>& Args = Cue.Args;
		switch (Cue.Cue)
		{
		case EBattleEventCue::Fade:
			Show.Fade.Start(Args[0], Args[1], static_cast<uint8>(Args[2]), static_cast<uint8>(Args[3]), static_cast<uint8>(Args[4]));
			break;
		case EBattleEventCue::Shake:
			Show.Shake.Start({Signed(Args[0]), Signed(Args[1]), Signed(Args[2])}, Args[3]);
			break;
		case EBattleEventCue::Perform:
			Show.Objects.Perform(Args[0], Args[1], Args[2]);
			break;
		case EBattleEventCue::PerformSignalled:
			Show.Objects.PerformSignalled(Args[0], Args[1], Args[2]);
			break;
		case EBattleEventCue::FinishPerform:
			CallFromRules(EPlayRoutine::EventFinishPerform);
			break;
		case EBattleEventCue::LoadObject:
			LoadEventObject(Args[0], Args[1]);
			break;
		case EBattleEventCue::ObjectAnim:
			if (Args[0] < EventObjects.size() && EventObjects[Args[0]] != nullptr)
			{
				Show.Sprites.SetAnimation(EventObjects[Args[0]]->Sprite, Signed(Args[1]));
			}
			break;
		case EBattleEventCue::FreeObject:
			FreeEventObject(Args[0]);
			break;
		case EBattleEventCue::LockCamera:
			LockEventCamera(true);
			break;
		case EBattleEventCue::StopSong:
		case EBattleEventCue::StartMusic:
		case EBattleEventCue::StopMusic:
		case EBattleEventCue::FreeMusic:
			Fail(static_cast<uint8>(EBattleShowFault::EventMusic));
			break;
		case EBattleEventCue::MusicVolume:
			Show.SetMusicVolume(Args[0], Args[1]);
			break;
		case EBattleEventCue::PlaySound:
			if (Show.Audio != nullptr)
			{
				Show.Audio->PlayEffectAt(EventSound(Args[0], Args[3] == 0), Signed(Args[1]), Signed(Args[2]));
			}
			break;
		case EBattleEventCue::SoundVolume:
			if (Show.Audio != nullptr)
			{
				Show.Audio->SetEffectVolume(EventSound(Args[0], Args[2] == 0), Signed(Args[1]));
			}
			break;
		case EBattleEventCue::SetAnim:
			SetEventAnimation(Args[0], Signed(Args[1]));
			break;
		case EBattleEventCue::RestoreAnim:
			if (FSprite* Sprite = EventSprite(Args[0]))
			{
				Show.Sprites.SetAnimation(*Sprite, Sprite->Animation());
			}
			break;
		case EBattleEventCue::StopMotion:
			if (FSprite* Sprite = EventSprite(Args[0]))
			{
				// Nothing of it is drawn any more: its frame none, its built parts none.
				Sprite->Wait = 0;
				Sprite->Frame = 0;
				Sprite->BuildFlags &= ~BuiltPartsMask;
			}
			break;
		case EBattleEventCue::StopTurn:
			if (FSprite* Sprite = EventSprite(Args[0]))
			{
				Sprite->Wait = 0;
			}
			break;
		case EBattleEventCue::Walk:
			if (FSprite* Sprite = EventSprite(Args[0]))
			{
				Sprite->SavedPoint = {Signed(Args[1]), Signed(Args[2]), Signed(Args[3])};
				Show.Sprites.SetDirection(*Sprite, HeadingToGoal(*Sprite));
				Show.Sprites.SetFacing(*Sprite, HeadingToGoal(*Sprite));
				Show.Sprites.SetAnimation(*Sprite, EventWalkAnimation);
				Show.Battle.StartWalkWatch(*Sprite, EventWalkReach);
				Sprite->Listener = &EventMotionEnd;
			}
			break;
		case EBattleEventCue::Jump:
			if (FSprite* Sprite = EventSprite(Args[0]))
			{
				Sprite->SavedPoint = {Signed(Args[1]), Signed(Args[2]), Signed(Args[3])};
				Show.Sprites.SetAnimation(*Sprite, EventJumpAnimation);
				Sprite->Listener = &EventMotionEnd;
			}
			break;
		case EBattleEventCue::Attack:
			CallFromRules(EPlayRoutine::EventAttack, Args[0] | Args[1] << 8, Signed(Args[2]));
			break;
		case EBattleEventCue::SpecialAttack:
			CallFromRules(EPlayRoutine::EventSpecialAttack, Args[0], Args[2]);
			break;
		case EBattleEventCue::FinishLoads:
			FinishLoads();
			break;
		case EBattleEventCue::PlayVoice:
			CallFromRules(EPlayRoutine::EventVoice, Args[0], Args[1]);
			break;
		case EBattleEventCue::CancelPerformSignals:
			for (int32 Slot = 0; Slot < FBattleObjects::CombatantSlots; ++Slot)
			{
				if (FBattleObject* Object = Show.Objects.Object(Slot))
				{
					Object->Signal = 0;
				}
			}
			break;
		case EBattleEventCue::ReleaseSounds:
			if (Show.Audio != nullptr && EventFiles != nullptr)
			{
				Show.Audio->StopEffects(EventFiles->Sounds());
				Show.Audio->UnregisterEffects(EventFiles->Sounds());
			}
			bEventSounds = false;
			break;
		case EBattleEventCue::OpenCommandHud:
		case EBattleEventCue::CloseCommandHud:
		case EBattleEventCue::OpenWindow:
		case EBattleEventCue::ShowPortrait:
		case EBattleEventCue::CloseWindow:
			// The HUD's, which the program's show hands it.
			break;
		}
	}

	uint32 FBattlePlayback::EventSound(uint16 Effect, bool bOwn)
	{
		const int32 Set = bOwn ? EventSoundSet() : Show.Battle.SystemSoundSet;
		if (Set < 0)
		{
			// Its set's file is not in memory: the original takes the id from whatever its pointer reaches.
			Fail(static_cast<uint8>(bOwn ? EBattleShowFault::EventSounds : EBattleShowFault::SystemSounds));
			return 0;
		}
		return static_cast<uint32>(Set) << 16 | Effect;
	}

	// The event's objects ---------------------------------------------------------------------------------------

	void FBattlePlayback::LoadEventObject(uint16 Record, uint16 Entry)
	{
		if (Record >= EventObjects.size() || EventFiles == nullptr)
		{
			Fail(static_cast<uint8>(EBattleShowFault::Playback), "a battle event loads an object past its records");
			return;
		}
		TResult<std::vector<uint8>> Data = EventFiles->UnpackObject(Entry);
		TResult<FSpriteFile> File = Data ? FSpriteFile::ParseWithSections(*Data) : TResult<FSpriteFile>(Data.Error());
		if (!File)
		{
			Fail(static_cast<uint8>(EBattleShowFault::File));
			return;
		}
		// Its data stays for the battle: its scripts write their variables into it, and a sprite may outlive its object.
		FEventObjectFile& Kept = EventObjectFiles.emplace_back();
		Kept.File = std::make_unique<FSpriteFile>(std::move(*File));
		Kept.Record = Record;
		Show.Battle.WritableFiles.push_back(Kept.File.get());
		// The original places it by three words of its stack that nothing has set.
		EventObjects[Record] = &Show.Battle.CreateEventObject(*Kept.File, {0, 0, 0});
		LockEventCamera(true);
	}

	void FBattlePlayback::FreeEventObject(uint16 Record)
	{
		if (Record >= EventObjects.size() || EventObjects[Record] == nullptr)
		{
			return;
		}
		Show.Battle.DestroyEventObject(*EventObjects[Record]);
		EventObjects[Record] = nullptr;
		LockEventCamera(false);
	}

	void FBattlePlayback::LockEventCamera(bool bLocked)
	{
		const int32 Mode = bLocked ? BattleCameraModes::Still : BattleCameraModes::Framing;
		Show.Camera.ModeAfter = Mode;
		Show.Camera.SetMode(Mode);
		Show.Camera.bLocked = bLocked;
	}

	// The combatants' sprites -------------------------------------------------------------------------------------

	void FBattlePlayback::SetEventAnimation(uint16 Slot, int32 Animation)
	{
		Show.Battle.Play.Record = 1;
		FSprite* Sprite = EventSprite(Slot);
		if (Sprite == nullptr)
		{
			return;
		}
		if (Animation < 0 && !bAttackFileInstalled && ActionFileData)
		{
			// An animation of the action's file, whose sections are put to use first.
			Sprite->SoundSet = InstallAttackFile();
			Sprite->AlternateFile = &ActionFileData->Sprite();
		}
		Show.Sprites.SetAnimation(*Sprite, Animation);
		Sprite->Listener = &EventMotionEnd;
		Sprite->NextWord = (Sprite->NextWord & ~0xFFu) | (Animation == 1 ? 1u : 0xFFu);
	}

	EShowFlow FBattlePlayback::RunEventAttack(FShowRoutine& Routine)
	{
		const uint16 Slot = static_cast<uint16>(Routine.Value & 0xFF);
		switch (Routine.Resume)
		{
		case 0:
		{
			// The play aims at the target alone, the attacker turned to it; a party member's attack file read.
			FSprite* Sprite = EventSprite(Slot);
			FSprite* Target = EventSprite(static_cast<uint16>(Routine.Value >> 8 & 0xFF));
			if (Sprite == nullptr || Target == nullptr)
			{
				return EShowFlow::Done;
			}
			FBattlePlayState& State = Show.Battle.Play;
			State.Targets[1] = nullptr;
			State.Targets[0] = Target;
			Sprite->Target = Target;
			Show.Sprites.SetFacing(*Sprite, HeadingTo(*Sprite, *Target));
			Show.Sprites.SetDirection(*Sprite, HeadingTo(*Sprite, *Target));
			if (Slot < FBattleContext::PartySlotCount)
			{
				return Call(Routine, 1, EPlayRoutine::ReadAttackFile, Slot);
			}
			[[fallthrough]];
		}
		case 1:
			SetEventAnimation(Slot, Routine.Other);
			return EShowFlow::Done;
		default:
			return EShowFlow::Done;
		}
	}

	EShowFlow FBattlePlayback::RunEventSpecialAttack(FShowRoutine& Routine)
	{
		FBattlePlayState& State = Show.Battle.Play;
		FSprite* Sprite = EventSprite(static_cast<uint16>(Routine.Value));
		if (Sprite == nullptr)
		{
			return EShowFlow::Done;
		}
		switch (Routine.Resume)
		{
		case 0:
			// The cast from the plain pose, unless nothing of the sprite shows.
			Sprite->SetAnimationNumber(Sprite->Frame == 0 ? 0 : CastAnimation);
			return Call(Routine, 1, EPlayRoutine::EffectCast, Routine.Other, Routine.Value);
		case 1:
			// Everyone the sprites stand for is its target.
			State.TargetMask = EverySlot;
			State.TargetCount = ListTargetSprites(EverySlot, State.Targets, Sprite);
			if (State.TargetCount == 0)
			{
				State.Targets[0] = Sprite;
			}
			Sprite->Target = State.Targets[0];
			return Call(Routine, 2, EPlayRoutine::WaitCounted);
		case 2:
			Show.Sprites.CountedTasks = 0;
			Show.Sprites.bCountingTasks = true;
			if (StartEffect())
			{
				Sprite->Listener = &AnimationEnd;
			}
			else if (Show.Fault() == EBattleShowFault::None)
			{
				if (Sprite->Frame != 0)
				{
					Show.Sprites.SetAnimation(*Sprite, EffectWaitAnimation);
				}
				Show.FrameSlots(State.TargetMask);
			}
			return Call(Routine, 3, EPlayRoutine::WaitCounted);
		case 3:
			Show.Sprites.CountedTasks = 0;
			Show.Sprites.bCountingTasks = false;
			return EShowFlow::Done;
		default:
			return EShowFlow::Done;
		}
	}

	EShowFlow FBattlePlayback::RunEventFinishPerform(FShowRoutine& Routine)
	{
		switch (Routine.Resume)
		{
		case 0:
			return Call(Routine, 1, EPlayRoutine::WaitLoads);
		default:
			Show.LoadSystemSoundBank();
			return EShowFlow::Done;
		}
	}

	// A voice ----------------------------------------------------------------------------------------------------

	EShowFlow FBattlePlayback::RunEventVoice(FShowRoutine& Routine)
	{
		switch (Routine.Resume)
		{
		case 0:
		{
			const int32 Set = Routine.Value;
			if (EventFiles == nullptr || !FBattleEventFiles::IsVoiceSet(Set))
			{
				// A set past the program's table: the original reads its files' numbers from what follows it.
				Fail(static_cast<uint8>(EBattleShowFault::Playback), "a battle event plays a voice set past the program's table");
				return EShowFlow::Done;
			}
			// The battle's bank gives its room to the voice's, whose effects are registered for as long as it plays.
			Show.UnloadSystemSoundBank();
			TResult<std::vector<uint8>> Sounds = EventFiles->ReadVoiceSounds(Set);
			TResult<std::vector<uint8>> Bank = EventFiles->ReadVoiceBank(Set, Routine.Other);
			if (!Sounds || !Bank)
			{
				Fail(static_cast<uint8>(EBattleShowFault::File));
				return EShowFlow::Done;
			}
			VoiceSounds = std::move(*Sounds);
			VoiceSound = static_cast<uint32>(SoundSetOf(VoiceSounds)) << 16 | static_cast<uint32>(EventFiles->VoiceEffect(Set, Routine.Other));
			if (Show.Audio != nullptr)
			{
				Show.Audio->RegisterEffects(VoiceSounds);
				Show.Audio->LoadBattleVoiceBank(*Bank);
				Show.Audio->PlayEffect(VoiceSound);
			}
			Routine.Resume = 1;
			[[fallthrough]];
		}
		case 1:
			if (Show.Audio != nullptr && Show.Audio->IsEffectPlaying(VoiceSound))
			{
				return EShowFlow::Frame;
			}
			if (Show.Audio != nullptr)
			{
				Show.Audio->UnregisterEffects(VoiceSounds);
				Show.Audio->UnloadBattleBank();
			}
			return EShowFlow::Done;
		default:
			return EShowFlow::Done;
		}
	}
}
