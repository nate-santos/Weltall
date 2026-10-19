#pragma once

#include "Commandlets/Commandlet.h"
#include "CoreMinimal.h"

#include "ExportTextCommandlet.generated.h"

/**
 * Runs the Assets menu's text export (FGameTextExport) on the first disc in Games into an assets folder's text and font
 * folders, headless, and logs how many files it took and how long.
 *
 *   UnrealEditor-Cmd Weltall.uproject -run=ExportText -Out=<assets folder> -unattended -nullrhi
 */
UCLASS()
class UExportTextCommandlet final : public UCommandlet
{
	GENERATED_BODY()

public:
	UExportTextCommandlet();

	virtual int32 Main(const FString& Params) override;
};
