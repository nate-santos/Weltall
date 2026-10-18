#include "Sound/GameSoundSession.h"

#include "Media/DiscIdentification.h"
#include "Media/Iso9660.h"
#include "Media/PsxExecutable.h"
#include "Sound/SoundDriverTables.h"

namespace Weltall
{
	int32 SoundDriverOutputMode(ESoundOutput Output)
	{
		switch (Output)
		{
		case ESoundOutput::Mono:
			return 0;
		case ESoundOutput::WideStereo:
			return 2;
		case ESoundOutput::Stereo:
			break;
		}
		return 1;
	}

	TResult<std::unique_ptr<FGameSoundSession>> FGameSoundSession::Start(const FIso9660& Disc)
	{
		const TResult<FDiscIdentity> Identity = IdentifyDisc(Disc);
		if (!Identity)
		{
			return Identity.Error();
		}
		if (!IsPlayableRelease(Identity->Release))
		{
			return MakeError("playing requires a USA or supported undub disc");
		}
		const TResult<FPsxExecutable> Executable = ReadBootExecutable(Disc);
		if (!Executable)
		{
			return Executable.Error();
		}
		TResult<FGameFileIndex> Index = FGameFileIndex::Read(*Executable);
		if (!Index)
		{
			return Index.Error();
		}
		const TResult<FSoundDriverTables> Tables = FSoundDriverTables::Read(*Executable);
		if (!Tables)
		{
			return Tables.Error();
		}
		TResult<FFieldSongTable> Songs = FFieldSongTable::Read(Disc.Image(), *Index, GameplayRelease(Identity->Release));
		if (!Songs)
		{
			return Songs.Error();
		}

		std::unique_ptr<FGameSoundSession> Session(new FGameSoundSession());
		Session->Index = std::make_unique<FGameFileIndex>(std::move(*Index));
		Session->GameSound = std::make_shared<FGameSoundSystem>(*Tables);
		TResult<std::unique_ptr<FResidentSound>> Resident = LoadResidentSound(*Session->GameSound, Disc.Image(), *Session->Index);
		if (!Resident)
		{
			return Resident.Error();
		}
		Session->ResidentSound = std::move(*Resident);
		Session->Music = std::make_unique<FFieldMusic>(*Session->GameSound, *Session->ResidentSound, Disc.Image(), *Session->Index, std::move(*Songs));
		Session->Songs = std::make_unique<FFieldGameSongs>(*Session->Music, *Session->GameSound);
		Session->Effects = std::make_unique<FFieldGameSoundEffects>(*Session->GameSound, *Session->ResidentSound, Disc.Image(), *Session->Index);
		Session->WorldMap = std::make_unique<FWorldMapMusic>(*Session->GameSound, *Session->Music, Disc.Image(), *Session->Index);
		Session->Music->SetBankRoomRelease([BankOwner = Session->WorldMap.get()](FSoundDriver& Driver) { BankOwner->ReleaseBank(Driver); });
		return Session;
	}

	void FGameSoundSession::Apply(ESoundOutput Output, const FSoundDriverOptions& DriverOptions, const FFieldMusicOptions& MusicOptions)
	{
		Music->SetOptions(MusicOptions);
		const FLockedGameSound Locked = GameSound->Lock();
		Locked->SetOptions(DriverOptions);
		const int32 Mode = SoundDriverOutputMode(Output);
		if (Locked->OutputMode() != Mode)
		{
			Locked->SetOutputMode(Mode);
		}
	}

	FGameSoundSession::~FGameSoundSession()
	{
		// The audio thread may still make samples: nothing the driver points at may go while it does.
		Effects.reset();
		Songs.reset();
		Music.reset();
		if (GameSound && ResidentSound)
		{
			const FLockedGameSound Locked = GameSound->Lock();
			if (Locked->FindEffects(ResidentSound->MainEffects.FileId()) == &ResidentSound->MainEffects)
			{
				Locked->UnregisterEffects(ResidentSound->MainEffects);
			}
		}
	}
}
