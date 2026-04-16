#include "GGMGameMode.h"

#include "../Player/GGMCharacter.h"
#include "../Player/GGMPlayerController.h"
#include "GameFramework/Pawn.h"

AGGMGameMode::AGGMGameMode()
{
	DefaultPawnClass = AGGMCharacter::StaticClass();
	PlayerControllerClass = AGGMPlayerController::StaticClass();
}

void AGGMGameMode::RespawnPlayerFromDeath(AGGMPlayerController* PlayerController)
{
	if (!HasAuthority())
	{
		return;
	}

	if (!PlayerController)
	{
		return;
	}

	APawn* OldPawn = PlayerController->GetPawn();
	if (OldPawn)
	{
		OldPawn->Destroy();
	}

	RestartPlayer(PlayerController);
}