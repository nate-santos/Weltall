#include "Battle/Show/BattleShowScreen.h"

#include <algorithm>

namespace Weltall
{
	namespace
	{
		/**
		 * How far a moving value still lies from its target after a step: the whole difference scaled by half the
		 * steps left through the GTE (its IR registers saturating to 16 bits), over half the steps.
		 */
		int32 Remainder(int32 Difference, int32 Remaining, int32 Total)
		{
			const int32 Scaled = std::clamp(Difference * (Remaining >> 1), -0x8000, 0x7FFF);
			return Scaled / (Total >> 1);
		}
	}

	void FBattleFade::Start(int32 Steps, int32 BlendMode, uint8 Red, uint8 Green, uint8 Blue, bool bEffect)
	{
		if (bBlocked)
		{
			return;
		}
		bFromEffect = bEffect;
		From = bOn ? Shown : std::array<uint8, 3>{};
		bOn = true;
		Mode = static_cast<uint8>(BlendMode);
		Target = {Red, Green, Blue};
		Total = Steps << 1;
		Remaining = Steps << 1;
		Step();
	}

	void FBattleFade::Step()
	{
		if (Remaining == 0)
		{
			Shown = Target;
			if (Target[0] == 0 && Target[1] == 0 && Target[2] == 0)
			{
				bOn = false;
			}
			return;
		}
		--Remaining;
		for (std::size_t Channel = 0; Channel < 3; ++Channel)
		{
			Shown[Channel] = static_cast<uint8>(Target[Channel] - Remainder(Target[Channel] - From[Channel], Remaining, Total));
		}
	}

	void FBattleShake::Start(const FGteVector& NewTarget, int32 Steps)
	{
		// A new shake starts from rest; one under way from where its amplitude is.
		From = bOn ? Amplitude : FGteVector{};
		if (!bOn)
		{
			Count = 0;
		}
		bOn = true;
		Target = NewTarget;
		Total = Steps << 1;
		Remaining = Steps << 1;
		Step();
	}

	void FBattleShake::Step()
	{
		if (Remaining == 0)
		{
			Amplitude = Target;
		}
		else
		{
			--Remaining;
			for (std::size_t Axis = 0; Axis < 3; ++Axis)
			{
				Amplitude[Axis] = static_cast<int16>(Target[Axis] - Remainder(Target[Axis] - From[Axis], Remaining, Total));
			}
		}
		++Count;
		for (std::size_t Axis = 0; Axis < 3; ++Axis)
		{
			Offset[Axis] = static_cast<int16>((Count & 2) != 0 ? -Amplitude[Axis] : Amplitude[Axis]);
		}
		if (Amplitude[0] == 0 && Amplitude[1] == 0 && Amplitude[2] == 0 && Remaining == 0)
		{
			bOn = false;
		}
	}

	void FBattleTintLayers::Set(int32 Layer, int32 Mode, int32 Level, int32 Red, int32 Green, int32 Blue)
	{
		if (Layer < 0 || Layer >= LayerCount)
		{
			return;
		}
		FLayer& Target = Layers[static_cast<std::size_t>(Layer)];
		if (Mode < 0)
		{
			Target.bOn = 0;
			return;
		}
		bChanged = true;
		Target.bOn = 1;
		Target.Mode = static_cast<uint8>(Mode);
		Target.Level = static_cast<uint8>(Level);
		Target.Color = {static_cast<int8>(Red), static_cast<int8>(Green), static_cast<int8>(Blue)};
	}

	void FBattleTintLayers::SwitchOff()
	{
		for (FLayer& Layer : Layers)
		{
			Layer.bOn = 0;
		}
	}

	std::array<uint8, 3> FBattleTintLayers::Tint(int32 Layer, std::array<uint8, 3>& Color) const
	{
		const FLayer& Source = Layers[static_cast<std::size_t>(Layer)];
		std::array<int32, 3> Base{Source.Color[0] * 8, Source.Color[1] * 8, Source.Color[2] * 8};
		if (Source.bOn != 0)
		{
			switch (Source.Mode)
			{
			case 0:
				for (std::size_t Channel = 0; Channel < 3; ++Channel)
				{
					Base[Channel] += Color[Channel];
				}
				break;
			case 1:
				for (std::size_t Channel = 0; Channel < 3; ++Channel)
				{
					Base[Channel] += Color[Channel] >> 1;
				}
				break;
			case 2:
				for (std::size_t Channel = 0; Channel < 3; ++Channel)
				{
					Base[Channel] += Color[Channel] >> 2;
				}
				break;
			case 3:
			{
				const int32 Grey = (Color[0] + Color[1] + Color[2]) / 3;
				for (int32& Channel : Base)
				{
					Channel += Grey;
				}
				break;
			}
			case 4:
				Base = {};
				break;
			default:
				break;
			}
		}
		for (std::size_t Channel = 0; Channel < 3; ++Channel)
		{
			const int32 Value = Base[Channel] + (Color[Channel] - Base[Channel]) * Source.Level / 32;
			Color[Channel] = static_cast<uint8>(std::clamp(Value, 0, 255));
		}
		return Color;
	}

	bool FBattleTintFade::Start(FBattleTintLayers& Layers, int16 NewTarget, int32 Steps, int16 NewMode, int16 Red, int16 Green, int16 Blue)
	{
		const bool bNew = !bOn;
		if (bNew)
		{
			Layers.SwitchOff();
			From = 0;
			Drawn = 0;
			Level = 0;
		}
		else
		{
			From = Level;
		}
		bOn = true;
		Target = NewTarget;
		Total = Steps << 1;
		Remaining = Steps << 1;
		Color = {Red, Green, Blue};
		Mode = NewMode;
		Step();
		return bNew;
	}

	bool FBattleTintFade::Step()
	{
		if (Remaining == 0)
		{
			if (Level == 0)
			{
				bOn = false;
			}
			return bOn;
		}
		--Remaining;
		const int32 Left = (Target - From) * ((Remaining * 32) / Total);
		Level = static_cast<int16>(Target - Left / 32);
		return true;
	}

	void FBattleTintFade::Draw(FBattleTintLayers& Layers)
	{
		if (Drawn != Level)
		{
			Drawn = Level;
			Layers.Set(0, Mode, 32 - Level, Color[0], Color[1], Color[2]);
		}
	}
}
