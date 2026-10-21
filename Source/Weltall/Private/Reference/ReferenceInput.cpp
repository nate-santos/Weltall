#include "Reference/ReferenceInput.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/FileHelper.h"

DEFINE_LOG_CATEGORY_STATIC(LogReferenceInput, Log, All);

namespace WeltallReference
{
	bool FRenderReader::Parse(const FString& Params)
	{
		FString Text;
		if (!FParse::Value(*Params, TEXT("Reader="), Text, false)) return true;
		FString BaseText;
		FString PerGlyphText;
		if (!Text.Split(TEXT(","), &BaseText, &PerGlyphText))
		{
			UE_LOG(LogReferenceInput, Error, TEXT("-Reader takes Base,PerGlyph in seconds"));
			return false;
		}
		Base = FCString::Atod(*BaseText);
		PerGlyph = FCString::Atod(*PerGlyphText);
		bReads = true;
		return true;
	}

	/**
	 * A controller button, or a push of the view (TurnLeft, TurnRight, TiltUp, TiltDown, ZoomIn, ZoomOut), by the name the pad
	 * script gives it; a push takes how far the stick is held after a '*' (TurnRight*0.1), all the way without.
	 */
	bool ParseButton(const FString& Pushed, FPadStep& Step)
	{
		FString Name = Pushed;
		FString Part;
		const float Push = Pushed.Split(TEXT("*"), &Name, &Part) ? FMath::Clamp(FCString::Atof(*Part), 0.0f, 1.0f) : 1.0f;
		Weltall::FPadState& Held = Step.Held;
		static const TPair<const TCHAR*, Weltall::EPadButton> Buttons[] = {{TEXT("Up"), Weltall::EPadButton::Up}, {TEXT("Down"), Weltall::EPadButton::Down},
			{TEXT("Left"), Weltall::EPadButton::Left}, {TEXT("Right"), Weltall::EPadButton::Right}, {TEXT("Cross"), Weltall::EPadButton::Cross},
			{TEXT("Circle"), Weltall::EPadButton::Circle}, {TEXT("Square"), Weltall::EPadButton::Square}, {TEXT("Triangle"), Weltall::EPadButton::Triangle},
			{TEXT("L1"), Weltall::EPadButton::L1}, {TEXT("R1"), Weltall::EPadButton::R1}, {TEXT("L2"), Weltall::EPadButton::L2},
			{TEXT("R2"), Weltall::EPadButton::R2}, {TEXT("Start"), Weltall::EPadButton::Start}, {TEXT("Select"), Weltall::EPadButton::Select}};
		for (const TPair<const TCHAR*, Weltall::EPadButton>& Button : Buttons)
		{
			if (Name == Button.Key)
			{
				Held.Buttons |= Weltall::FPadState::Bit(Button.Value);
				return true;
			}
		}
		if (Name == TEXT("TurnLeft") || Name == TEXT("TurnRight"))
		{
			Step.Turn = Name == TEXT("TurnRight") ? Push : -Push;
			return true;
		}
		if (Name == TEXT("TiltUp") || Name == TEXT("TiltDown"))
		{
			Step.Tilt = Name == TEXT("TiltUp") ? Push : -Push;
			return true;
		}
		if (Name == TEXT("ZoomIn") || Name == TEXT("ZoomOut"))
		{
			Step.Zoom = Name == TEXT("ZoomIn") ? Push : -Push;
			return true;
		}
		UE_LOG(LogReferenceInput, Error, TEXT("Unknown button '%s' in -Pad"), *Name);
		return false;
	}

	/**
	 * -Pad=a,b,... or -PadFile=Path: the buttons held through each step of play (-PadStep=S seconds, 1 by default),
	 * each step's buttons joined by '+' and empty for none; nothing is held after the last step.
	 * A file contains the same script, letting long references retain their timing without filling the command line.
	 */
	bool ParsePadScript(const FString& Params, TArray<FPadStep>& Script, double& Step)
	{
		Step = 1.0;
		FParse::Value(*Params, TEXT("PadStep="), Step);
		FString Text;
		const bool bInline = FParse::Value(*Params, TEXT("Pad="), Text, false);
		FString File;
		const bool bFile = FParse::Value(*Params, TEXT("PadFile="), File, false);
		if (bInline && bFile)
		{
			UE_LOG(LogReferenceInput, Error, TEXT("Use either -Pad or -PadFile"));
			return false;
		}
		if (bFile && !FFileHelper::LoadFileToString(Text, *File))
		{
			UE_LOG(LogReferenceInput, Error, TEXT("Cannot read the controller script %s"), *File);
			return false;
		}
		if (!bInline && !bFile)
		{
			return true;
		}
		if (!FMath::IsFinite(Step) || !(Step > 0.0))
		{
			UE_LOG(LogReferenceInput, Error, TEXT("-PadStep must be finite and positive"));
			return false;
		}
		TArray<FString> Steps;
		Text.ParseIntoArray(Steps, TEXT(","), false);
		for (const FString& Buttons : Steps)
		{
			FPadStep& Pushed = Script.AddDefaulted_GetRef();
			TArray<FString> Names;
			Buttons.ParseIntoArray(Names, TEXT("+"));
			for (const FString& Name : Names)
			{
				if (!ParseButton(Name.TrimStartAndEnd(), Pushed))
				{
					return false;
				}
			}
		}
		return true;
	}

}

#endif
