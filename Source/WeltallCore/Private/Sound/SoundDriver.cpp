#include "Sound/SoundDriver.h"

#include "Sound/SoundDriverState.h"

namespace Weltall
{
	FSoundDriver::FState::FState(ISpuBus& InSpu, const FSoundDriverTables& InTables, const FSoundDriverOptions& InOptions)
		: Spu(InSpu), Tables(InTables), Options(InOptions), Library(InSpu, Tables)
	{
	}

	FSoundDriver::FSoundDriver(ISpuBus& Spu, const FSoundDriverTables& Tables, const FSoundDriverOptions& Options)
		: State(std::make_unique<FState>(Spu, Tables, Options))
	{
		State->Start();
	}

	FSoundDriver::~FSoundDriver() = default;

	void FSoundDriver::Interrupt()
	{
		State->RunInterrupt();
		State->RunTransfers();
	}

	const FSoundBank* FSoundDriver::LoadBank(const FSoundBankFile& File, int32 SpuAddress)
	{
		return State->LoadBankSlot(File, SpuAddress, false);
	}

	const FSoundBank* FSoundDriver::LoadVoiceBank(const FSoundBankFile& File, int32 SpuAddress)
	{
		return State->LoadBankSlot(File, SpuAddress, true);
	}

	const FSoundBank* FSoundDriver::LoadNewBank(const FSoundBankFile& File, int32 SpuAddress)
	{
		return State->FindBank(File.Id()) != nullptr ? nullptr : State->LoadBankSlot(File, SpuAddress, false);
	}

	void FSoundDriver::UnloadBank(const FSoundBank& Bank)
	{
		FState& S = *State;
		for (int32 Index = 0; Index < S.BankCount; ++Index)
		{
			FSoundBank* Loaded = S.Banks[static_cast<std::size_t>(Index)];
			if (Loaded == &Bank)
			{
				for (int32 Next = Index; Next + 1 < S.BankCount; ++Next)
				{
					S.Banks[static_cast<std::size_t>(Next)] = S.Banks[static_cast<std::size_t>(Next + 1)];
				}
				--S.BankCount;
				S.Ram.Free(Loaded->Address);
				Loaded->bLoaded = false;
				return;
			}
		}
	}

	const FSoundBank* FSoundDriver::FindBank(int32 Id) const
	{
		return State->FindBank(Id);
	}

	uint32 FSoundDriver::BankAddress(const FSoundBank& Bank)
	{
		return Bank.Address;
	}

	ESoundKind FSoundDriver::VoiceKind(int32 Voice) const
	{
		return Voice >= 0 && Voice < VoiceCount ? State->VoiceKinds[static_cast<std::size_t>(Voice)] : ESoundKind::Effect;
	}

	bool FSoundDriver::RegisterEffects(const FSoundEffectFile& File)
	{
		FState& S = *State;
		if ((!(S.Flags & DriverFlag::AllowDuplicateEffects) && S.EffectFile(File.FileId()) != nullptr) || S.EffectFileCount == MaxEffectFiles)
		{
			return false;
		}
		S.EffectFiles[static_cast<std::size_t>(S.EffectFileCount++)] = &File;
		return true;
	}

	void FSoundDriver::UnregisterEffects(const FSoundEffectFile& File)
	{
		FState& S = *State;
		for (int32 Index = 0; Index < S.EffectFileCount; ++Index)
		{
			if (S.EffectFiles[static_cast<std::size_t>(Index)] == &File)
			{
				StopEffectsOfFile(File);
				for (int32 Next = Index; Next + 1 < S.EffectFileCount; ++Next)
				{
					S.EffectFiles[static_cast<std::size_t>(Next)] = S.EffectFiles[static_cast<std::size_t>(Next + 1)];
				}
				--S.EffectFileCount;
				// A music track that went on into the file's commands would read freed memory in the
				// original; here it ends.
				S.ForgetCommands(File.Bytes());
				return;
			}
		}
	}

	const FSoundEffectFile* FSoundDriver::FindEffects(uint16 FileId) const
	{
		return State->EffectFile(FileId);
	}
}
