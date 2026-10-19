#include "Game/BattlingGameFiles.h"

#include "Media/GameFileIndex.h"

namespace Weltall
{
	namespace
	{
		constexpr int32 BattlingDataDirectory = 48;
		constexpr int32 BattlingEffectFile = 2;
	}

	TResult<std::unique_ptr<FBattlingGameFiles>> FBattlingGameFiles::Load(const FCdImage& Image, const FGameFileIndex& Index,
		const FPsxExecutable& Executable, EDiscRelease Release)
	{
		auto Files = std::make_unique<FBattlingGameFiles>();
		TResult<FBattlingProgramTables> Tables = FBattlingProgramTables::Read(Image, Index, Executable, Release);
		if (!Tables)
		{
			return Tables.Error();
		}
		Files->Tables = std::move(*Tables);
		TResult<FBattlingTerrain> Terrain = FBattlingTerrain::Read(Image, Index);
		if (!Terrain)
		{
			return Terrain.Error();
		}
		Files->Terrain = std::move(*Terrain);
		TResult<FBattlingGearTable> GearTable = FBattlingGearTable::Read(Image, Index);
		if (!GearTable)
		{
			return GearTable.Error();
		}
		Files->GearTable = *GearTable;
		TResult<FBattlingMessages> Messages = FBattlingMessages::Read(Image, Index);
		if (!Messages)
		{
			return Messages.Error();
		}
		Files->Messages = std::move(*Messages);
		const FIndexRecord* Record = Index.Find(BattlingDataDirectory, BattlingEffectFile);
		if (Record == nullptr || !Record->IsFile())
		{
			return MakeError("the disc lacks Battling's effects");
		}
		TResult<std::vector<uint8>> Bytes = ReadFileBytes(Image, *Record);
		if (!Bytes)
		{
			return Bytes.Error();
		}
		TResult<FSoundEffectFile> Effects = FSoundEffectFile::Parse(std::move(*Bytes));
		if (!Effects)
		{
			return Effects.Error();
		}
		Files->Effects = std::move(*Effects);
		TResult<FBattlingArenaTables> ArenaTables = FBattlingArenaTables::Read(Image, Index, Release);
		if (!ArenaTables)
		{
			return ArenaTables.Error();
		}
		Files->ArenaTables = *ArenaTables;
		TResult<FBattlingArenaTextures> ArenaTextures = FBattlingArenaTextures::Read(Image, Index);
		if (!ArenaTextures)
		{
			return ArenaTextures.Error();
		}
		Files->ArenaTextures = std::move(*ArenaTextures);
		return Files;
	}
}
