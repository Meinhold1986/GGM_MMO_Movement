#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GGMGameMode.generated.h"

class AGGMPlayerController;

UCLASS()
class GGM_API AGGMGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AGGMGameMode();

	void RespawnPlayerFromDeath(AGGMPlayerController* PlayerController);
};