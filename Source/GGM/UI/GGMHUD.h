#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "GGMHUD.generated.h"

UCLASS()
class GGM_API AGGMHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

private:
	FString BuildHealthPercentText() const;
};