#include "Battling/BattlingDrawing.h"

#include "Battling/BattlingFight.h"
#include "Field/FieldSceneFrame.h"
#include "Graphics/GteVectors.h"
#include "Graphics/MdlLights.h"

#include <algorithm>
#include <string>

namespace Weltall
{
	namespace
	{
		/** The builder's light mode for a gear's parts: lit, their normals kept for the frames' relighting. */
		constexpr int32 GearLightMode = 2;
		/** Where each side's textures go: its page across VRAM's top, 4-bit, and its row of palettes. */
		constexpr std::array<int32, 2> GearPageX{0x3C0, 0x380};
		constexpr std::array<int32, 2> GearClutY{0x1FF, 0x1FE};
		constexpr uint16 ClutColumnBits = 0xF;
		/** The environment-mapped kind reads its side's page as 8-bit texels. */
		constexpr uint16 EightBitPage = 0x80;

		constexpr uint16 TexturePageOf(int32 Depth, int32 Blend, int32 X, int32 Y)
		{
			return static_cast<uint16>((Depth & 3) << 7 | (Blend & 3) << 5 | (Y & 0x100) >> 4 | (X & 0x3FF) >> 6 | (Y & 0x200) << 2);
		}

		constexpr uint16 ClutOf(int32 X, int32 Y)
		{
			return static_cast<uint16>(Y << 6 | (X >> 4 & 0x3F));
		}

		/** The kernel's ordering shift for a table of 1000h entries, and the display's lines less one, which its screen clip keeps. */
		constexpr int32 GearOrderingShift = 2;
		constexpr uint16 GearClipRows = FBattlingFrame::ScreenHeight - 1;
		/** InitGeom's average-depth scales. */
		constexpr int16 AverageScale3 = 0x155;
		constexpr int16 AverageScale4 = 0x100;

		/** The first light: white at half strength, from where the scene's start leaves it. */
		constexpr FGteLongVector SceneLightDirection{0x10, 0x10, 0x10};
		constexpr int16 SceneLightStrength = 0x800;

		/** The scale of a matrix's columns, as the GTE's rotation of each axis keeps it in 16 bits. */
		FGteMatrix ScaleAxes(const FGteMatrix& Matrix, const std::array<int16, 3>& Scale)
		{
			FGteMatrix Result = Matrix;
			for (std::size_t Column = 0; Column < 3; ++Column)
			{
				for (std::size_t Row = 0; Row < 3; ++Row)
				{
					Result.M[Row][Column] = static_cast<int16>(std::clamp<int64>((static_cast<int64>(Matrix.M[Row][Column]) * Scale[Column]) >> 12, -0x8000, 0x7FFF));
				}
			}
			return Result;
		}

		/** The light a hit throws on a gear: the opponent's colour for a projectile's or a guarded hit, else orange, by the flash; halved in some lightings. */
		FGteVector HitLightColour(const FBattlingFighter& Fighter, const FBattlingFighter& Opponent, bool bHalved)
		{
			const int32 Flash = Fighter.HitLight;
			FGteVector Colour{static_cast<int16>(Flash << 4), static_cast<int16>(Flash << 3), 0};
			if ((Fighter.Flags2 & BattlingFlags2::HitFlash) != 0)
			{
				for (std::size_t Channel = 0; Channel < 3; ++Channel)
				{
					Colour[Channel] = static_cast<int16>(Opponent.Colour[Channel] * Flash >> 4);
				}
			}
			if (bHalved)
			{
				for (int16& Channel : Colour)
				{
					Channel = static_cast<int16>(Channel / 2);
				}
			}
			return Colour;
		}
	}

	std::optional<FError> FBattlingDrawing::LoadGears(const FBattlingFight& Fight, const FGte& Lighting)
	{
		// The builder's page and CLUT go on from one part's build to the next, and from the first gear's to the second's.
		FMdlTextureState State;
		for (std::size_t Side = 0; Side < Gears.size(); ++Side)
		{
			const FBattlingSkeleton& Skeleton = Fight.Fighters[Side].Skeleton;
			const uint16 Page = TexturePageOf(0, 1, GearPageX[Side], 0);
			const uint16 ClutRow = static_cast<uint16>(ClutOf(0, GearClutY[Side]) & ~ClutColumnBits);
			std::vector<FBattlingGearPart>& Parts = Gears[Side];
			Parts.assign(Skeleton.Joints.size(), {});
			for (std::size_t Joint = 0; Joint < Skeleton.Joints.size(); ++Joint)
			{
				const int32 Number = Skeleton.Joints[Joint].Part;
				if (Number < 0)
				{
					continue;
				}
				if (static_cast<std::size_t>(Number) >= Skeleton.Model.Parts.size())
				{
					return MakeError("a gear's joint " + std::to_string(Joint) + " names part " + std::to_string(Number) + ", which its model lacks");
				}
				const FMdlPart& Part = Skeleton.Model.Parts[static_cast<std::size_t>(Number)];
				TResult<std::vector<FMdlPacket>> Built = BuildMdlPackets(Part, GearLightMode, Lighting);
				if (!Built)
				{
					return MakeError("a gear's part " + std::to_string(Number) + ": " + Built.Error().Message);
				}
				FBattlingGearPart& Kept = Parts[Joint];
				Kept.Packets = std::move(*Built);
				for (const FMdlPrimitive& Primitive : Part.Primitives)
				{
					if (Primitive.Type == EMdlPrimitiveType::EnvironmentMappedTriangle)
					{
						Kept.Textures.push_back({static_cast<uint16>(EightBitPage | Page), ClutRow});
						continue;
					}
					if (Primitive.Traits().bTextured && Skeleton.Joints[Joint].bOwnTextures)
					{
						State = Primitive.Texture(State);
					}
					else if (Primitive.Traits().bTextured)
					{
						// Every page a part names becomes the side's; a CLUT keeps its column in the side's row.
						if (!Primitive.bInheritsTexturePage)
						{
							State.TexturePage = Page;
						}
						if (!Primitive.bInheritsClut)
						{
							State.Clut = static_cast<uint16>((Primitive.Clut & ClutColumnBits) | ClutRow);
						}
					}
					Kept.Textures.push_back(State);
				}
			}
		}
		return std::nullopt;
	}

	FMdlLights FBattlingDrawing::GearLights(const FBattlingFight& Fight, uint32 Side) const
	{
		const FBattlingFighter& Fighter = Fight.Fighters[Side];
		const FBattlingFighter& Opponent = Fight.Fighters[1 - Side];
		FMdlLights Lights;
		Lights.SetDirectional(0, SceneLightDirection, {SceneLightStrength, SceneLightStrength, SceneLightStrength});
		Lights.SetDirectional(1,
			{Fighter.Position[0] - Opponent.Position[0], Fighter.Position[1] - Opponent.Position[1], Fighter.Position[2] - Opponent.Position[2]},
			HitLightColour(Fighter, Opponent, bHitLightsHalved));
		Lights.SetDirectional(2, {Fighter.Position[0] - Fight.Eye[0], Fighter.Position[1] - Fight.Eye[1], Fighter.Position[2] - Fight.Eye[2]}, {0, 0, 0});
		Lights.Ambient = {GearAmbient.R << 4, GearAmbient.G << 4, GearAmbient.B << 4};
		return Lights;
	}

	void FBattlingDrawing::DrawGear(const FBattlingFight& Fight, uint32 Side, const FGearView& View)
	{
		const FBattlingSkeleton& Skeleton = Fight.Fighters[Side].Skeleton;
		const std::vector<FBattlingGearPart>& Parts = Gears[Side];
		if (Parts.size() != Skeleton.Joints.size())
		{
			return;
		}
		const FMdlLights& Lights = View.Lights;
		FGte Gte = View.Gte;
		Gte.AverageScale3 = AverageScale3;
		Gte.AverageScale4 = AverageScale4;

		// The gear's root hung from the GTE's matrix: turned, scaled, at its place. The lights turn without the scale.
		const FGteMatrix RootTurn = RotationXyz(Skeleton.RootAngles);
		FGteMatrix Root = ScaleAxes(RootTurn, Skeleton.Scale);
		Root.T = View.RootPlace;
		const FGteMatrix RootDraw = CompMatrix(View.Gte.Transform, Root);

		// Each joint's matrices chained from its parent's, in the tree's order: a joint, its children, then its next sibling.
		const std::size_t Count = Skeleton.Joints.size();
		std::vector<FGteMatrix> Draws(Count);
		std::vector<FGteMatrix> Turns(Count);
		std::vector<std::size_t> Order;
		Order.reserve(Count);
		std::vector<std::size_t> Stack;
		const auto PushChildren = [&](int32 Parent) {
			for (std::size_t Joint = Count; Joint-- > 0;)
			{
				if (Skeleton.Joints[Joint].Parent == Parent || (Parent < 0 && Skeleton.Joints[Joint].Parent < 0))
				{
					Stack.push_back(Joint);
				}
			}
		};
		PushChildren(-1);
		while (!Stack.empty())
		{
			const std::size_t Joint = Stack.back();
			Stack.pop_back();
			Order.push_back(Joint);
			PushChildren(static_cast<int32>(Joint));
		}
		for (const std::size_t Joint : Order)
		{
			const FBattlingJoint& Posed = Skeleton.Joints[Joint];
			FGteMatrix Local = RotationXyz(Posed.Angles);
			Local.T = {Posed.Translation[0], Posed.Translation[1], Posed.Translation[2]};
			const bool bUnderRoot = Posed.Parent < 0;
			Turns[Joint] = MulMatrix(bUnderRoot ? RootTurn : Turns[static_cast<std::size_t>(Posed.Parent)], Local);
			Draws[Joint] = CompMatrix(bUnderRoot ? RootDraw : Draws[static_cast<std::size_t>(Posed.Parent)], Local);
			if (Posed.Part < 0 || Posed.bHidden || Parts[Joint].Packets.empty())
			{
				continue;
			}
			const FMdlPart& Part = Skeleton.Model.Parts[static_cast<std::size_t>(Posed.Part)];
			const FBattlingGearPart& Built = Parts[Joint];
			Lights.Load(Gte, Turns[Joint]);
			LightMatrixLeft = Gte.LightMatrix;
			Gte.Transform = Draws[Joint];
			Projected.resize(Part.Vertices.size());
			for (std::size_t Vertex = 0; Vertex < Part.Vertices.size(); ++Vertex)
			{
				Projected[Vertex] = Gte.Rtps(Part.Vertices[Vertex]);
			}
			Builder.Anchor(EWorldMapAnchorKind::Model, BattlingAnchors::GearJoint(Side, static_cast<uint32>(Joint)), Gte.Transform);
			for (std::size_t Number = 0; Number < Part.Primitives.size() && Number < Built.Packets.size(); ++Number)
			{
				const FMdlPrimitive& Primitive = Part.Primitives[Number];
				const FFieldPolygonTest Test = TestFieldPolygon(Primitive, Projected.data(), EFieldDrawMode::Relit, Gte, GearOrderingShift, GearClipRows, View.ClipColumns);
				if (Culling == EBattlingCulling::Original ? Test.Dropped != 0 : !Test.bInRange)
				{
					continue;
				}
				FMdlPacket Packet = Built.Packets[Number];
				if (Packet.Relight != EMdlRelight::None)
				{
					RelightMdlPacket(Packet, Primitive, Part, Gte);
				}
				const FMdlPrimitiveTraits& Traits = Primitive.Traits();
				FWorldMapPolygon& Polygon = Builder.Add(Test.Bucket);
				Polygon.Layer = EWorldMapLayer::Gear;
				Polygon.CornerCount = static_cast<uint8>(Traits.Corners);
				Polygon.bGouraud = Traits.bGouraud;
				Polygon.bTextured = Traits.bTextured;
				Polygon.bRaw = Traits.bTextured && (Packet.Code & 1) != 0;
				Polygon.bSemiTransparent = (Packet.Code & 2) != 0;
				std::array<FMdlUv, 4> Texels = Primitive.Uvs;
				if (Primitive.Type == EMdlPrimitiveType::EnvironmentMappedTriangle)
				{
					const std::array<FMdlUv, 3> Mapped = MdlEnvironmentTexels(Part, Primitive, Gte.Transform, View.Environment);
					std::copy(Mapped.begin(), Mapped.end(), Texels.begin());
				}
				uint8 LowU = 0xFF;
				uint8 LowV = 0xFF;
				uint8 HighU = 0;
				uint8 HighV = 0;
				for (std::size_t Corner = 0; Corner < static_cast<std::size_t>(Traits.Corners); ++Corner)
				{
					const std::size_t Vertex = Primitive.Vertices[Corner];
					Polygon.View[Corner] = ViewPoint(Gte.Transform, Part.Vertices[Vertex]);
					Polygon.ScreenX[Corner] = Projected[Vertex].ScreenX;
					Polygon.ScreenY[Corner] = Projected[Vertex].ScreenY;
					const FMdlColor& Colour = Packet.Colors[Traits.bGouraud ? Corner : 0];
					Polygon.Colors[Corner] = {Colour.R, Colour.G, Colour.B};
					if (Traits.bTextured)
					{
						Polygon.U[Corner] = Texels[Corner].U;
						Polygon.V[Corner] = Texels[Corner].V;
						Polygon.TexCoords[Corner] = {static_cast<float>(Polygon.U[Corner]), static_cast<float>(Polygon.V[Corner])};
						LowU = std::min(LowU, Polygon.U[Corner]);
						LowV = std::min(LowV, Polygon.V[Corner]);
						HighU = std::max(HighU, Polygon.U[Corner]);
						HighV = std::max(HighV, Polygon.V[Corner]);
					}
				}
				if (Traits.bTextured)
				{
					Polygon.TexturePage = Built.Textures[Number].TexturePage;
					Polygon.Clut = Built.Textures[Number].Clut;
					Polygon.TexelMin = {LowU, LowV};
					Polygon.TexelMax = {HighU, HighV};
				}
			}
		}
	}
}
