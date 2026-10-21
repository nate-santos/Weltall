#pragma once

#include "Battle/Show/BattleShowSeams.h"
#include "Support/OracleDigest.h"

#include <set>
#include <string>
#include <vector>

namespace Weltall::Testing
{
	/**
	 * The sound driver as the battle show oracles' stubs record it: each call an event of its name and values, the effect files
	 * registered in a set; no bank is ever held.
	 */
	class FBattleSoundRecorder final : public IBattleShowSound
	{
	public:
		struct FEvent
		{
			std::string Name;
			std::vector<uint32> Values;
		};
		std::vector<FEvent> Events;
		std::set<uint16> Registered;
		static constexpr int32 VoiceFrames = 30;
		int32 VoiceLeft = VoiceFrames;

		void PlayEffect(uint32 Sound) override { Events.push_back({"play", {Sound}}); }
		void PlaySystemEffect(uint32 Sound) override { Events.push_back({"system", {Sound}}); }
		void StopEffect(uint32 Sound) override { Events.push_back({"stop", {Sound}}); }
		void PlayEffectOn(uint32 Sound, int32 Voice) override { Events.push_back({"on", {Sound, static_cast<uint32>(Voice)}}); }
		void PlayEffectAt(uint32 Sound, int32 Volume, int32 Pan) override
		{
			Events.push_back({"play at", {Sound, static_cast<uint32>(Volume), static_cast<uint32>(Pan)}});
		}
		/** The only effect asked after is a battle event's voice, which the harness has playing for VoiceFrames frames. */
		bool IsEffectPlaying(uint32) override
		{
			if (VoiceLeft > 0)
			{
				--VoiceLeft;
				return true;
			}
			VoiceLeft = VoiceFrames;
			return false;
		}
		void SetEffectVolume(uint32 Sound, int32 Volume) override { Events.push_back({"effect volume", {Sound, static_cast<uint32>(Volume)}}); }
		void FadeEffectVolume(uint32 Sound, int32 Volume, int32 Steps) override
		{
			Events.push_back({"effect fade", {Sound, static_cast<uint32>(Volume), static_cast<uint32>(Steps)}});
		}
		void SetMusicVolume(int32 Volume, int32 Steps) override { Events.push_back({"volume", {static_cast<uint32>(Volume), static_cast<uint32>(Steps)}}); }
		void RegisterEffects(std::span<const uint8> Seds) override;
		void UnregisterEffects(std::span<const uint8> Seds) override;
		void StopAllEffects() override { Events.push_back({"stop all", {}}); }
		void StopEffects(std::span<const uint8> Seds) override;
		bool HasEffects(uint16 Id) override { return Registered.contains(Id); }
		void UnloadGearBattleBank() override {}
		void UnloadBattleBank() override {}
		void LoadBattleBank(std::span<const uint8>) override {}
		void LoadBattleVoiceBank(std::span<const uint8>) override {}
		void LoadGearBattleBank(std::span<const uint8>) override {}
		bool HasBank(uint16) override { return false; }
		void PauseSound() override { Events.push_back({"pause", {}}); }
		void ResumeSound() override { Events.push_back({"resume", {}}); }

		/** The events since the last digest, each as 0xEE, its value count + 1, its name and its values; then forgets them. */
		void Digest(FOracleDigest& One);
	};
}
