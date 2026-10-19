#pragma once

#include "Foundation/Result.h"
#include "Foundation/Types.h"
#include "Graphics/Gte.h"
#include "Graphics/MdlModel.h"
#include "Text/MessageTable.h"
#include "WorldMap/WorldMapTables.h"
#include "WorldMap/WorldMapTerrain.h"

#include <array>
#include <optional>
#include <span>
#include <vector>

namespace Weltall
{
	class FCdImage;
	class FGameFileIndex;

	/** Where the party arrives from a field map's entrance: a place by its id, in whole units. */
	struct FWorldMapEntrance
	{
		int16 X = 0;
		int16 Id = 0;
		int16 Z = 0;
	};

	/** A place model put in the world: its model part, its flags (bit 0: the party collides with it), its place and angles. */
	struct FWorldMapPlacement
	{
		int16 Part = 0;
		int16 Flags = 0;
		int16 X = 0;
		int16 Y = 0;
		int16 Z = 0;
		FGteVector Angles{};
	};

	/** A triangle of a model's collision mesh: its corners, the triangle across each edge (-1 none), and its flags (1: a wall). */
	struct FWorldMapMeshTriangle
	{
		std::array<int16, 3> Corners{};
		std::array<int16, 3> Neighbours{};
		int16 Flags = 0;
	};

	/** A model part's collision mesh, at the model's half scale. */
	struct FWorldMapMesh
	{
		std::vector<FWorldMapMeshTriangle> Triangles;
		std::vector<FGteVector> Vertices;
	};

	/** A billboard of a block's list (a tree): its place (x, y, z, whole units) and a kind nothing reads. */
	struct FWorldMapBillboard
	{
		int16 X = 0;
		int16 Y = 0;
		int16 Z = 0;
		int16 Kind = 0;
	};

	/** An encounter region: the battles it holds and how often each comes in each story band. */
	struct FWorldMapRegion
	{
		static constexpr std::size_t FormationSize = 0x20;
		static constexpr int32 FormationCount = 16;
		static constexpr int32 BandCount = 4;

		/** The battle's formation records, as the battle reads them. */
		std::array<std::array<uint8, FormationSize>, FormationCount> Formations{};
		/** [band][formation]: the formation's weight in the band. */
		std::array<std::array<uint8, FormationCount>, BandCount> Weights{};
	};

	/**
	 * A world map scene group: its archive's sections (the places, the place models and their
	 * collision meshes and placements, the billboards, the particle effects, the messages, the animated
	 * textures, the encounter regions) and its terrain, as the Worldmap program reads them.
	 */
	struct FWorldMapScene
	{
		static constexpr int32 TriggerListCount = 4;
		static constexpr int32 BlockCount = 256;

		/** The group's header file in directory 36. */
		int32 GroupHeader = 0;
		std::vector<FWorldMapEntrance> Entrances;
		/** On foot, in gears, in the vehicle, and the story's list; none where the archive's offset lies outside its section. */
		std::array<std::optional<std::vector<FWorldMapTrigger>>, TriggerListCount> TriggerLists;
		FMdlModel Models;
		/** One per model part. */
		std::vector<FWorldMapMesh> Meshes;
		std::vector<FWorldMapPlacement> Placements;
		/** Per block, row by row. */
		std::array<std::vector<FWorldMapBillboard>, BlockCount> Billboards;
		/** Section 5's records, kept whole. */
		std::vector<uint8> ParticleEffects;
		/** The place names' and notices' message table. */
		std::vector<uint8> MessageBytes;
		/** Sections 7 and 8, kept whole. */
		std::vector<uint8> AnimatedTexturesA;
		std::vector<uint8> AnimatedTexturesB;
		/** Sixteen in the playable groups; none in the first cutscene groups. */
		std::vector<FWorldMapRegion> Regions;
		FWorldMapTerrain Terrain;
		/** The minimap's places the port's story shows with its own places, a bit a place as the game state's mask has them. */
		uint32 StoryDots = 0;

		/** The message table over MessageBytes. */
		WELTALLCORE_API TResult<FMessageTable> Messages() const;
	};

	/** Parses a scene group's unpacked archive (file header + 1). */
	WELTALLCORE_API TResult<FWorldMapScene> ParseWorldMapArchive(std::span<const uint8> Archive);

	/** Reads a scene's group: its archive and its terrain, of the size the scene table gives. */
	WELTALLCORE_API TResult<FWorldMapScene> ReadWorldMapScene(const FCdImage& Image, const FGameFileIndex& Index, const FWorldMapSceneEntry& Entry);

	/** Scene group Group's message table (its place names and messages), as its archive holds it. */
	WELTALLCORE_API TResult<std::vector<uint8>> ReadWorldMapMessages(const FCdImage& Image, const FGameFileIndex& Index, int32 Group);
}
