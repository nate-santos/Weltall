#include "Battle/Show/BattleStageFiles.h"

#include "Foundation/LittleEndian.h"
#include "Media/GameFileIndex.h"

#include <string>
#include <utility>

namespace Weltall
{
	namespace
	{
		using namespace LittleEndian;

		/** The stages' group record: its size is minus the count of its files. */
		constexpr int32 GroupFile = 5;
		constexpr int32 FirstModelFile = 6;
		constexpr int32 FirstRecordFile = 7;

		constexpr std::size_t TerrainOffset = 0x340;
		constexpr std::size_t ObjectScaleOffset = 0x344;
		constexpr std::size_t TrackPoolOffset = 0x348;
		constexpr std::size_t TrailPoolOffset = 0x34A;
		constexpr std::size_t CameraBoundsOffset = 0x34C;
		constexpr std::size_t ClearFlagOffset = 0x35E;
		constexpr std::size_t GroundSoundOffset = 0x35F;
		constexpr std::size_t ElementsOffset = 0x360;
		constexpr std::size_t ElementSize = 0x28;
		/** The kind of stage element that makes the screen clear to the stage's clear colour. */
		constexpr int16 ClearColourElement = 5;
		constexpr std::size_t DomeGridOffset = 0x450;
		constexpr std::size_t ClearColourOffset = 0x458;
		constexpr std::size_t LightDirectionOffset = 0x464;
		constexpr std::size_t LightColourOffset = 0x46C;
		constexpr std::size_t AmbientOffset = 0x474;
		constexpr std::size_t ShadowOffset = 0x478;
		constexpr std::size_t PresetsOffset = 0x47C;
		constexpr std::size_t PresetSize = 0xC;
		constexpr std::size_t GroundPointOffset = 0x4DC;
		constexpr std::size_t VerticesWord = 0x50C;
		constexpr std::size_t TrianglesWord = 0x510;
		constexpr std::size_t ArchiveWord = 0x514;
		/** The record's fixed part: everything up to the three offsets. */
		constexpr std::size_t FixedSize = 0x518;
		constexpr std::size_t VertexSize = 8;
		constexpr std::size_t TriangleSize = 0xE;
		/** The archive's first section: four offsets from it, to the obstacles and the three scrolls' band speeds. */
		constexpr std::size_t TableCount = 4;
		constexpr std::size_t ObstacleSize = 6;
		constexpr std::size_t BonePlaceSize = 8;
		constexpr std::size_t BonePlacesSection = 4;

		bool Has(std::span<const uint8> Bytes, std::size_t Offset, std::size_t Count)
		{
			return Offset <= Bytes.size() && Count <= Bytes.size() - Offset;
		}

		std::array<uint8, 3> ReadRgb(std::span<const uint8> Bytes, std::size_t Offset)
		{
			return {Bytes[Offset], Bytes[Offset + 1], Bytes[Offset + 2]};
		}

		FGteVector ReadVector(std::span<const uint8> Bytes, std::size_t Offset)
		{
			return {ReadS16(Bytes, Offset), ReadS16(Bytes, Offset + 2), ReadS16(Bytes, Offset + 4)};
		}

		TResult<std::vector<uint8>> ReadFile(const FCdImage& Image, const FGameFileIndex& Index, int32 Number)
		{
			const FIndexRecord* Record = Index.Find(FBattleStageFiles::Directory, Number);
			if (Record == nullptr || !Record->IsFile())
			{
				return MakeError("the disc lacks file " + std::to_string(Number) + " of the stages' directory");
			}
			return ReadFileBytes(Image, *Record);
		}
	}

	TResult<FBattleStageRecord> FBattleStageRecord::Parse(std::span<const uint8> File)
	{
		if (!Has(File, 0, 4) || !Has(File, 4, ReadU32(File, 0)) || ReadU32(File, 0) < FixedSize)
		{
			return MakeError("a battle stage's record is shorter than its fixed part or than its length");
		}
		FBattleStageRecord Stage;
		Stage.Record.assign(File.begin() + 4, File.begin() + 4 + static_cast<std::ptrdiff_t>(ReadU32(File, 0)));
		const std::span<const uint8> Bytes = Stage.Record;

		const uint32 VerticesAt = ReadU32(Bytes, VerticesWord);
		const uint32 TrianglesAt = ReadU32(Bytes, TrianglesWord);
		if (VerticesAt != 0 && TrianglesAt != 0)
		{
			if (TrianglesAt < VerticesAt || !Has(Bytes, TrianglesAt, 4))
			{
				return MakeError("a battle stage's floor triangles do not follow its corners");
			}
			const std::size_t VertexCount = (TrianglesAt - VerticesAt) / VertexSize;
			for (std::size_t Vertex = 0; Vertex < VertexCount; ++Vertex)
			{
				const FGteVector Corner = ReadVector(Bytes, VerticesAt + Vertex * VertexSize);
				Stage.Vertices.push_back({Corner[0], Corner[1], Corner[2]});
			}
			const uint32 TriangleCount = ReadU32(Bytes, TrianglesAt);
			if (!Has(Bytes, TrianglesAt + 4, std::size_t{TriangleCount} * TriangleSize))
			{
				return MakeError("a battle stage's floor triangles run past its record");
			}
			for (uint32 Triangle = 0; Triangle < TriangleCount; ++Triangle)
			{
				const std::size_t At = TrianglesAt + 4 + std::size_t{Triangle} * TriangleSize;
				FBattleFloorTriangle Entry;
				for (std::size_t Corner = 0; Corner < 3; ++Corner)
				{
					Entry.Corners[Corner] = ReadS16(Bytes, At + 2 * Corner);
					Entry.Neighbours[Corner] = ReadS16(Bytes, At + 6 + 2 * Corner);
					if (Entry.Corners[Corner] < 0 || static_cast<std::size_t>(Entry.Corners[Corner]) >= VertexCount
						|| Entry.Neighbours[Corner] >= static_cast<int32>(TriangleCount))
					{
						return MakeError("a battle stage's floor triangle names a corner or a neighbour it does not have");
					}
				}
				Entry.Attribute = Bytes[At + 0xC];
				Stage.Triangles.push_back(Entry);
			}
		}

		const uint32 ArchiveAt = ReadU32(Bytes, ArchiveWord);
		if (!Has(Bytes, ArchiveAt, 4) || ReadU32(Bytes, ArchiveAt) < 1 || !Has(Bytes, ArchiveAt + 4, 4 * std::size_t{ReadU32(Bytes, ArchiveAt)}))
		{
			return MakeError("a battle stage's archive lies past its record");
		}
		const uint32 Sections = ReadU32(Bytes, ArchiveAt);
		const uint32 TablesAt = ArchiveAt + ReadU32(Bytes, ArchiveAt + 4);
		if (!Has(Bytes, TablesAt, 4 * TableCount))
		{
			return MakeError("a battle stage's obstacles and scrolls lie past its record");
		}
		std::array<uint32, TableCount> Tables{};
		for (std::size_t Table = 0; Table < TableCount; ++Table)
		{
			Tables[Table] = TablesAt + ReadU32(Bytes, TablesAt + 4 * Table);
			if (!Has(Bytes, Tables[Table], 2))
			{
				return MakeError("a battle stage's obstacles or scrolls lie past its record");
			}
		}
		const uint16 ObstacleCount = ReadU16(Bytes, Tables[0]);
		if (!Has(Bytes, Tables[0] + 2, std::size_t{ObstacleCount} * ObstacleSize))
		{
			return MakeError("a battle stage's obstacles run past its record");
		}
		for (uint16 Obstacle = 0; Obstacle < ObstacleCount; ++Obstacle)
		{
			const FGteVector Values = ReadVector(Bytes, Tables[0] + 2 + std::size_t{Obstacle} * ObstacleSize);
			Stage.StageObstacles.push_back({Values[0], Values[1], Values[2]});
		}
		for (std::size_t Table = 0; Table < ScrollSpeedTables; ++Table)
		{
			Stage.ScrollSpeedOffsets[Table] = Tables[Table + 1];
		}
		for (uint32 Section = 1; Section < Sections; ++Section)
		{
			const uint32 At = ArchiveAt + ReadU32(Bytes, ArchiveAt + 4 + 4 * std::size_t{Section});
			if (!Has(Bytes, At, 2))
			{
				return MakeError("a battle stage's sequence lies past its record");
			}
			Stage.SequenceOffsets.push_back(At);
		}
		return Stage;
	}

	std::array<uint8, 4> FBattleStageRecord::TerrainBytes() const
	{
		return {Record[TerrainOffset], Record[TerrainOffset + 1], Record[TerrainOffset + 2], Record[TerrainOffset + 3]};
	}

	int16 FBattleStageRecord::ObjectScale() const { return ReadS16(Record, ObjectScaleOffset); }
	int16 FBattleStageRecord::TrackPoolSize() const { return ReadS16(Record, TrackPoolOffset); }
	int16 FBattleStageRecord::TrailPoolSize() const { return ReadS16(Record, TrailPoolOffset); }

	std::array<int16, 4> FBattleStageRecord::CameraBounds() const
	{
		return {ReadS16(Record, CameraBoundsOffset), ReadS16(Record, CameraBoundsOffset + 2), ReadS16(Record, CameraBoundsOffset + 4),
			ReadS16(Record, CameraBoundsOffset + 6)};
	}

	uint8 FBattleStageRecord::ClearFlag() const
	{
		for (int32 Index = 0; Index < ElementCount; ++Index)
		{
			if (Element(Index).Kind() == ClearColourElement)
			{
				return 1;
			}
		}
		return Record[ClearFlagOffset];
	}
	uint8 FBattleStageRecord::GroundSound() const { return Record[GroundSoundOffset]; }

	FBattleStageElement FBattleStageRecord::Element(int32 Index) const
	{
		const std::size_t At = ElementsOffset + static_cast<std::size_t>(Index) * ElementSize;
		FBattleStageElement Element;
		for (std::size_t Word = 0; Word < Element.Point.size(); ++Word)
		{
			Element.Point[Word] = static_cast<int32>(ReadU32(Record, At + 4 * Word));
		}
		for (std::size_t Word = 0; Word < FBattleStageElement::WordCount; ++Word)
		{
			Element.Words[Word] = ReadS16(Record, At + 0x10 + 2 * Word);
		}
		return Element;
	}

	std::array<int16, 4> FBattleStageRecord::DomeGrid() const
	{
		return {ReadS16(Record, DomeGridOffset), ReadS16(Record, DomeGridOffset + 2), ReadS16(Record, DomeGridOffset + 4), ReadS16(Record, DomeGridOffset + 6)};
	}

	std::array<uint8, 3> FBattleStageRecord::ClearColour() const { return ReadRgb(Record, ClearColourOffset); }
	FGteVector FBattleStageRecord::LightDirection() const { return ReadVector(Record, LightDirectionOffset); }
	FGteVector FBattleStageRecord::LightColour() const { return ReadVector(Record, LightColourOffset); }
	std::array<uint8, 3> FBattleStageRecord::AmbientColour() const { return ReadRgb(Record, AmbientOffset); }
	std::array<uint8, 3> FBattleStageRecord::ShadowColour() const { return ReadRgb(Record, ShadowOffset); }

	std::optional<FBattleCameraPreset> FBattleStageRecord::CameraPreset(int32 Index) const
	{
		const std::size_t At = PresetsOffset + static_cast<std::size_t>(Index) * PresetSize;
		if (Index < 0 || !Has(Record, At, PresetSize))
		{
			return std::nullopt;
		}
		return FBattleCameraPreset{ReadVector(Record, At), ReadVector(Record, At + 6)};
	}

	FGteVector FBattleStageRecord::GroundPoint() const { return ReadVector(Record, GroundPointOffset); }

	uint32 FBattleStageRecord::ArchiveOffset() const { return ReadU32(Record, ArchiveWord); }

	TResult<FBattleStageModel> FBattleStageModel::Parse(std::span<const uint8> File)
	{
		TResult<FGearModelPack> Pack = ParseGearModelPack(File);
		if (!Pack)
		{
			return MakeError("a battle stage's model: " + Pack.Error().Message);
		}
		if (!Has(File, 0, 4 + 4 * (BonePlacesSection + 1)) || ReadU32(File, 0) <= BonePlacesSection)
		{
			return MakeError("a battle stage's model has no section placing its bones");
		}
		FBattleStageModel Model;
		Model.Pack = std::move(*Pack);
		const std::size_t PlacesAt = ReadU32(File, 4 + 4 * BonePlacesSection);
		const std::size_t Bones = Model.Pack.Skeleton.size();
		if (!Has(File, PlacesAt, Bones * BonePlaceSize))
		{
			return MakeError("a battle stage's bone places run past its model");
		}
		for (std::size_t Bone = 0; Bone < Bones; ++Bone)
		{
			const std::size_t At = PlacesAt + Bone * BonePlaceSize;
			Model.BonePlaces.push_back({ReadVector(File, At), ReadS16(File, At + 6)});
		}
		return Model;
	}

	int32 FBattleStageFiles::Count(const FGameFileIndex& Index)
	{
		const FIndexRecord* Group = Index.Find(Directory, GroupFile);
		return Group == nullptr || Group->Size >= 0 ? 0 : -Group->Size / 2;
	}

	TResult<FBattleStageFiles> FBattleStageFiles::Load(const FCdImage& Image, const FGameFileIndex& Index, int32 Number)
	{
		if (Number < 0 || Number >= Count(Index))
		{
			return MakeError("the disc has no battle stage " + std::to_string(Number));
		}
		TResult<std::vector<uint8>> ModelFile = ReadFile(Image, Index, FirstModelFile + 2 * Number);
		if (!ModelFile)
		{
			return ModelFile.Error();
		}
		TResult<std::vector<uint8>> RecordFile = ReadFile(Image, Index, FirstRecordFile + 2 * Number);
		if (!RecordFile)
		{
			return RecordFile.Error();
		}
		TResult<FBattleStageModel> Model = FBattleStageModel::Parse(*ModelFile);
		if (!Model)
		{
			return MakeError("stage " + std::to_string(Number) + ": " + Model.Error().Message);
		}
		TResult<FBattleStageRecord> Record = FBattleStageRecord::Parse(*RecordFile);
		if (!Record)
		{
			return MakeError("stage " + std::to_string(Number) + ": " + Record.Error().Message);
		}
		return FBattleStageFiles{std::move(*Model), std::move(*Record)};
	}
}
