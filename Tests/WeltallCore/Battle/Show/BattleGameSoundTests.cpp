#include "Battle/Show/BattleGameSound.h"
#include "Media/GameFileIndex.h"
#include "Media/Iso9660.h"
#include "Media/PsxExecutable.h"
#include "Sound/GameSoundSession.h"
#include "Sound/SoundDriver.h"
#include "Support/GameDiscs.h"

#include <catch_amalgamated.hpp>

#include <algorithm>
#include <memory>
#include <vector>

using namespace Weltall;

namespace
{
	/** The battle's own bank (the gear battles' bank's copy), the enemies' bank and the battle's common effects, in directory 44. */
	constexpr int32 BattleDirectory = 44;
	constexpr int32 CommonSoundsFile = 3;
	constexpr int32 SystemBankFile = 5;
	constexpr int32 EnemyBankFile = 7;
	constexpr int32 GearBattleBankId = 2;
	constexpr int32 EnemyBankId = 0xC7;
	constexpr uint16 CommonSoundsId = 1;
	/** Where every bank of the battle asks to be in sound memory: the gear battles' bank's room. */
	constexpr uint32 GearBattleRoom = 0x68000;

	/** The game's sound started from the disc, with the battle's files it loads. */
	struct FBattleSound
	{
		explicit FBattleSound(const FIso9660& Disc)
		{
			auto Started = FGameSoundSession::Start(Disc);
			REQUIRE(Started);
			Session = std::move(*Started);
			auto Executable = ReadBootExecutable(Disc);
			REQUIRE(Executable);
			auto Index = FGameFileIndex::Read(*Executable);
			REQUIRE(Index);
			const auto Read = [&](int32 File) {
				const FIndexRecord* Record = Index->Find(BattleDirectory, File);
				REQUIRE(Record != nullptr);
				auto Bytes = ReadFileSectors(Disc.Image(), *Record);
				REQUIRE(Bytes);
				Bytes->resize(static_cast<std::size_t>(Record->Size));
				return std::move(*Bytes);
			};
			CommonSounds = Read(CommonSoundsFile);
			SystemBank = Read(SystemBankFile);
			EnemyBank = Read(EnemyBankFile);
		}

		const FSoundBank* Bank(int32 Id) { return Session->Sound()->Lock()->FindBank(Id); }

		std::unique_ptr<FGameSoundSession> Session;
		std::vector<uint8> CommonSounds;
		std::vector<uint8> SystemBank;
		std::vector<uint8> EnemyBank;
	};
}

TEST_CASE("The battle's banks take the gear battles' bank's room, and its end gives the room back to its own bank", "[Battle][Content]")
{
	REQUIRE_DISC(Disc, EDiscRelease::UsaDisc1);
	FBattleSound Game(*Disc);
	FResidentSound& Resident = Game.Session->Resident();
	REQUIRE(Resident.GearBattleBank == Game.Bank(GearBattleBankId));
	FBattleGameSound Sound(*Game.Session->Sound(), Resident, nullptr);

	// Until the kernel's bank gives up its room, a battle bank finds none.
	Sound.LoadBattleBank(Game.EnemyBank);
	CHECK(Game.Bank(EnemyBankId) == nullptr);
	CHECK(Sound.TakeError());

	Sound.UnloadGearBattleBank();
	CHECK(Resident.GearBattleBank == nullptr);
	CHECK(Game.Bank(GearBattleBankId) == nullptr);
	Sound.LoadBattleBank(Game.EnemyBank);
	const FSoundBank* Enemies = Game.Bank(EnemyBankId);
	REQUIRE(Enemies != nullptr);
	CHECK(FSoundDriver::BankAddress(*Enemies) == GearBattleRoom);
	CHECK(Sound.HasBank(EnemyBankId));

	// The battle's own bank in the enemies' place, then given back to the kernel as the battle ends.
	Sound.UnloadBattleBank();
	CHECK_FALSE(Sound.HasBank(EnemyBankId));
	Sound.LoadBattleBank(Game.SystemBank);
	CHECK(Sound.HasBank(GearBattleBankId));
	Sound.UnloadBattleBank();
	CHECK_FALSE(Sound.HasBank(GearBattleBankId));
	Sound.UnloadBattleBank();
	Sound.LoadGearBattleBank(Game.SystemBank);
	CHECK(Resident.GearBattleBank != nullptr);
	CHECK(Resident.GearBattleBank == Game.Bank(GearBattleBankId));
	CHECK(FSoundDriver::BankAddress(*Resident.GearBattleBank) == GearBattleRoom);
	CHECK_FALSE(Sound.TakeError());
}

TEST_CASE("The battle's effect files are known by the bytes they were registered from, and leave with the battle", "[Battle][Content]")
{
	REQUIRE_DISC(Disc, EDiscRelease::UsaDisc1);
	FBattleSound Game(*Disc);
	const std::vector<uint8> Copy = Game.CommonSounds;
	{
		FBattleGameSound Sound(*Game.Session->Sound(), Game.Session->Resident(), nullptr);
		Sound.RegisterEffects(Game.CommonSounds);
		CHECK(Sound.HasEffects(CommonSoundsId));
		// The same set from other bytes is refused as the driver refuses it, and those bytes unregister nothing.
		Sound.RegisterEffects(Copy);
		CHECK_FALSE(Sound.TakeError());
		Sound.UnregisterEffects(Copy);
		CHECK(Sound.HasEffects(CommonSoundsId));
		Sound.UnregisterEffects(Game.CommonSounds);
		CHECK_FALSE(Sound.HasEffects(CommonSoundsId));

		Sound.RegisterEffects(Game.CommonSounds);
		Sound.PlaySystemEffect(static_cast<uint32>(CommonSoundsId) << 16 | 0x4D);
		CHECK(Game.Session->Sound()->Lock()->PlayingEffects(FSoundDriver::AnyEffect) != 0);
		Sound.StopEffects(Copy);
		CHECK(Game.Session->Sound()->Lock()->PlayingEffects(FSoundDriver::AnyEffect) == 0);
		Sound.PlaySystemEffect(static_cast<uint32>(CommonSoundsId) << 16 | 0x4D);
	}
	// The battle gone, the driver holds none of its sets, and none of their effects plays.
	CHECK(Game.Session->Sound()->Lock()->FindEffects(CommonSoundsId) == nullptr);
	CHECK(Game.Session->Sound()->Lock()->PlayingEffects(FSoundDriver::AnyEffect) == 0);
}

TEST_CASE("A battle's voices play from the banks it loads as voices, which the Voices volume then hears", "[Battle][Content][Sound]")
{
	REQUIRE_DISC(Disc, EDiscRelease::UsaDisc1);
	// Fei's hit voice: the common effects' 52h, from the enemies' bank of cries and voices.
	constexpr uint32 FeiHitVoice = static_cast<uint32>(CommonSoundsId) << 16 | 0x52;
	const auto KindsOf = [](FGameSoundSystem& System, uint32 Effect)
	{
		uint32 Tracks = 0;
		{
			const FLockedGameSound Locked = System.Lock();
			Locked->PlayEffect(Effect);
			Tracks = Locked->PlayingEffects(Effect);
		}
		std::vector<int16> Samples(4000 * 2);
		System.Render(Samples);
		std::vector<ESoundKind> Kinds;
		const FLockedGameSound Locked = System.Lock();
		for (int32 Track = 0; Track < 16; ++Track)
		{
			if ((Tracks >> Track & 1) != 0)
			{
				// Effect track n plays on voice n + 8.
				Kinds.push_back(Locked->VoiceKind(Track + 8));
			}
		}
		Locked->StopAllEffects();
		return Kinds;
	};
	FBattleSound Game(*Disc);
	FBattleGameSound Sound(*Game.Session->Sound(), Game.Session->Resident(), nullptr);
	Sound.UnloadGearBattleBank();
	Sound.RegisterEffects(Game.CommonSounds);
	Sound.LoadBattleVoiceBank(Game.EnemyBank);
	REQUIRE_FALSE(Sound.TakeError());
	const std::vector<ESoundKind> Voiced = KindsOf(*Game.Session->Sound(), FeiHitVoice);
	REQUIRE_FALSE(Voiced.empty());
	CHECK(std::all_of(Voiced.begin(), Voiced.end(), [](ESoundKind Kind) { return Kind == ESoundKind::Voice; }));

	Sound.UnloadBattleBank();
	Sound.LoadBattleBank(Game.EnemyBank);
	const std::vector<ESoundKind> Plain = KindsOf(*Game.Session->Sound(), FeiHitVoice);
	REQUIRE_FALSE(Plain.empty());
	CHECK(std::all_of(Plain.begin(), Plain.end(), [](ESoundKind Kind) { return Kind == ESoundKind::Effect; }));
}
