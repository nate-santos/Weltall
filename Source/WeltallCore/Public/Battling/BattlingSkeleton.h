#pragma once

#include "Battling/BattlingData.h"
#include "Graphics/Gte.h"
#include "Graphics/MdlModel.h"

#include <array>
#include <vector>

namespace Weltall
{
	/** A point of the arena: x, z across it, y down. */
	using FBattlingPoint = std::array<int32, 3>;

	struct FBattlingJoint
	{
		int16 Parent = -1;
		int16 Part = -1;
		FGteVector Angles{};
		FGteVector Translation{};
		/** Its part is not drawn (the animation events' hide and show). */
		bool bHidden = false;
		/** Its part keeps the texture pages and CLUTs it names, built with the gear's VRAM places cleared; the file's parts take the side's. */
		bool bOwnTextures = false;
		/** Its place in the gear, relative to the gear's position: the matrix the last pose built. */
		FGteMatrix World = FGteMatrix::Identity();
	};

	/**
	 * A gear's hierarchy as Battling builds it from its file: the joints with the values its animations drive, and the
	 * matrices each pose builds. The program builds the matrices only as it draws the gear, so whatever reads a joint's place
	 * reads the last pose drawn; before the first, every matrix is the identity.
	 */
	class FBattlingSkeleton
	{
	public:
		WELTALLCORE_API void Build(const FBattlingGearFile& GearFile, FMdlModel InModel);

		/**
		 * Hangs a part of the program's own on joint Parent, turned by Angles, after the file's joints: no animation or
		 * event names it, and it keeps its own textures.
		 */
		WELTALLCORE_API void Attach(int32 Parent, FMdlPart Part, const FGteVector& Angles);

		/** The root's rotation: the fighter's yaw about y; x and z stay 0. */
		FGteVector RootAngles{};
		/** The gear's scale on each axis (4096 = 1). */
		std::array<int16, 3> Scale{0x1000, 0x1000, 0x1000};
		FGteMatrix RootWorld = FGteMatrix::Identity();
		std::vector<FBattlingJoint> Joints;
		FMdlModel Model;
		/** The gear file, and where each part's vertex array lies in it: the program reads vertices from there, wherever a number points. */
		const FBattlingGearFile* File = nullptr;
		std::vector<uint32> PartVertices;

		/** Builds every joint's matrix from the joints' values, as drawing the gear does. */
		WELTALLCORE_API void Pose();

		/**
		 * Where a joint, or a vertex of its part (1-based; 0 the joint itself), stands in the arena for a gear at Position,
		 * by the last pose's matrices. A vertex number past either end of the part's list reads the model's neighbouring
		 * bytes, as the program does (two gears' events name one).
		 */
		WELTALLCORE_API FBattlingPoint JointPosition(int32 Joint, int32 Vertex, const FBattlingPoint& Position) const;
	};

	/**
	 * An animation bound to a skeleton: its frame counter and each keyed channel's place in its stream, its hold and its
	 * delta, as the program keeps them for the animation playing.
	 */
	class FBattlingAnimationPlayer
	{
	public:
		/** Starts Animation (or none, which counts as finished) from its first frame. */
		WELTALLCORE_API void Start(const FBattlingGearFile& File, int32 Animation);

		/**
		 * Steps Frames frames, every channel's joint value moving 1/Divisor of the way to its new value (all of it for a divisor
		 * of 1, angles the short way round). True when the last frame is reached with a divisor of 1, or with no animation.
		 */
		WELTALLCORE_API bool Step(const FBattlingGearFile& File, FBattlingSkeleton& Skeleton, int32 Frames, int32 Divisor);

		int16 Frame() const { return FrameCounter; }

	private:
		struct FKeyedState
		{
			uint32 Stream = 0;
			int16 Hold = 0;
			int16 Value = 0;
			int16 Delta = 0;
		};

		const FBattlingAnimation* Animation = nullptr;
		int16 FrameCounter = 0;
		std::vector<FKeyedState> Keyed;
	};

	/** The program's ease of an angle (12 bits) 1/Divisor of the way toward Target, the short way round. */
	WELTALLCORE_API int16 BattlingEaseAngle(int32 Angle, int32 Target, int32 Divisor);
}
