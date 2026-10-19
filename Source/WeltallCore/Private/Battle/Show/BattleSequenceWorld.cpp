#include "BattleObjectSequence.h"

#include "Battle/BattleContext.h"
#include "Battle/Show/BattleShowCamera.h"
#include "Battle/Show/BattleStageFiles.h"

#include <string>

namespace Weltall::BattleObjectMachine
{
	namespace
	{
		/** A model camera move's kinds for a point rather than a slot come after the slot's two. */
		constexpr uint8 PointKinds = 2;
		constexpr uint8 TargetPoint = 0xF6;
		constexpr uint8 TargetPreset = 0xF5;
		constexpr uint8 EyePreset = 0xF4;
		constexpr uint8 FirstTargetOf = 0xF9;
		constexpr uint8 NotWatching = 0xFF;
		/** The angles the model camera turns by: yaws and pitch wrap in a turn; the others are scaled by the stage's object scale. */
		constexpr int32 EyePitchChannel = 2;
		constexpr int32 FirstLengthChannel = 3;
		constexpr int32 LastChannel = 6;
		constexpr uint8 Relative = 0x20;
		constexpr uint8 SecondRelative = 0x40;
		constexpr uint8 FromWorld = 0x20;

		void LoadExtension(FBattleObjects& Objects, FSequenceRun& Run, FBattleObject& Object)
		{
			const int16 Group = SignedWord(Objects, Run, 1);
			Run.Next = Run.At + 4;
			if (Object.Extension != nullptr)
			{
				return;
			}
			const std::optional<int32> File = BattleObjectFiles::ExtensionFile(*Objects.Tables, Run.B, Group);
			if (!File)
			{
				Objects.Fail("a sequence loads extension group " + std::to_string(Group) + ", past the program's table");
				return;
			}
			BattleObjectMachine::LoadExtension(Objects, Object, *File);
		}

		void InstallExtension(FBattleObjects& Objects, FSequenceRun& Run, FBattleObject& Object)
		{
			if (Object.bExtensionPending)
			{
				// The port's reads are done at once.
				Object.bExtensionPending = false;
				return;
			}
			if (Object.Extension == nullptr || Object.bExtensionInstalled)
			{
				return;
			}
			BattleObjectMachine::InstallExtension(Objects, Object);
			Object.bExtensionPending = true;
			Run.Stop();
		}

		void FreeExtensionOp(FBattleObjects& Objects, FSequenceRun&, FBattleObject& Object)
		{
			FreeExtension(Objects, Object);
		}

		void AbortReads(FBattleObjects& Objects, FSequenceRun&, FBattleObject&)
		{
			Objects.Services->AbortReads();
		}

		void ReleaseCamera(FBattleObjects& Objects, FSequenceRun&, FBattleObject&)
		{
			Objects.Camera->Release();
			FreeCameraTracks(Objects);
			Objects.ModelCamera.bOn = false;
		}

		void FadeSound(FBattleObjects& Objects, FSequenceRun& Run, FBattleObject& Object)
		{
			const uint16 Operand = Word(Objects, Run, 1);
			Run.Next = Run.At + 4;
			if (const std::optional<uint32> Base = SoundBase(Objects, Object, Run.B))
			{
				Objects.Services->FadeSound((Operand & 0xFF) + *Base, 0, Operand >> 8);
			}
		}

		void ShowNumbers(FBattleObjects& Objects, FSequenceRun&, FBattleObject&)
		{
			Objects.Services->ShowNumbers();
			if (Objects.Context != nullptr)
			{
				Objects.Context->KeepCamera = 0;
			}
		}

		/** 65, 66: the look-at or the eye moved toward an object (its height scaled by the word), the target point or a stage preset. */
		void MoveCameraPoint(FBattleObjects& Objects, FSequenceRun& Run, FBattleObject& Object)
		{
			const bool bEye = static_cast<uint8>(GearMachine::ReadU16(Objects, Run.At)) == 0x66;
			const uint16 First = Word(Objects, Run, 1);
			const uint16 Second = Word(Objects, Run, 2);
			const int16 Height = SignedWord(Objects, Run, 3);
			Run.Next = Run.At + 8;
			const uint8 Who = static_cast<uint8>(First >> 8);
			const uint8 Tag = static_cast<uint8>(First);
			const uint8 Preset = static_cast<uint8>(Second);
			const FGteVector& From = bEye ? Objects.Camera->Eye : Objects.Camera->Target;
			uint16 Mask = 0;
			const uint32 Slot = ResolveWho(Objects, Object, Who, Mask) & 0xFF;
			std::array<int16, 6> Words = {From[0], From[1], From[2], static_cast<int16>(Slot), 0, -1};
			uint8 KindAdd = 0;
			if (Who == TargetPoint || Who == TargetPreset || Who == EyePreset)
			{
				KindAdd = PointKinds;
				FGteVector Point{};
				if (Who == TargetPoint)
				{
					Point = {Object.Target[0], Object.Target[1], Object.Target[2]};
				}
				else
				{
					const std::optional<FBattleCameraPreset> Found = Objects.Stage->CameraPreset(Preset);
					if (!Found)
					{
						Objects.Fail("the model camera reads preset " + std::to_string(Preset) + ", past the stage's record");
						return;
					}
					Point = Who == TargetPreset ? Found->Target : Found->Eye;
				}
				Words[3] = Point[0];
				Words[4] = static_cast<int16>(Height + Point[1]);
				Words[5] = Point[2];
			}
			else if (NamedObject(Objects, Slot) == nullptr)
			{
				Words[4] = static_cast<int16>((Height < 0 ? Height + 0x3F : Height) >> 6);
			}
			else
			{
				const int32 Scaled = Objects.Height(static_cast<int16>(Slot)) * Height;
				Words[4] = static_cast<int16>((Scaled < 0 ? Scaled + 0xFFF : Scaled) >> 12);
				if (Who == FirstTargetOf)
				{
					Words[5] = static_cast<int16>(FirstTarget(Objects));
				}
			}
			StartCameraTrack(Objects, bEye ? FBattleModelCamera::EyeTrack : FBattleModelCamera::TargetTrack, static_cast<uint8>(Run.B + KindAdd), Tag,
				Words, static_cast<int16>(Second >> 8));
		}

		/** 67: one of the model camera's angles and lengths moved from a value to a value, each set or added to what it is. */
		void MoveCameraChannel(FBattleObjects& Objects, FSequenceRun& Run, FBattleObject& Object)
		{
			const uint16 First = Word(Objects, Run, 1);
			const uint16 Second = Word(Objects, Run, 2);
			uint16 Start = Word(Objects, Run, 3);
			uint16 End = Word(Objects, Run, 4);
			Run.Next = Run.At + 10;
			const int32 Channel = First >> 8;
			const uint8 Tag = static_cast<uint8>(First);
			const uint8 How = static_cast<uint8>(Second);
			const int16 Yaw = (Run.B & FromWorld) == 0 ? Object.Bones[0].Angles[1] : int16{0};
			if (Channel > LastChannel)
			{
				Objects.Fail("the model camera moves channel " + std::to_string(Channel) + ", whose value the original reads from an unset register");
				return;
			}
			FBattleModelCamera& Camera = Objects.ModelCamera;
			uint16 Now = static_cast<uint16>(Camera.Channel(Channel));
			if (Channel <= EyePitchChannel)
			{
				Now &= 0xFFF;
			}
			else
			{
				const int32 Scale = Objects.Stage->ObjectScale();
				Start = static_cast<uint16>(static_cast<uint32>(static_cast<int16>(Start) * Scale) >> 12);
				End = static_cast<uint16>(static_cast<int16>(End) * Scale >> 12);
			}
			uint16 From = 0;
			int16 FirstValue = 0;
			if ((How & Relative) == 0)
			{
				if (Channel < EyePitchChannel)
				{
					From = static_cast<uint16>((Start + Yaw) & 0xFFF);
				}
				else if (Channel < FirstLengthChannel)
				{
					From = Start & 0xFFF;
				}
				else
				{
					From = Start;
				}
				FirstValue = static_cast<int16>(From);
			}
			else
			{
				FirstValue = static_cast<int16>(Now);
				From = static_cast<uint16>(Now + Start);
			}
			int16 To = 0;
			if ((How & SecondRelative) == 0)
			{
				if (Channel < FirstLengthChannel)
				{
					uint16 Base = Start;
					if (Channel < EyePitchChannel)
					{
						Base = static_cast<uint16>(Base + Yaw);
					}
					uint16 Delta = static_cast<uint16>((End - Start) & 0xFFF);
					if (Delta > 0x7FF)
					{
						Delta |= 0xF000;
					}
					To = static_cast<int16>(Delta + (Base & 0xFFF));
				}
				else
				{
					To = static_cast<int16>(End);
				}
			}
			else
			{
				To = static_cast<int16>(FirstValue + End);
			}
			StartCameraTrack(Objects, Channel, static_cast<uint8>((Run.B & 0x1F) + PointKinds), Tag, {static_cast<int16>(From), 0, 0, To, 0, 0},
				static_cast<int16>(Second >> 8));
		}

		/** 68: the model camera takes the camera from where it stands; passed over once while the AI keeps it (KeepCamera). */
		void TakeCamera(FBattleObjects& Objects, FSequenceRun&, FBattleObject&)
		{
			if (Objects.Context != nullptr && Objects.Context->KeepCamera != 0)
			{
				Objects.Context->KeepCamera = 0;
				return;
			}
			FBattleCamera& View = *Objects.Camera;
			const int32 Dx = View.Target[0] - View.Eye[0];
			const int32 Dz = View.Target[2] - View.Eye[2];
			FreeCameraTracks(Objects);
			View.TakeForModel();
			FBattleModelCamera& Camera = Objects.ModelCamera;
			Camera.bOn = true;
			Camera.Watched = NotWatching;
			Camera.EyeYaw = static_cast<int16>(GearMachine::Ratan2(Objects, Dx, Dz) & 0xFFF);
			Camera.EyePitch = 0;
			Camera.EyeDistance = 0;
			Camera.EyeHeight = 0;
			Camera.bFollow = false;
			Camera.TargetHeight = 0;
			Camera.TargetDistance = 0;
			Camera.TargetYaw = Camera.EyeYaw;
		}

		/** 69: the camera's tracks of tag B waited for: the tag watched first, then until none of them runs. */
		void WaitCamera(FBattleObjects& Objects, FSequenceRun& Run, FBattleObject&)
		{
			FBattleModelCamera& Camera = Objects.ModelCamera;
			if (Camera.Watched == NotWatching)
			{
				Camera.Watched = Run.B;
				Run.Stop();
				return;
			}
			if ((Camera.Events & 1) != 0)
			{
				Run.Stop();
				return;
			}
			Camera.Watched = NotWatching;
		}

		void FollowCamera(FBattleObjects& Objects, FSequenceRun&, FBattleObject&)
		{
			Objects.ModelCamera.bFollow = true;
		}

		void SetEffect(FBattleObjects& Objects, FSequenceRun& Run, FBattleObject&)
		{
			const int16 Effect = SignedWord(Objects, Run, 1);
			Run.Next = Run.At + 4;
			if (Run.B == 0)
			{
				Objects.RequestedEffect = Effect;
			}
		}

		void WaitEffect(FBattleObjects& Objects, FSequenceRun& Run, FBattleObject&)
		{
			Run.Stop(Objects.Services->EffectLoaded() ? Run.At + 2 : Run.At);
		}

		void RequestEffect(FBattleObjects& Objects, FSequenceRun& Run, FBattleObject&)
		{
			Objects.Services->RequestEffect(Objects.RequestedEffect);
			Run.Stop(Run.At + 2);
		}

		void CountSpecialModel(FBattleObjects& Objects, FSequenceRun& Run, FBattleObject&)
		{
			if (Objects.Services->CountSpecialModel())
			{
				Objects.Free(FBattleObjects::SpecialSlot);
			}
			Run.bEnd = true;
		}
	}

	void AddSequenceWorldOps(std::array<FSequenceOp, SequenceOpCount>& Ops)
	{
		Ops[0x04] = &LoadExtension;
		Ops[0x05] = &InstallExtension;
		Ops[0x06] = &FreeExtensionOp;
		Ops[0x07] = &AbortReads;
		Ops[0x0F] = &ReleaseCamera;
		Ops[0x3C] = &FadeSound;
		Ops[0x3F] = &ShowNumbers;
		Ops[0x65] = &MoveCameraPoint;
		Ops[0x66] = &MoveCameraPoint;
		Ops[0x67] = &MoveCameraChannel;
		Ops[0x68] = &TakeCamera;
		Ops[0x69] = &WaitCamera;
		Ops[0x6A] = &FollowCamera;
		Ops[0x71] = &SetEffect;
		Ops[0x72] = &WaitEffect;
		Ops[0x73] = &RequestEffect;
		Ops[0x74] = &CountSpecialModel;
	}
}
