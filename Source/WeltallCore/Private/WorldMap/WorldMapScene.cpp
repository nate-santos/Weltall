#include "WorldMap/WorldMapScene.h"

#include "Formats/Lzss.h"
#include "Foundation/LittleEndian.h"
#include "Media/GameFileIndex.h"
#include "Text/MessageOverrides.h"
#include "Text/MessageTableIds.h"
#include "WorldMap/WorldMapTables.h"

#include <algorithm>
#include <string>

namespace Weltall
{
	namespace
	{
		using namespace LittleEndian;

		enum ESection : int32
		{
			Places = 0,
			Models = 1,
			Meshes = 2,
			Placements = 3,
			Billboards = 4,
			Particles = 5,
			Messages = 6,
			AnimatedA = 7,
			AnimatedB = 8,
			FirstRegion = 10,
		};

		constexpr int32 FullSectionCount = 26;
		constexpr int32 CutsceneSectionCount = 10;
		constexpr std::size_t RegionBytes = 0x260;
		constexpr std::size_t TriggerSize = 16;
		constexpr std::size_t PlacementSize = 16;
		constexpr std::size_t TriangleSize = 14;
		constexpr std::size_t BillboardSize = 8;
		constexpr int16 ListEnd = -1;

		int32 ReadS32(std::span<const uint8> Bytes, std::size_t Offset)
		{
			return static_cast<int32>(ReadU32(Bytes, Offset));
		}

		/** The archive's sections: each from its offset to the next one's, the last to the archive's end. */
		TResult<std::vector<std::span<const uint8>>> SplitSections(std::span<const uint8> Archive)
		{
			if (Archive.size() < 4)
			{
				return MakeError("the world map's scene archive is empty");
			}
			const uint32 Count = ReadU32(Archive, 0);
			if (Count == 0 || 4 + std::size_t{Count} * 4 > Archive.size())
			{
				return MakeError("the world map's scene archive's section table does not fit it");
			}
			std::vector<std::span<const uint8>> Sections;
			for (uint32 Section = 0; Section < Count; ++Section)
			{
				const uint32 Start = ReadU32(Archive, 4 + Section * 4);
				const uint32 End = Section + 1 < Count ? ReadU32(Archive, 8 + Section * 4) : static_cast<uint32>(Archive.size());
				if (Start > End || End > Archive.size())
				{
					return MakeError("world map archive section " + std::to_string(Section) + " lies outside the archive");
				}
				Sections.push_back(Archive.subspan(Start, End - Start));
			}
			return Sections;
		}

		std::optional<FError> ParsePlaces(std::span<const uint8> Section, FWorldMapScene& Scene)
		{
			if (Section.size() < 8)
			{
				return MakeError("the world map's places section is too short");
			}
			std::size_t At = static_cast<std::size_t>(ReadS32(Section, 0));
			while (true)
			{
				if (At + 8 > Section.size())
				{
					return MakeError("the world map's entrance list runs past its section");
				}
				const int16 Id = ReadS16(Section, At + 2);
				if (Id == ListEnd)
				{
					break;
				}
				Scene.Entrances.push_back({ReadS16(Section, At), Id, ReadS16(Section, At + 4)});
				At += 8;
			}
			const std::size_t Lists = static_cast<std::size_t>(ReadS32(Section, 4));
			if (Lists + FWorldMapScene::TriggerListCount * 4 > Section.size())
			{
				return MakeError("the world map's trigger lists' offsets lie past their section");
			}
			for (int32 List = 0; List < FWorldMapScene::TriggerListCount; ++List)
			{
				const int32 Offset = ReadS32(Section, Lists + static_cast<std::size_t>(List) * 4);
				if (Offset < 0 || static_cast<std::size_t>(Offset) + TriggerSize > Section.size())
				{
					continue;
				}
				std::vector<FWorldMapTrigger> Triggers;
				for (std::size_t Record = static_cast<std::size_t>(Offset);; Record += TriggerSize)
				{
					if (Record + TriggerSize > Section.size())
					{
						return MakeError("world map trigger list " + std::to_string(List) + " runs past its section");
					}
					const int16 Map = ReadS16(Section, Record + 8);
					if (Map == ListEnd)
					{
						break;
					}
					Triggers.push_back({ReadS16(Section, Record), ReadS16(Section, Record + 2), ReadS16(Section, Record + 4), ReadS16(Section, Record + 6), Map,
						ReadS16(Section, Record + 10), ReadS16(Section, Record + 12), ReadS16(Section, Record + 14)});
				}
				Scene.TriggerLists[static_cast<std::size_t>(List)] = std::move(Triggers);
			}
			return std::nullopt;
		}

		std::optional<FError> ParseMeshes(std::span<const uint8> Section, std::size_t PartCount, FWorldMapScene& Scene)
		{
			if (Section.size() < 4 + PartCount * 4)
			{
				return MakeError("the world map's collision section is too short for its model's parts");
			}
			std::vector<uint32> Starts;
			for (std::size_t Part = 0; Part < PartCount; ++Part)
			{
				Starts.push_back(ReadU32(Section, 4 + Part * 4));
			}
			for (std::size_t Part = 0; Part < PartCount; ++Part)
			{
				const std::size_t Start = Starts[Part];
				if (Start + 8 > Section.size())
				{
					return MakeError("world map collision mesh " + std::to_string(Part) + " lies past its section");
				}
				FWorldMapMesh Mesh;
				const int32 Count = ReadS32(Section, Start);
				const std::size_t Vertices = Start + static_cast<std::size_t>(ReadS32(Section, Start + 4));
				if (Count < 0 || Start + 8 + static_cast<std::size_t>(Count) * TriangleSize > Section.size())
				{
					return MakeError("world map collision mesh " + std::to_string(Part) + "'s triangles run past its section");
				}
				int32 VertexCount = 0;
				for (int32 Triangle = 0; Triangle < Count; ++Triangle)
				{
					const std::size_t At = Start + 8 + static_cast<std::size_t>(Triangle) * TriangleSize;
					FWorldMapMeshTriangle Record;
					for (std::size_t Corner = 0; Corner < 3; ++Corner)
					{
						Record.Corners[Corner] = ReadS16(Section, At + Corner * 2);
						Record.Neighbours[Corner] = ReadS16(Section, At + 6 + Corner * 2);
						VertexCount = std::max(VertexCount, Record.Corners[Corner] + 1);
					}
					Record.Flags = ReadS16(Section, At + 12);
					Mesh.Triangles.push_back(Record);
				}
				if (Vertices + static_cast<std::size_t>(VertexCount) * 8 > Section.size())
				{
					return MakeError("world map collision mesh " + std::to_string(Part) + "'s vertices run past its section");
				}
				for (int32 Vertex = 0; Vertex < VertexCount; ++Vertex)
				{
					const std::size_t At = Vertices + static_cast<std::size_t>(Vertex) * 8;
					Mesh.Vertices.push_back({ReadS16(Section, At), ReadS16(Section, At + 2), ReadS16(Section, At + 4)});
				}
				Scene.Meshes.push_back(std::move(Mesh));
			}
			return std::nullopt;
		}

		std::optional<FError> ParsePlacements(std::span<const uint8> Section, FWorldMapScene& Scene)
		{
			if (Section.size() < 2)
			{
				return MakeError("the world map's placements section is empty");
			}
			const int16 Count = ReadS16(Section, 0);
			if (Count < 0 || 2 + static_cast<std::size_t>(Count) * PlacementSize > Section.size())
			{
				return MakeError("the world map's placements run past their section");
			}
			for (int16 Placement = 0; Placement < Count; ++Placement)
			{
				const std::size_t At = 2 + static_cast<std::size_t>(Placement) * PlacementSize;
				Scene.Placements.push_back({ReadS16(Section, At), ReadS16(Section, At + 2), ReadS16(Section, At + 4), ReadS16(Section, At + 6),
					ReadS16(Section, At + 8), {ReadS16(Section, At + 10), ReadS16(Section, At + 12), ReadS16(Section, At + 14)}});
			}
			return std::nullopt;
		}

		std::optional<FError> ParseBillboards(std::span<const uint8> Section, FWorldMapScene& Scene)
		{
			if (Section.size() < static_cast<std::size_t>(FWorldMapScene::BlockCount) * 8)
			{
				return MakeError("the world map's billboard lists are too short");
			}
			for (std::size_t Block = 0; Block < static_cast<std::size_t>(FWorldMapScene::BlockCount); ++Block)
			{
				const std::size_t Offset = ReadU32(Section, Block * 8);
				const int32 Count = ReadS32(Section, Block * 8 + 4);
				if (Offset == 0 || Count == 0)
				{
					continue;
				}
				if (Count < 0 || Offset + static_cast<std::size_t>(Count) * BillboardSize > Section.size())
				{
					return MakeError("world map billboard list " + std::to_string(Block) + " runs past its section");
				}
				for (int32 Billboard = 0; Billboard < Count; ++Billboard)
				{
					const std::size_t At = Offset + static_cast<std::size_t>(Billboard) * BillboardSize;
					Scene.Billboards[Block].push_back({ReadS16(Section, At), ReadS16(Section, At + 2), ReadS16(Section, At + 4), ReadS16(Section, At + 6)});
				}
			}
			return std::nullopt;
		}

		std::optional<FError> ParseRegion(std::span<const uint8> Section, FWorldMapScene& Scene)
		{
			if (Section.size() < RegionBytes)
			{
				return MakeError("a world map encounter region is too short");
			}
			FWorldMapRegion Region;
			for (std::size_t Formation = 0; Formation < Region.Formations.size(); ++Formation)
			{
				std::copy_n(Section.begin() + static_cast<std::ptrdiff_t>(Formation * FWorldMapRegion::FormationSize), FWorldMapRegion::FormationSize,
					Region.Formations[Formation].begin());
			}
			for (std::size_t Band = 0; Band < Region.Weights.size(); ++Band)
			{
				const std::size_t At = Region.Formations.size() * FWorldMapRegion::FormationSize + Band * 16;
				std::copy_n(Section.begin() + static_cast<std::ptrdiff_t>(At), 16, Region.Weights[Band].begin());
			}
			Scene.Regions.push_back(Region);
			return std::nullopt;
		}

		std::vector<uint8> Copy(std::span<const uint8> Bytes)
		{
			return std::vector<uint8>(Bytes.begin(), Bytes.end());
		}

		TResult<std::vector<uint8>> ReadFile(const FCdImage& Image, const FGameFileIndex& Index, int32 Number)
		{
			const FIndexRecord* Record = Index.Find(WorldMapFiles::Directory, Number);
			if (Record == nullptr)
			{
				return MakeError("the disc lacks world map file " + std::to_string(Number));
			}
			return ReadFileSectors(Image, *Record);
		}
	}

	TResult<FMessageTable> FWorldMapScene::Messages() const
	{
		return FMessageTable::Parse(MessageBytes);
	}

	TResult<FWorldMapScene> ParseWorldMapArchive(std::span<const uint8> Archive)
	{
		const TResult<std::vector<std::span<const uint8>>> Split = SplitSections(Archive);
		if (!Split)
		{
			return Split.Error();
		}
		const std::vector<std::span<const uint8>>& Sections = *Split;
		if (Sections.size() != FullSectionCount && Sections.size() != CutsceneSectionCount)
		{
			return MakeError("the world map's scene archive has " + std::to_string(Sections.size()) + " sections");
		}
		FWorldMapScene Scene;
		if (std::optional<FError> Error = ParsePlaces(Sections[Places], Scene))
		{
			return *Error;
		}
		TResult<FMdlModel> Model = ParseMdlModel(Sections[Models]);
		if (!Model)
		{
			return Model.Error();
		}
		Scene.Models = std::move(*Model);
		if (std::optional<FError> Error = ParseMeshes(Sections[Meshes], Scene.Models.Parts.size(), Scene))
		{
			return *Error;
		}
		if (std::optional<FError> Error = ParsePlacements(Sections[Placements], Scene))
		{
			return *Error;
		}
		for (const FWorldMapPlacement& Placement : Scene.Placements)
		{
			if (Placement.Part < 0 || static_cast<std::size_t>(Placement.Part) >= Scene.Models.Parts.size())
			{
				return MakeError("a world map placement names a model part the scene lacks");
			}
		}
		if (std::optional<FError> Error = ParseBillboards(Sections[Billboards], Scene))
		{
			return *Error;
		}
		Scene.ParticleEffects = Copy(Sections[Particles]);
		Scene.MessageBytes = Copy(Sections[Messages]);
		if (!FMessageTable::Parse(Scene.MessageBytes))
		{
			return MakeError("the world map's message table does not parse");
		}
		Scene.AnimatedTexturesA = Copy(Sections[AnimatedA]);
		Scene.AnimatedTexturesB = Copy(Sections[AnimatedB]);
		if (Sections.size() == FullSectionCount)
		{
			for (std::size_t Region = FirstRegion; Region < Sections.size(); ++Region)
			{
				if (std::optional<FError> Error = ParseRegion(Sections[Region], Scene))
				{
					return *Error;
				}
			}
		}
		return Scene;
	}

	TResult<std::vector<uint8>> ReadWorldMapMessages(const FCdImage& Image, const FGameFileIndex& Index, int32 Group)
	{
		const TResult<std::vector<uint8>> Packed = ReadFile(Image, Index, WorldMapFiles::Archive(WorldMapFiles::GroupHeader(Group)));
		if (!Packed)
		{
			return Packed.Error();
		}
		const TResult<std::vector<uint8>> Archive = UnpackLzss(*Packed);
		if (!Archive)
		{
			return Archive.Error();
		}
		TResult<FWorldMapScene> Scene = ParseWorldMapArchive(*Archive);
		if (!Scene)
		{
			return Scene.Error();
		}
		return std::move(Scene->MessageBytes);
	}

	TResult<FWorldMapScene> ReadWorldMapScene(const FCdImage& Image, const FGameFileIndex& Index, const FWorldMapSceneEntry& Entry)
	{
		const int32 GroupHeader = Entry.GroupHeader;
		const TResult<std::vector<uint8>> Packed = ReadFile(Image, Index, WorldMapFiles::Archive(GroupHeader));
		if (!Packed)
		{
			return Packed.Error();
		}
		const TResult<std::vector<uint8>> Archive = UnpackLzss(*Packed);
		if (!Archive)
		{
			return Archive.Error();
		}
		TResult<FWorldMapScene> Scene = ParseWorldMapArchive(*Archive);
		if (!Scene)
		{
			return Scene.Error();
		}
		const std::string Table = MessageTables::WorldMap(WorldMapFiles::Group(GroupHeader));
		if (std::optional<FAppliedMessages> Applied = ApplyMessageOverrides(Image, Table, Scene->MessageBytes, EMessageWindows::Program))
		{
			Scene->MessageBytes = std::move(Applied->Table);
		}
		const TResult<std::vector<uint8>> Blocks = ReadFile(Image, Index, WorldMapFiles::Terrain(GroupHeader));
		if (!Blocks)
		{
			return Blocks.Error();
		}
		TResult<FWorldMapTerrain> Terrain = FWorldMapTerrain::Parse(*Blocks, Entry.BlocksX, Entry.BlocksZ);
		if (!Terrain)
		{
			return Terrain.Error();
		}
		Scene->Terrain = std::move(*Terrain);
		Scene->GroupHeader = GroupHeader;
		return Scene;
	}
}
