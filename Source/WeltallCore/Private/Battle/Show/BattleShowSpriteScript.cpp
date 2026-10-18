#include "Battle/BattleContext.h"
#include "Battle/Show/BattleShowCamera.h"
#include "Battle/Show/BattleShowNumbers.h"
#include "Battle/Show/BattleShowSprites.h"
#include "Battle/Show/BattleShowTasks.h"
#include "Battle/Show/BattleTmdModel.h"
#include "Foundation/ProcessorDivide.h"
#include "Graphics/Gte.h"
#include "Graphics/GteMath.h"
#include "Graphics/GteVectors.h"
#include "Graphics/SpriteFile.h"

namespace Weltall
{
	namespace
	{
		/** Animation 63 treats a hold as the end. */
		constexpr int8 HoldEndsAnimation = '?';
		/** Pad 1 as the development hardware's debug code reads it; nothing sets it on a retail console. */
		constexpr uint16 RetailDebugPad = 0;
		constexpr uint16 SelectButton = 0x100;

		int32 Wrap(int64 Value)
		{
			return static_cast<int32>(static_cast<uint32>(static_cast<uint64>(Value)));
		}

		int32 DivideByPower(int64 Value, int32 Bits)
		{
			const int32 Truncated = Wrap(Value);
			return (Truncated < 0 ? static_cast<int32>(static_cast<int64>(Truncated) + (int64{1} << Bits) - 1) : Truncated) >> Bits;
		}

		/** An instruction's length in the kernel's table: SetFrame is in it at two bytes, though it has three. */
		uint32 TableLength(uint8 Opcode)
		{
			return Opcode < 0xA0 ? 1 : Opcode < 0xC8 ? 2 : Opcode < 0xF1 ? 3 : 4;
		}

		void SetState(FSprite& Sprite, uint32 State)
		{
			Sprite.AnimationFlags = (Sprite.AnimationFlags & ~SpriteAnimationFlags::StateMask) | State << SpriteAnimationFlags::StateShift;
		}

		void CallListener(FSprite& Sprite)
		{
			if (Sprite.Listener != nullptr)
			{
				Sprite.Listener->OnAnimationEvent(Sprite);
			}
		}

		/** One more step run, stuck at 63. */
		void CountStep(FSprite& Sprite)
		{
			uint32 Steps = (Sprite.StepsRun() + 1) & 0x3F;
			if (Steps == 0)
			{
				Steps = 0x3F;
			}
			Sprite.AnimationFlags = (Sprite.AnimationFlags & ~SpriteAnimationFlags::StepsMask) | Steps << SpriteAnimationFlags::StepsShift;
		}

		int32 SlotBit(const FSprite& Sprite)
		{
			return 1 << FBattleSprites::SlotOf(Sprite);
		}

		/**
		 * The slot's bit the camera's framing commands take through a sprite pointer. Through a cleared one (the actor
		 * before the battle's first playback, in a battle event's own attacks) the console reads its slot from the
		 * BIOS's call vector at the bottom of memory, a jump and an empty instruction whose bits spell slot 0.
		 */
		int32 FramedSlotBit(const FSprite* Sprite)
		{
			return Sprite != nullptr ? SlotBit(*Sprite) : 1;
		}

		/** A script's velocity from a byte: 16ths of the sprite's weight, whole in the scale the kind needs. */
		int32 ScriptSpeed(const FSprite& Sprite, int32 Value)
		{
			return DivideByPower(int64{Value} * 0x10 * Sprite.Weight, 12);
		}
	}

	void FBattleSprites::RunStep(FSprite& Sprite, uint32 StaleRegister)
	{
		FSpriteSystem& System = *Sprites;
		uint32& Register = StepRegister;
		Register = StaleRegister;
		for (;;)
		{
			if (Sprite.Wait != 0 || System.Fault() != ESpriteFault::None)
			{
				return;
			}
			const FSpriteFile& File = *Sprite.ScriptFile;
			const uint32 At = Sprite.Pc;
			if (At >= File.Size())
			{
				System.Report(ESpriteFault::PcOutsideFile, At);
				return;
			}
			const uint8 Opcode = File.Byte(At);
			const int32 TimeScale = static_cast<int32>(Sprite.TimeScale());
			const auto Operand = [&File, At](uint32 Index) { return File.Byte(At + Index); };
			const auto Relative = [&File, At]() { return At + static_cast<uint32>(static_cast<int16>(File.Word(At + 1))); };
			if (Opcode < 0x80)
			{
				Sprite.Pc = At + 1;
				if (Opcode >= 0x40)
				{
					// Waits by a register the interpreter never sets: no script Battle runs reaches one.
					System.Report(ESpriteFault::UnsupportedInstruction, Opcode);
					return;
				}
				if (Opcode < 0x10)
				{
					System.SetFrame(Sprite, Sprite.Frame + 1);
				}
				else if (Opcode < 0x20)
				{
					Sprite.AnimationFlags = (Sprite.AnimationFlags & ~SpriteAnimationFlags::ListStepMask) | ((Sprite.ListStep() + 1) & 0x3F) << SpriteAnimationFlags::ListStepShift;
					System.ApplyDirectionFrame(Sprite);
				}
				else if (Opcode < 0x30)
				{
					System.SetFrame(Sprite, Sprite.Frame - 1);
				}
				const int32 Wait = DivideByPower(int64{(Opcode & 0xF) + 1} * TimeScale, 8);
				Sprite.Wait = static_cast<uint16>(Sprite.Wait + (Wait == 0 ? 1 : Wait));
				CountStep(Sprite);
				return;
			}
			switch (Opcode)
			{
			case 0x81:
				if (Sprite.Animation() != HoldEndsAnimation)
				{
					Sprite.Wait = 0;
					CallListener(Sprite);
					SetState(Sprite, SpriteAnimationFlags::StateHeld);
					return;
				}
				[[fallthrough]];
			case 0x80:
				SetState(Sprite, 0);
				if (Sprite.Listener != nullptr)
				{
					Sprite.Listener->OnAnimationEvent(Sprite);
					return;
				}
				if (Sprite.NextAnimation() >= 0)
				{
					System.SetAnimation(Sprite, Sprite.NextAnimation());
				}
				SetState(Sprite, 0);
				return;
			case 0x82:
			{
				CallListener(Sprite);
				const int32 VerticalSpeed = Sprite.Velocity[1];
				System.SetAnimation(Sprite, Sprite.Animation());
				Sprite.Velocity[1] = VerticalSpeed;
				Sprite.Wait = 0;
				RunStep(Sprite, Register);
				return;
			}
			case 0x85:
			{
				uint32 Address = 0;
				if (!System.PopAddress(Sprite, Address))
				{
					return;
				}
				Sprite.Pc = Address;
				continue;
			}
			case 0x86:
				if (Sprite.Velocity[1] < 0)
				{
					Sprite.Wait = 1;
					return;
				}
				break;
			case 0x87:
				if (static_cast<int16>(Sprite.Position[1] >> 16) < Sprite.FloorY)
				{
					Sprite.Wait = 1;
					return;
				}
				break;
			case 0x88:
				ThrowArc(Sprite);
				break;
			case 0x89:
				JumpArc(Sprite);
				break;
			case 0x8B:
				ShowNumbers();
				break;
			case 0x8E:
				Sprite.Pc = FSprite::Stopped;
				return;
			case 0x8F:
				Play.bCastEnded = true;
				Sprite.Pc = FSprite::Stopped;
				SetState(Sprite, 0);
				return;
			case 0x95:
				Sprite.Wait = 1;
				if (Services->FilesPending())
				{
					return;
				}
				break;
			case 0x97:
			{
				if (static_cast<int16>(Sprite.Position[1] >> 16) < Sprite.FloorY)
				{
					Sprite.Wait = 1;
					return;
				}
				FSprite* Target = Sprite.Target;
				if (Target == nullptr)
				{
					ReportFault(Opcode);
					return;
				}
				Register = 1;
				Sprite.Speed = 0;
				System.UpdateVelocity(Sprite);
				System.SetDirection(Sprite, static_cast<int16>(HeadingBetween(*Target, Sprite)));
				System.SetFacing(Sprite, static_cast<int16>(HeadingBetween(*Target, Sprite)));
				System.SetDirection(*Target, static_cast<int16>(HeadingBetween(Sprite, *Target)));
				System.SetFacing(*Target, static_cast<int16>(HeadingBetween(Sprite, *Target)));
				break;
			}
			case 0x98:
			{
				const FSprite* Parent = Sprite.Parent;
				if (Parent == nullptr)
				{
					break;
				}
				Sprite.Wait = 1;
				if (Parent->Animation() == Sprite.ParentAnimation && Parent->State() == SpriteAnimationFlags::StateRunning)
				{
					return;
				}
				break;
			}
			case 0x99:
				if (!Sprite.bHasTransform)
				{
					ReportFault(Opcode);
					return;
				}
				Sprite.Rotation = {Camera->ViewPitch, Camera->ViewYaw, 0};
				break;
			case 0x9A:
			case 0x9C:
			{
				if (!Sprite.bHasTransform)
				{
					ReportFault(Opcode);
					return;
				}
				Register = 1;
				FGteVector Angles{};
				int16 Reach = 0;
				if (Opcode == 0x9A)
				{
					Angles = Sprite.Rotation;
					Reach = static_cast<int16>(Camera->Distance);
				}
				else
				{
					Angles = {Camera->ViewPitch, Camera->ViewYaw, 0};
					Reach = static_cast<int16>(ProcessorDivide(static_cast<int16>(Camera->SpriteDistance) * 0x1000, Sprite.Scale));
				}
				const FGteLongVector Along = ApplyMatrix(RotationXyz(Angles), {Reach, 0, 0});
				// The eye's controller stands back from the look-at's place, the look-at's out from the eye's.
				if (Sprite.Kind() == 10)
				{
					for (std::size_t Axis = 0; Axis < 3; ++Axis)
					{
						Sprite.Position[Axis] = Wrap(int64{Camera->TargetPoint[Axis]} + int64{Along[Axis]} * -0x10000);
					}
				}
				if (Sprite.Kind() == 11)
				{
					for (std::size_t Axis = 0; Axis < 3; ++Axis)
					{
						Sprite.Position[Axis] = Wrap(int64{Camera->EyePoint[Axis]} + int64{Along[Axis]} * 0x10000);
					}
				}
				break;
			}
			case 0x9B:
				Sprite.Scale = static_cast<int16>(ProcessorDivide(static_cast<int16>(Camera->SpriteDistance) * 0x1000, Camera->Distance));
				break;
			case 0x9D:
			{
				FGteVector& Point = Sprite.Kind() == 10 ? Camera->Eye : Camera->Target;
				Point = {static_cast<int16>(Sprite.Position[0] >> 16), static_cast<int16>(Sprite.Position[1] >> 16), static_cast<int16>(Sprite.Position[2] >> 16)};
				break;
			}
			case 0x9E:
				// Waits for Select on pad 1 as the development hardware reads it: on a retail console, for ever.
				Sprite.Wait = 1;
				if constexpr ((RetailDebugPad & SelectButton) == 0)
				{
					return;
				}
				break;
			case 0x9F:
				break;
			case 0xA4:
			{
				FSprite* Target = Sprite.Target;
				if (Target == nullptr)
				{
					ReportFault(Opcode);
					return;
				}
				const int32 Animation = static_cast<int8>(Operand(1));
				if (Target->PrimaryFile == nullptr)
				{
					// A 3D combatant's proxy: its object reacts instead.
					Services->ReactModel(SlotOf(*Target), static_cast<uint16>(SlotBit(Sprite)), Animation);
				}
				else
				{
					System.SetAnimation(*Target, Animation);
				}
				break;
			}
			case 0xA7:
			{
				Sprite.Pc = At + TableLength(Opcode);
				const uint8 Value = Operand(1);
				if ((Value & 0x80) != 0)
				{
					System.HitStop = (Value & 0x7F) + 1;
					Sprite.Wait = static_cast<uint16>(Sprite.Wait + 1);
					return;
				}
				const int32 Scaled = (Value + 2) * TimeScale;
				const int32 Wait = (Scaled >> 8) == 0 ? 1 : static_cast<int16>(static_cast<uint32>(Scaled) >> 8);
				Sprite.Wait = static_cast<uint16>(Sprite.Wait + Wait);
				return;
			}
			case 0xBE:
			{
				const int32 Word = static_cast<int16>(File.Word(At + 1));
				const int32 Scaled = Wrap(int64{((Word >> 11) & 0xF) + 1} * TimeScale);
				const int32 Wait = (Scaled >> 8) == 0 ? 1 : static_cast<int16>(static_cast<uint32>(Scaled) >> 8);
				int32 Frame = Word & 0x1FF;
				if (Sprite.Layout() == SpriteDrawFlags::LayoutParts)
				{
					if (Word < 0 && Frame != 0)
					{
						// Through the byte table, which a spawned sprite does not have.
						if (Sprite.ByteTableFile == nullptr)
						{
							ReportFault(Opcode);
							return;
						}
						Frame = Sprite.ByteTableFile->Byte(Sprite.ByteTable + static_cast<uint32>(Frame) - 1);
					}
					const bool bFlipped = (Word & 0x200) != 0;
					Sprite.TimeFlags = bFlipped ? Sprite.TimeFlags | SpriteTimeFlags::ListEntryFlipped : Sprite.TimeFlags & ~SpriteTimeFlags::ListEntryFlipped;
					const bool bMirrored = bFlipped != Sprite.IsMirroredFacing();
					Sprite.DrawFlags = (Sprite.DrawFlags & ~(SpriteDrawFlags::Mirrored | SpriteDrawFlags::FlippedVertically))
						| (bMirrored ? SpriteDrawFlags::Mirrored : 0) | ((Word & 0x400) != 0 ? SpriteDrawFlags::FlippedVertically : 0);
					System.SetFrame(Sprite, Frame);
				}
				else
				{
					Sprite.Frame = static_cast<uint16>(Frame);
				}
				Sprite.Wait = static_cast<uint16>(Sprite.Wait + Wait);
				Sprite.Pc += 3;
				return;
			}
			case 0xC2:
			{
				FSprite* Target = Sprite.Target;
				if (Target == nullptr)
				{
					ReportFault(Opcode);
					return;
				}
				Sprite.Position[1] = Wrap(int64{Sprite.FloorY - 1} * 0x10000);
				int16 Ahead = static_cast<int16>(DivideByPower(int64{static_cast<int8>(Operand(1))} * Sprite.Scale, 12));
				if (!Sprite.IsMirroredFacing())
				{
					Ahead = static_cast<int16>(-Ahead);
				}
				Sprite.SavedPoint[0] = static_cast<int16>(static_cast<int16>(Target->Position[0] >> 16) + Ahead);
				Sprite.SavedPoint[1] = 0;
				Sprite.SavedPoint[2] = static_cast<int16>(Target->Position[2] >> 16);
				JumpArc(Sprite);
				break;
			}
			case 0xC3:
			case 0xEC:
			case 0xF9:
			{
				FSpriteOperands Args;
				Args.At = At + 2;
				for (uint32 Byte = 0; Byte < 3; ++Byte)
				{
					Args.Bytes[Byte] = File.Byte(At + 2 + Byte);
				}
				SystemCommand(Sprite, Operand(1), Args);
				break;
			}
			case 0xC8:
			{
				const std::optional<FSpriteOperands> Args = System.VariableOperands(Sprite, Operand(2));
				if (!Args)
				{
					return;
				}
				System.ExecuteKernelInstruction(Sprite, Operand(1), *Args);
				break;
			}
			case 0xCA:
			{
				Register = 1;
				const uint32 Data = Relative() + 1;
				const uint8 Mode = File.Byte(Data + 4);
				const int32 BlendMode = Mode != 0 ? Mode - 1 : Fade->Fade.ModeOrAdd();
				Fade->Start(File.Byte(Data + 3) >> 1, BlendMode, File.Byte(Data), File.Byte(Data + 1), File.Byte(Data + 2));
				break;
			}
			case 0xCB:
			{
				Register = 1;
				const uint32 Data = Relative() + 1;
				Shake->Start({static_cast<int16>(File.Byte(Data)), static_cast<int16>(File.Byte(Data + 1)), static_cast<int16>(File.Byte(Data + 2))}, File.Byte(Data + 3));
				break;
			}
			case 0xD4:
				Sprite.Pc = Relative();
				CallListener(Sprite);
				continue;
			case 0xE1:
				Sprite.Pc = Relative();
				continue;
			case 0xE2:
				if (!System.PushAddress(Sprite, At + 3))
				{
					return;
				}
				Sprite.Pc = Relative();
				continue;
			case 0xE3:
			{
				FSprite* Target = Sprite.Target;
				if (Target == nullptr)
				{
					ReportFault(Opcode);
					return;
				}
				Target->TimeFlags = (Target->TimeFlags & 0x00FFFFFF) | static_cast<uint32>(static_cast<uint8>(HoldEndsAnimation)) << SpriteTimeFlags::AnimationShift;
				System.Start(*Target, File, Relative());
				break;
			}
			case 0xE4:
			{
				uint8 Count = 0;
				if (!System.PopByte(Sprite, Count))
				{
					return;
				}
				if (Count != 0)
				{
					if (!System.PushByte(Sprite, static_cast<uint8>(Count - 1)))
					{
						return;
					}
					Sprite.Pc = Relative();
					continue;
				}
				break;
			}
			case 0xE8:
			{
				const std::optional<FSpriteOperands> Args = System.VariableOperands(Sprite, Operand(2));
				if (!Args)
				{
					return;
				}
				SystemCommand(Sprite, Operand(1), *Args);
				break;
			}
			case 0xF3:
				AttachModel(Sprite, At, Operand(1) | Operand(2) << 8 | static_cast<int8>(Operand(3)) * 0x10000);
				break;
			case 0xF8:
			{
				const uint8 Test = Operand(3);
				const FSprite* Target = Sprite.Target;
				// The target's result in the record before the one to play. Only the first six tests look at it: before the
				// first record it is a byte of the slot records, which the port does not keep as bytes.
				uint8 Result = 0;
				if ((Test & 0x7F) < 7)
				{
					if (Play.Record < 1)
					{
						ReportFault(Opcode);
						return;
					}
					// A target nothing set names slot 0, as the camera's framing commands find.
					const int32 Slot = Target != nullptr ? SlotOf(*Target) : 0;
					Result = Context->Performances[static_cast<std::size_t>(Play.Record - 1)].Results[static_cast<std::size_t>(Slot)];
				}
				bool bTaken = false;
				switch (Test & 0x7F)
				{
				case 0:
				case 1:
				case 2:
				case 3:
				case 5:
				case 6:
					bTaken = Result == (Test & 0x7F);
					break;
				case 4:
					// A miss, or a counterattack's.
					bTaken = Result == 4 || Result == 7;
					break;
				case 7:
					bTaken = TargetIndex(Target) + 1 < Play.TargetCount;
					NextTarget(Sprite);
					break;
				case 8:
					// The rules' stored-combo flag.
					bTaken = Context->Party.bStoredCombo;
					break;
				case 9:
					bTaken = Target == Play.Actor;
					break;
				default:
					break;
				}
				if ((Test & 0x80) != 0)
				{
					bTaken = !bTaken;
				}
				Register = bTaken ? 1 : 0;
				if (bTaken)
				{
					Sprite.Pc = Relative();
					continue;
				}
				break;
			}
			case 0xFA:
			{
				const std::optional<FSpriteOperands> Args = System.VariableOperands(Sprite, Operand(1));
				if (!Args)
				{
					return;
				}
				if (Args->Bytes[0] != 0)
				{
					Sprite.Pc = At + static_cast<uint32>(static_cast<int16>(File.Word(At + 2)));
					continue;
				}
				break;
			}
			case 0xFB:
				StartWatch(Sprite, Operand(3) << 1, Relative());
				break;
			default:
			{
				FSpriteOperands Args;
				Args.At = At + 1;
				for (uint32 Byte = 0; Byte < 3; ++Byte)
				{
					Args.Bytes[Byte] = File.Byte(At + 1 + Byte);
				}
				System.ExecuteKernelInstruction(Sprite, Opcode, Args);
				break;
			}
			}
			Sprite.Pc += TableLength(Opcode);
		}
	}

	void FBattleSprites::ShowNumbers()
	{
		if (Play.Record < 1)
		{
			ReportFault(0x8B);
			return;
		}
		std::array<const FSprite*, SlotCount> Over{};
		for (std::size_t Slot = 0; Slot < Over.size(); ++Slot)
		{
			Over[Slot] = SlotSprites[Slot];
		}
		Numbers->ShowAll(Context->Performances[static_cast<std::size_t>(Play.Record - 1)], Over, Play.Actor);
	}

	void FBattleSprites::AttachModel(FSprite& Sprite, uint32 At, int32 Offset)
	{
		if (!Sprite.bHasTransform)
		{
			ReportFault(0xF3);
			return;
		}
		if (Offset == 0)
		{
			// Detached: the transform's model and packets are none; its parts' place keeps the packets it had.
			Sprite.ModelFile = nullptr;
			Sprite.Model = 0;
			Sprite.bHasAnchors = false;
			return;
		}
		// The model's first object, its packets built with the sprite's blend and raw flag; with a size
		// factor, the object's vertices are scaled in the file first, once.
		const FSpriteFile& File = *Sprite.ScriptFile;
		const uint32 Object = At + 1 + static_cast<uint32>(Offset) + 0xC;
		if (Sprite.SizeFactor != 0 && !ScaleModelVertices(File, Object))
		{
			return;
		}
		Sprite.ModelFile = &File;
		Sprite.Model = Object;
		Sprite.ModelPackets.Blend = static_cast<uint8>(Sprite.DrawFlags >> 5 & 7);
		if (Sprite.ModelPackets.Blend != 0)
		{
			// The builder writes the sprite's blend mode into the model's pages in the file, for every later build of it too.
			FSpriteFile* Writable = WritableFile(File);
			if (Writable == nullptr || !SetBattleTmdBlend(*Writable, Object, Sprite.ModelPackets.Blend))
			{
				ReportFault(0xF3);
				return;
			}
		}
		Sprite.ModelPackets.bRawTexture = (Sprite.Code & 1) != 0;
		Sprite.ModelPackets.Tint = {};
		Sprite.ModelPackets.Build = ++ModelBuilds;
		// The model takes the anchor slots' place, and its packets the part records'.
		Sprite.bHasAnchors = false;
		if (Sprite.Layout() == SpriteDrawFlags::LayoutParts)
		{
			Sprite.PartCapacity = 0;
		}
	}

	bool FBattleSprites::ScaleModelVertices(const FSpriteFile& File, uint32 Object)
	{
		// The model's vertices scaled by a shift of 3: each vertex times 8 in the file, unless the object's flags say it was.
		const auto ReadU32 = [&File](uint32 Offset) { return File.Word(Offset) | static_cast<uint32>(File.Word(Offset + 2)) << 16; };
		const uint32 Flags = ReadU32(Object + 0x18);
		if ((Flags & 0x8000) != 0)
		{
			return true;
		}
		FSpriteFile* Writable = WritableFile(File);
		if (Writable == nullptr)
		{
			ReportFault(0xF3);
			return false;
		}
		Writable->SetByte(Object + 0x19, static_cast<uint8>((Flags | 0x8000) >> 8));
		const uint32 Vertices = Object + ReadU32(Object);
		const uint32 Count = ReadU32(Object + 4);
		for (uint32 Vertex = 0; Vertex < Count; ++Vertex)
		{
			for (uint32 Axis = 0; Axis < 3; ++Axis)
			{
				const uint32 Offset = Vertices + Vertex * 8 + Axis * 2;
				const uint16 Scaled = static_cast<uint16>(File.Word(Offset) << 3);
				Writable->SetByte(Offset, static_cast<uint8>(Scaled));
				Writable->SetByte(Offset + 1, static_cast<uint8>(Scaled >> 8));
			}
		}
		return true;
	}

	void FBattleSprites::SystemCommand(FSprite& Sprite, uint8 Command, const FSpriteOperands& Operand)
	{
		FSpriteSystem& System = *Sprites;
		// The data an F9 command's operand points to, counted from the operand.
		const auto Inline = [this, &Operand, Command](uint32& Data) {
			if (!Operand.bInScript)
			{
				ReportFault(0xC3u << 8 | Command);
				return false;
			}
			Data = Operand.At + static_cast<uint32>(Operand.S16());
			return true;
		};
		const auto SetDrawFlag = [&Sprite](uint32 Flag) { Sprite.DrawFlags |= Flag; };
		switch (Command)
		{
		case 0x01:
			if (!Sprite.bHasAnchors && Sprite.ModelFile == nullptr && Sprite.bHasTransform)
			{
				Sprite.bHasAnchors = true;
				Sprite.Anchors = {};
			}
			StartTrail(Sprite, Operand);
			return;
		case 0x02:
			EndTrail(Sprite);
			return;
		case 0x03:
			StartGlue(Sprite, Operand.U8(0));
			return;
		case 0x04:
			EndGlues();
			return;
		case 0x05:
		case 0x1E:
			Sprite.Frame = 1;
			if (Sprite.Task != nullptr && System.ReachesTask(Sprite, Command))
			{
				Sprite.Task->DrawRole = Command == 0x05 ? BattleSpriteDraws::Streak : BattleSpriteDraws::LineToParent;
			}
			Sprite.Code = 0x40;
			return;
		case 0x06:
			FrameSlots(static_cast<uint32>(FramedSlotBit(Play.Actor)));
			return;
		case 0x07:
			FrameSlots(static_cast<uint32>(FramedSlotBit(Sprite.Target)));
			return;
		case 0x08:
			Sprite.BuildFlags |= SpriteBuildFlags::KeepQueuedAnchors;
			if (Sprite.bHasTransform && Sprite.bHasAnchors)
			{
				Sprite.Anchors = {};
				Sprite.PivotX = 0;
				Sprite.PivotY = 0;
			}
			else if (Sprite.ModelFile != nullptr)
			{
				System.Report(ESpriteFault::AnchorsHoldAModel, 0);
			}
			return;
		case 0x09:
			Sprite.Heading = static_cast<int16>(Sprite.Heading + 0x800);
			for (int32& Axis : Sprite.Velocity)
			{
				Axis = Wrap(-int64{Axis});
			}
			return;
		case 0x0A:
			if (!Sprite.bHasTransform)
			{
				ReportFault(0xC300u | Command);
				return;
			}
			Sprite.Rotation[2] = static_cast<int16>(GteRatan2(Sprite.Velocity[1] >> 8, Sprite.Velocity[0] >> 8, *Arctangents));
			Sprite.DrawFlags |= SpriteDrawFlags::MatrixDirty;
			return;
		case 0x0B:
			if (!Sprite.bHasTransform)
			{
				ReportFault(0xC300u | Command);
				return;
			}
			Sprite.Rotation[0] = static_cast<int16>(GteRatan2(Sprite.Velocity[2] >> 8, Sprite.Velocity[0] >> 8, *Arctangents));
			Sprite.DrawFlags |= SpriteDrawFlags::MatrixDirty;
			return;
		case 0x0C:
		{
			if (!Sprite.bHasTransform)
			{
				ReportFault(0xC300u | Command);
				return;
			}
			std::array<int32, 3> Along{Sprite.Velocity[0] >> 8, Sprite.Velocity[1] >> 8, Sprite.Velocity[2] >> 8};
			if (Along[2] == 0)
			{
				Along[2] = 4;
			}
			const int32 Flat = SquareRoot0(GteSquare(Along[0]) + GteSquare(Along[2]));
			Sprite.Rotation[1] = static_cast<int16>(-GteRatan2(Along[2], Along[0], *Arctangents));
			Sprite.Rotation[2] = static_cast<int16>(GteRatan2(Along[1], Flat, *Arctangents));
			Sprite.Rotation[0] = 0;
			Sprite.DrawFlags |= SpriteDrawFlags::MatrixDirty;
			return;
		}
		case 0x0D:
			Sprite.Velocity = ApplyMatrixLV(RotationXyz({0, 0, static_cast<int16>(Operand.S8(0) << 4)}), Sprite.Velocity);
			return;
		case 0x0E:
			System.SetDirection(Sprite, Operand.S8(0) << 4);
			return;
		case 0x0F:
			Sprite.TimeFlags &= ~(SpriteTimeFlags::FacingMirrored | SpriteTimeFlags::ListEntryFlipped);
			Sprite.DrawFlags &= ~(SpriteDrawFlags::Mirrored | SpriteDrawFlags::FlippedVertically);
			System.SetDirection(Sprite, 0);
			return;
		case 0x10:
			for (std::size_t Axis = 0; Axis < 3; ++Axis)
			{
				Sprite.Position[Axis] = Wrap(int64{Camera->Target[Axis]} * 0x10000);
			}
			return;
		case 0x11:
			bCombatantsHidden = true;
			return;
		case 0x12:
			bStageHidden = true;
			SavedClear = Clear;
			Clear = {1, 0, 0, 0};
			return;
		case 0x13:
			bCombatantsHidden = false;
			return;
		case 0x14:
			bStageHidden = false;
			Clear = SavedClear;
			return;
		case 0x15:
			SetDrawFlag(0x1000000);
			return;
		case 0x16:
			SetDrawFlag(0x2000000);
			return;
		case 0x17:
		{
			const int32 Factor = Operand.S8(0) * 4;
			for (int32& Axis : Sprite.Velocity)
			{
				Axis = DivideByPower(int64{Axis} * Factor, 8);
			}
			return;
		}
		case 0x18:
			Services->MoveImage({0, 0, 0x140, 0xE0}, 0x2C0, 0x100);
			StartShatter();
			return;
		case 0x19:
		{
			// The wave's values from its inline data.
			uint32 Data = 0;
			if (!Inline(Data))
			{
				return;
			}
			const FSpriteFile& File = *Sprite.ScriptFile;
			const std::array<int32, 6> Wave{File.Byte(Data) << 4, File.Byte(Data + 1), static_cast<int8>(File.Byte(Data + 2)) << 3, File.Byte(Data + 3) << 3,
				static_cast<int8>(File.Byte(Data + 4)) << 3, File.Byte(Data + 5)};
			Services->StartExtension(Command, Sprite, Operand, Wave);
			return;
		}
		case 0x24:
		case 0x25:
		case 0x54:
			Services->StartExtension(Command, Sprite, Operand, {});
			return;
		case 0x1A:
			if (System.ReachesTask(Sprite, Command))
			{
				System.KillChildren(Sprite);
			}
			return;
		case 0x1B:
			if (Sprite.Parent == nullptr)
			{
				ReportFault(0xC300u | Command);
				return;
			}
			Sprite.Velocity = Sprite.Parent->Velocity;
			return;
		case 0x1C:
		{
			const int32 AlongX = Sprite.SavedPoint[0] - static_cast<int16>(Sprite.Position[0] >> 16);
			const int32 AlongY = Sprite.SavedPoint[1] - static_cast<int16>(Sprite.Position[1] >> 16);
			const int32 AlongZ = Sprite.SavedPoint[2] - static_cast<int16>(Sprite.Position[2] >> 16);
			const int32 Flat = SquareRoot0(GteSquare(AlongX) + GteSquare(AlongZ));
			const int32 Yaw = GteRatan2(AlongZ, AlongX, *Arctangents);
			const FGteVector Angles{0, static_cast<int16>(-Yaw), static_cast<int16>(GteRatan2(AlongY, Flat, *Arctangents))};
			Sprite.Heading = Angles[1];
			const FGteLongVector Aimed = ApplyMatrix(RotationXyz(Angles), {static_cast<int16>(Wrap(int64{Sprite.Speed} << 9) >> 16), 0, 0});
			for (std::size_t Axis = 0; Axis < 3; ++Axis)
			{
				Sprite.Velocity[Axis] = Wrap(int64{Aimed[Axis]} << 7);
			}
			return;
		}
		case 0x1D:
		{
			uint32 Data = 0;
			if (!Inline(Data))
			{
				return;
			}
			const FSpriteFile& File = *Sprite.ScriptFile;
			const uint8 Low = File.Byte(Data);
			const int32 High = static_cast<int8>(File.Byte(Data + 1));
			const auto ScaleBySize = [&Sprite](int32 Value) { return Sprite.SizeFactor != 0 ? DivideByPower(int64{Value} * Sprite.SizeFactor, 10) : Value; };
			std::array<int32, 6> Measures{};
			if (Sprite.SizeFactor == 0)
			{
				Measures[0] = Wrap(int64{High << 3 | Low} << 5);
				Measures[1] = File.Byte(Data + 2) << 13;
				Measures[2] = File.Byte(Data + 3) << 13;
			}
			else
			{
				Measures[0] = Wrap(int64{ScaleBySize(High << 3 | Low)} << 8);
				Measures[1] = Wrap(int64{ScaleBySize(File.Byte(Data + 2))} << 16);
				Measures[2] = Wrap(int64{ScaleBySize(File.Byte(Data + 3))} << 16);
			}
			Measures[3] = File.Byte(Data + 4) << 4;
			Measures[4] = File.Byte(Data + 5) << 2;
			Services->StartExtension(Command, Sprite, Operand, Measures);
			// The shards take the sprite's model and its packets; the sprite keeps none.
			Sprite.ModelFile = nullptr;
			Sprite.Model = 0;
			Sprite.bHasAnchors = false;
			Sprite.PartCapacity = 0;
			return;
		}
		case 0x1F:
			Sprite.DrawFlags &= ~SpriteDrawFlags::SimplePhysics;
			SnapToFloor(Sprite);
			return;
		case 0x20:
			StartOrbit(Sprite);
			return;
		case 0x21:
			Sprite.Velocity[2] = ScriptSpeed(Sprite, Operand.S8(0)) << 12;
			return;
		case 0x22:
			Sprite.Velocity[2] = Wrap(int64{Sprite.Velocity[2]} + int64{ScriptSpeed(Sprite, Operand.S8(0))} * 0x100);
			return;
		case 0x23:
		{
			const int32 Across = Sprite.SavedPoint[2] - static_cast<int16>(Sprite.Position[2] >> 16);
			const int32 Along = Sprite.SavedPoint[0] - static_cast<int16>(Sprite.Position[0] >> 16);
			const int32 Heading = -GteRatan2(Across, Along, *Arctangents) & 0xFFF;
			System.SetDirection(Sprite, static_cast<int16>(Heading));
			System.SetFacing(Sprite, static_cast<int16>(Heading));
			return;
		}
		case 0x26:
		{
			FSprite* Other = Operand.U8(1) < SlotCount ? SlotSprites[Operand.U8(1)] : nullptr;
			if (Other == nullptr)
			{
				ReportFault(0xC300u | Command);
				return;
			}
			System.SetAnimation(*Other, Operand.U8(0));
			return;
		}
		case 0x27:
			for (int32 Index = 0; Index < Sprite.PartCount(); ++Index)
			{
				uint16& Clut = Sprite.Parts[static_cast<std::size_t>(Index)].Clut;
				Clut = static_cast<uint16>(((Clut & 0x3F) + Operand.U8(0)) | ((Clut >> 6 & 0x1FF) + Operand.U8(1)) << 6);
			}
			return;
		case 0x28:
			Sprite.Velocity[0] = ScriptSpeed(Sprite, Operand.S8(0)) << 8;
			return;
		case 0x29:
			Sprite.Velocity[0] = Wrap(int64{Sprite.Velocity[0]} + int64{ScriptSpeed(Sprite, Operand.S8(0))} * 0x100);
			return;
		case 0x2A:
			Sprite.Velocity[1] = ScriptSpeed(Sprite, Operand.S8(0)) << 12;
			return;
		case 0x2B:
			Sprite.Velocity[1] = Wrap(int64{Sprite.Velocity[1]} + int64{ScriptSpeed(Sprite, Operand.S8(0))} * 0x100);
			return;
		case 0x2C:
			if (Sprite.Task != nullptr && System.ReachesTask(Sprite, Command))
			{
				Sprite.Task->DrawRole = BattleSpriteDraws::Kernel;
			}
			return;
		case 0x2D:
		{
			uint32 Data = 0;
			if (!Inline(Data))
			{
				return;
			}
			const FSpriteFile& File = *Sprite.ScriptFile;
			for (uint32 Axis = 0; Axis < 3; ++Axis)
			{
				Sprite.Position[Axis] = Wrap(int64{static_cast<int16>(File.Word(Data + Axis * 2))} * 0x10000);
			}
			return;
		}
		case 0x2E:
		case 0x5B:
		case 0x5C:
		case 0x5D:
		case 0x5E:
		{
			uint32 Data = 0;
			if (!Inline(Data))
			{
				return;
			}
			const FSpriteFile& File = *Sprite.ScriptFile;
			const std::array<int16, 3> Vector{static_cast<int16>(File.Word(Data)), static_cast<int16>(File.Word(Data + 2)), static_cast<int16>(File.Word(Data + 4))};
			if (Command == 0x2E)
			{
				Sprite.SavedPoint = Vector;
				return;
			}
			// A model sprite's light (its transform's 44h and 4Ch); on sprites of other layouts the words are other memory.
			if (Sprite.Layout() != SpriteDrawFlags::LayoutTransform)
			{
				ReportFault(0xC300u | Command);
				return;
			}
			std::array<int16, 3>& Words = Command == 0x5B || Command == 0x5D ? Sprite.LightAngles : Sprite.LightColor;
			for (std::size_t Axis = 0; Axis < 3; ++Axis)
			{
				Words[Axis] = Command <= 0x5C ? Vector[Axis] : static_cast<int16>(Words[Axis] + Vector[Axis]);
			}
			return;
		}
		case 0x2F:
			Play.Caster = &Sprite;
			return;
		case 0x30:
			if (!Sprite.bHasTransform || Sprite.Layout() != SpriteDrawFlags::LayoutParts || !Sprite.bHasAnchors)
			{
				return;
			}
			if (Operand.U8(0) >= FSprite::AnchorCount)
			{
				ReportFault(0xC300u | Command);
				return;
			}
			Sprite.PivotY = static_cast<int8>(Sprite.Anchors[Operand.U8(0)].OffsetY);
			Sprite.PivotX = static_cast<int8>(Sprite.Anchors[Operand.U8(0)].OffsetX);
			return;
		case 0x31:
			// SetGeomOffset: the screen's centre raised for an effect, and put back.
			Gte->OffsetX = 0xA0 << 16;
			Gte->OffsetY = 0x70 << 16;
			return;
		case 0x32:
			Gte->OffsetX = 0xA0 << 16;
			Gte->OffsetY = 0xA4 << 16;
			return;
		case 0x33:
			if (Sprite.PrimaryFile == nullptr)
			{
				return;
			}
			System.SetAnimation(Sprite, Operand.U8(0));
			return;
		case 0x34:
		{
			const int32 Gravity = DivideByPower(int64{Operand.S8(0)} * 2 * Sprite.Weight, 12) * 0x20;
			const int32 Ticks = System.TickExtraFrames + 1;
			Sprite.Gravity = Wrap(int64{Gravity} * Ticks * Ticks);
			return;
		}
		case 0x35:
			SetDrawFlag(0x8000000);
			return;
		case 0x36:
			Sprite.DrawFlags &= ~0x8000000u;
			return;
		case 0x37:
			Play.bReloadEffectImages = true;
			return;
		case 0x38:
		{
			uint32 Data = 0;
			if (!Inline(Data))
			{
				return;
			}
			Services->UploadImages(*Sprite.ScriptFile, Data);
			return;
		}
		case 0x3A:
		{
			uint32 Data = 0;
			if (!Inline(Data))
			{
				return;
			}
			const FSpriteFile& File = *Sprite.ScriptFile;
			// The level, steps and mode unsigned, the colour signed.
			const auto Signed = [&File, Data](uint32 Index) { return static_cast<int16>(static_cast<int8>(File.Byte(Data + Index))); };
			TintFade->Start(File.Byte(Data + 5), File.Byte(Data + 3), File.Byte(Data + 4), Signed(0), Signed(1), Signed(2));
			return;
		}
		case 0x3B:
			SetDrawFlag(0x20000000);
			return;
		case 0x3C:
			Sprite.DrawFlags &= ~0x20000000u;
			return;
		case 0x3D:
		{
			if (Sprite.PartCapacity == 0)
			{
				ReportFault(0xC300u | Command);
				return;
			}
			// The first part's texel block and its palette, copied where the trails and effects read them.
			const FSpritePart& Part = Sprite.Parts[0];
			const int16 X = static_cast<int16>((Part.U >> 2) + (Part.TexturePage & 0xF) * 0x40);
			const int16 Y = static_cast<int16>(Part.V + (Part.TexturePage & 0x10) * 0x10);
			Services->MoveImage({X, Y, 8, 8}, 0x3F0, 0x1F0);
			Services->MoveImage({static_cast<int16>(Part.Clut & 0x3F), static_cast<int16>(Part.Clut >> 6 & 0x1FF), 0x10, 1}, 0x3F0, 0x1EE);
			return;
		}
		case 0x3E:
			NextTarget(Sprite);
			return;
		case 0x3F:
			SetDrawFlag(0x4000000);
			return;
		case 0x40:
			HomeOnGoal(Sprite, Operand.U8(0));
			return;
		case 0x41:
			FrameSlots(static_cast<uint32>(FramedSlotBit(Sprite.Target) | FramedSlotBit(Play.Actor)));
			return;
		case 0x42:
			Sprite.TimeFlags &= ~0x20u;
			return;
		case 0x43:
			SetDrawFlag(0x80000000);
			return;
		case 0x44:
		case 0x45:
			// A development console's hold on the sprite; the retail game reads no files from a host.
			return;
		case 0x46:
			CountedWaits = 0;
			Services->LoadSpecialModel(Operand.U8(0));
			SpecialModelSteps = 0;
			return;
		case 0x47:
		{
			const int32 Sequence = static_cast<int8>(Operand.U8(0));
			uint16 Slots = Play.TargetMask;
			if (Sequence == 2)
			{
				if (Play.Actor == nullptr)
				{
					ReportFault(0xC300u | Command);
					return;
				}
				Slots = static_cast<uint16>(SlotBit(*Play.Actor));
			}
			Services->RunSpecialModel(Sequence, Slots);
			return;
		}
		case 0x48:
			Sprite.BuildFlags |= SpriteBuildFlags::ScaleFirst;
			return;
		case 0x49:
		case 0x4A:
		case 0x4B:
			if (!Sprite.bHasTransform)
			{
				return;
			}
			Sprite.Rotation[static_cast<std::size_t>(Command - 0x49)] = static_cast<int16>(Sprite.Rotation[static_cast<std::size_t>(Command - 0x49)] + Operand.S8(0));
			Sprite.DrawFlags |= SpriteDrawFlags::MatrixDirty;
			return;
		case 0x4C:
			Sprite.BuildFlags |= SpriteBuildFlags::OwnLight;
			return;
		case 0x4D:
			if (Sprite.SoundSet >= 0)
			{
				Services->StopSound(static_cast<uint32>(Operand.U8(0)) | static_cast<uint32>(Sprite.SoundSet) << 16);
			}
			return;
		case 0x4E:
			if (SystemSoundSet >= 0)
			{
				Services->StopSound(static_cast<uint32>(Operand.U8(0)) | static_cast<uint32>(SystemSoundSet) << 16);
			}
			return;
		case 0x4F:
			if (Sprite.SoundSet >= 0)
			{
				Services->PlaySystemSound(static_cast<uint32>(Operand.U8(0)) | static_cast<uint32>(Sprite.SoundSet) << 16);
			}
			return;
		case 0x50:
			Sprite.Wait = static_cast<uint16>(Sprite.Wait + 1);
			if (CountedWaits != 0)
			{
				--CountedWaits;
				return;
			}
			// Not yet: the instruction runs again next step.
			Sprite.Pc -= 2;
			return;
		case 0x51:
			Services->FreeSpecialModel();
			return;
		case 0x52:
			if (Sprite.SoundSet >= 0)
			{
				Services->PlaySoundOn(static_cast<uint32>(Operand.U8(0)) | static_cast<uint32>(Sprite.SoundSet) << 16, Operand.U8(1));
			}
			return;
		case 0x53:
			Sprite.TimeFlags &= ~(SpriteTimeFlags::FacingMirrored | SpriteTimeFlags::ListEntryFlipped);
			Sprite.DrawFlags &= ~(SpriteDrawFlags::Mirrored | SpriteDrawFlags::FlippedVertically);
			return;
		case 0x55:
			FrameSlots(static_cast<uint32>(FramedSlotBit(Play.Actor)) | Play.TargetMask);
			return;
		case 0x56:
		{
			if (!Play.bHitVoices)
			{
				return;
			}
			const int32 Slot = SlotOf(Sprite);
			uint32 Voice = Context->Slots[static_cast<std::size_t>(Slot)].Occupant;
			// Three characters share another's voice.
			Voice = Voice == 9 ? 2 : Voice == 10 ? 6 : Voice == 8 ? 15 : Voice;
			if ((Play.HitVoiced >> Slot & 1) != 0)
			{
				return;
			}
			Play.HitVoiced = static_cast<uint16>(Play.HitVoiced | 1 << Slot);
			if (SystemSoundSet >= 0)
			{
				Services->PlaySound((Voice + 0x52) | static_cast<uint32>(SystemSoundSet) << 16);
			}
			return;
		}
		case 0x57:
			Services->SetObjectsVisible(true);
			return;
		case 0x58:
			Services->SetObjectsVisible(false);
			return;
		case 0x59:
			Fade->Fade.bBlocked = false;
			return;
		case 0x5A:
			Fade->Fade.bBlocked = true;
			return;
		case 0x5F:
			Play.ScriptMark = 1;
			return;
		case 0x60:
			Play.ScriptMark = static_cast<uint8>(SlotOf(Sprite) + 2);
			return;
		case 0x61:
		{
			FSprite* Target = Sprite.Target;
			if (Target == nullptr)
			{
				ReportFault(0xC300u | Command);
				return;
			}
			Sprite.Position[1] = Wrap(int64{Sprite.FloorY - 1} * 0x10000);
			int16 Beside = static_cast<int16>(DivideByPower(int64{Operand.S8(0)} * Sprite.Scale, 12));
			// On the side of the target that faces this sprite: its slot's place tells which way it faces.
			const int32 TargetX = static_cast<int16>(Target->Position[0] >> 16);
			const uint16 SlotX = Context->Slots[static_cast<std::size_t>(SlotOf(*Target))].Position.X;
			if (TargetX == static_cast<int32>(SlotX))
			{
				if (!Sprite.IsMirroredFacing())
				{
					Beside = static_cast<int16>(-Beside);
				}
			}
			else if (TargetX < static_cast<int32>(SlotX))
			{
				Beside = static_cast<int16>(-Beside);
			}
			Sprite.SavedPoint[0] = static_cast<int16>(TargetX + Beside);
			Sprite.SavedPoint[1] = 0;
			Sprite.SavedPoint[2] = static_cast<int16>(Target->Position[2] >> 16);
			JumpArc(Sprite);
			return;
		}
		case 0x62:
			if (Play.Record < 1)
			{
				ReportFault(0xC300u | Command);
				return;
			}
			Numbers->ShowSlot(Context->Performances[static_cast<std::size_t>(Play.Record - 1)], SlotOf(Sprite), SlotSprites[static_cast<std::size_t>(SlotOf(Sprite))]);
			return;
		case 0x63:
		case 0x64:
			if (Command == 0x64)
			{
				Sprite.NextWord |= SpriteNextFlags::SizeFactorToChildren;
			}
			if (static_cast<int16>(Camera->SpriteDistance) < 0x201)
			{
				return;
			}
			Sprite.SizeFactor = Camera->SpriteDistance;
			Sprite.BuildFlags = (Sprite.BuildFlags & ~SpriteBuildFlags::SizeShiftMask) | 3u << SpriteBuildFlags::SizeShiftShift;
			Sprite.DrawFlags |= SpriteDrawFlags::MatrixDirty;
			return;
		case 0x65:
			FrameSlots(Play.TargetMask);
			return;
		case 0x66:
			Camera->bIgnoresHeights = true;
			return;
		case 0x67:
		{
			Sprite.Wait = static_cast<uint16>(Sprite.Wait + 1);
			const auto Apart = [](const FGteVector& A, const FGteVector& B) {
				return SquareRoot0(GteSquare(A[0] - B[0]) + GteSquare(A[1] - B[1]) + GteSquare(A[2] - B[2]));
			};
			if (Apart(Camera->DesiredEye, Camera->Eye) < 4 && Apart(Camera->DesiredTarget, Camera->Target) < 4)
			{
				return;
			}
			Sprite.Pc -= 2;
			return;
		}
		case 0x68:
			if (Sprite.Animation() >= 0)
			{
				System.SetResource(Sprite, Sprite.PrimaryFile);
				Sprite.NextWord &= ~SpriteNextFlags::AlternateFile;
				return;
			}
			System.SetResource(Sprite, Sprite.AlternateFile);
			Sprite.NextWord |= SpriteNextFlags::AlternateFile;
			return;
		case 0x69:
			if ((Sprite.NextWord & SpriteNextFlags::AlternateFile) == 0)
			{
				System.SetResource(Sprite, Sprite.PrimaryFile);
				Sprite.NextWord &= ~SpriteNextFlags::AlternateFile;
				return;
			}
			System.SetResource(Sprite, Sprite.AlternateFile);
			Sprite.NextWord |= SpriteNextFlags::AlternateFile;
			return;
		case 0x6A:
			Play.bHudHidden = true;
			Play.bMusicLowered = false;
			Services->SetMusicVolume(0, 0x78);
			return;
		case 0x6B:
			Services->RestoreVram();
			return;
		default:
			return;
		}
	}
}
