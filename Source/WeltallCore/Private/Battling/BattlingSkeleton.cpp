#include "Battling/BattlingSkeleton.h"

#include "Foundation/LittleEndian.h"
#include "Graphics/GteVectors.h"

#include <algorithm>

namespace Weltall
{
	namespace
	{
		int16 Saturate16(int64 Value)
		{
			return static_cast<int16>(std::clamp<int64>(Value, -0x8000, 0x7FFF));
		}

		/** The GTE's RTIR of (Scale, 0, 0) and its kin: column Axis of Matrix times Scale, as the IR registers keep it. */
		FGteMatrix ScaleAxes(const FGteMatrix& Matrix, const std::array<int16, 3>& Scale)
		{
			FGteMatrix Result = Matrix;
			for (std::size_t Column = 0; Column < 3; ++Column)
			{
				for (std::size_t Row = 0; Row < 3; ++Row)
				{
					Result.M[Row][Column] = Saturate16((static_cast<int64>(Matrix.M[Row][Column]) * Scale[Column]) >> 12);
				}
			}
			return Result;
		}

		int16& Field(FBattlingJoint& Joint, EBattlingChannelTarget Target)
		{
			switch (Target)
			{
			case EBattlingChannelTarget::AngleX:
				return Joint.Angles[0];
			case EBattlingChannelTarget::AngleY:
				return Joint.Angles[1];
			case EBattlingChannelTarget::AngleZ:
				return Joint.Angles[2];
			case EBattlingChannelTarget::TranslationX:
				return Joint.Translation[0];
			case EBattlingChannelTarget::TranslationY:
				return Joint.Translation[1];
			case EBattlingChannelTarget::TranslationZ:
			default:
				return Joint.Translation[2];
			}
		}

		void Apply(int16& Current, int16 Value, bool bAngle, int32 Divisor)
		{
			if (Divisor == 1)
			{
				Current = Value;
			}
			else if (bAngle)
			{
				Current = BattlingEaseAngle(Current, Value, Divisor);
			}
			else
			{
				Current = static_cast<int16>(Current + (Value - Current) / Divisor);
			}
		}

		/** A byte of a keyed channel's stream; the streams of every gear file are checked to stay inside it. */
		uint8 StreamByte(const std::vector<uint8>& Bytes, uint32 At)
		{
			return At < Bytes.size() ? Bytes[At] : 0;
		}
	}

	int16 BattlingEaseAngle(int32 Angle, int32 Target, int32 Divisor)
	{
		const uint32 Masked = static_cast<uint32>(Angle) & 0xFFF;
		int16 Result = static_cast<int16>(Masked);
		const uint32 Difference = (Masked - static_cast<uint32>(Target)) & 0xFFF;
		if (Difference != 0)
		{
			if (Difference < 0x800)
			{
				Result = static_cast<int16>(Result - static_cast<int16>(static_cast<int32>(Difference) / Divisor));
			}
			else
			{
				Result = static_cast<int16>(Result + static_cast<int16>(static_cast<int32>(0x1000 - Difference) / Divisor));
			}
		}
		return Result;
	}

	void FBattlingSkeleton::Build(const FBattlingGearFile& GearFile, FMdlModel InModel)
	{
		Model = std::move(InModel);
		File = &GearFile;
		PartVertices.clear();
		const uint32 PartCount = LittleEndian::ReadU32(GearFile.Bytes, GearFile.Model);
		for (uint32 Part = 0; Part < PartCount; ++Part)
		{
			PartVertices.push_back(GearFile.Model + LittleEndian::ReadU32(GearFile.Bytes, GearFile.Model + 0x10 + Part * 0x38 + 8));
		}
		Joints.clear();
		for (const FBattlingJointRecord& Record : GearFile.Joints)
		{
			FBattlingJoint Joint;
			Joint.Parent = Record.Parent;
			Joint.Part = Record.Part;
			Joint.Angles = Record.Angles;
			Joint.Translation = Record.Translation;
			Joints.push_back(Joint);
		}
		RootAngles = {};
		Scale = {0x1000, 0x1000, 0x1000};
		RootWorld = FGteMatrix::Identity();
	}

	void FBattlingSkeleton::Attach(int32 Parent, FMdlPart Part, const FGteVector& Angles)
	{
		Model.Parts.push_back(std::move(Part));
		FBattlingJoint Joint;
		Joint.Parent = static_cast<int16>(Parent);
		Joint.Part = static_cast<int16>(Model.Parts.size() - 1);
		Joint.Angles = Angles;
		Joint.bOwnTextures = true;
		Joints.push_back(Joint);
	}

	void FBattlingSkeleton::Pose()
	{
		RootWorld = ScaleAxes(RotationXyz(RootAngles), Scale);
		RootWorld.T = {};
		for (FBattlingJoint& Joint : Joints)
		{
			FGteMatrix Local = RotationXyz(Joint.Angles);
			Local.T = {Joint.Translation[0], Joint.Translation[1], Joint.Translation[2]};
			const FGteMatrix& Parent = Joint.Parent < 0 ? RootWorld : Joints[static_cast<std::size_t>(Joint.Parent)].World;
			Joint.World = CompMatrix(Parent, Local);
		}
	}

	FBattlingPoint FBattlingSkeleton::JointPosition(int32 JointNumber, int32 Vertex, const FBattlingPoint& Position) const
	{
		const FBattlingJoint& Joint = Joints[static_cast<std::size_t>(JointNumber)];
		if (Vertex == 0 || Joint.Part < 0)
		{
			return {Joint.World.T[0] + Position[0], Joint.World.T[1] + Position[1], Joint.World.T[2] + Position[2]};
		}
		const uint32 At = PartVertices[static_cast<std::size_t>(Joint.Part)] + static_cast<uint32>((Vertex - 1) * 8);
		const std::span<const uint8> Bytes = File->Bytes;
		const FGteVector Corner{LittleEndian::ReadS16(Bytes, At), LittleEndian::ReadS16(Bytes, At + 2), LittleEndian::ReadS16(Bytes, At + 4)};
		const FGteLongVector Place = RotTrans(Joint.World, Corner);
		return {Place[0] + Position[0], Place[1] + Position[1], Place[2] + Position[2]};
	}

	void FBattlingAnimationPlayer::Start(const FBattlingGearFile& File, int32 Number)
	{
		FrameCounter = 0;
		Animation = nullptr;
		Keyed.clear();
		if (Number >= 0 && static_cast<std::size_t>(Number) < File.Animations.size() && File.HasAnimation[static_cast<std::size_t>(Number)])
		{
			Animation = &File.Animations[static_cast<std::size_t>(Number)];
			for (const FBattlingChannel& Channel : Animation->KeyedChannels)
			{
				Keyed.push_back({Channel.Value, 0, 0, 0});
			}
		}
	}

	bool FBattlingAnimationPlayer::Step(const FBattlingGearFile& File, FBattlingSkeleton& Skeleton, int32 Frames, int32 Divisor)
	{
		if (Animation == nullptr)
		{
			return true;
		}
		if (Frames == 0)
		{
			return false;
		}
		if (Animation->Frames < FrameCounter + Frames)
		{
			Frames = Animation->Frames - FrameCounter;
		}
		Divisor -= Frames;
		if (Divisor < 1)
		{
			Divisor = 1;
		}
		FrameCounter = static_cast<int16>(FrameCounter + Frames);

		for (const FBattlingChannel& Channel : Animation->StaticChannels)
		{
			Apply(Field(Skeleton.Joints[Channel.Joint], Channel.Target), static_cast<int16>(Channel.Value), Channel.IsAngle(), Divisor);
		}
		for (std::size_t Index = 0; Index < Animation->KeyedChannels.size(); ++Index)
		{
			const FBattlingChannel& Channel = Animation->KeyedChannels[Index];
			FKeyedState& State = Keyed[Index];
			for (int32 Step = 0; Step < Frames; ++Step)
			{
				if (State.Hold == 0)
				{
					// A byte below 80h is a 7-bit delta; 80h-BFh holds the delta for its low six bits more steps; C0h-FFh with
					// the next byte is a 14-bit delta.
					const uint8 Code = StreamByte(File.Bytes, State.Stream++);
					if ((Code & 0x80) == 0)
					{
						State.Delta = static_cast<int16>(static_cast<int8>(Code << 1) >> 1);
					}
					else if ((Code & 0x40) == 0)
					{
						State.Hold = static_cast<int16>(Code & 0x3F);
					}
					else
					{
						const int8 High = static_cast<int8>(StreamByte(File.Bytes, State.Stream++));
						State.Delta = static_cast<int16>((Code & 0x3F) | (static_cast<int32>(High) << 6));
					}
				}
				else
				{
					State.Hold = static_cast<int16>(State.Hold - 1);
				}
				State.Value = static_cast<int16>(State.Value + State.Delta);
			}
			Apply(Field(Skeleton.Joints[Channel.Joint], Channel.Target), State.Value, Channel.IsAngle(), Divisor);
		}
		return FrameCounter == Animation->Frames && Divisor == 1;
	}
}
