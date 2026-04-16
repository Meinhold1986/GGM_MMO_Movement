#pragma once

#include "CoreMinimal.h"
#include "GGMMovementTypes.generated.h"

UENUM(BlueprintType)
enum class EGGM_LocomotionMode : uint8
{
	Travel = 0,
	Combat = 1,
	Flex = 2
};

UENUM(BlueprintType)
enum class EGGM_LocomotionGroup : uint8
{
	Idle = 0,
	Forward = 1,
	Strafe = 2,
	Backward = 3
};

USTRUCT(BlueprintType)
struct FGGM_MoveSpeedStats
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
	float Forward = 600.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
	float StrafeBackward = 300.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
	float Sprint = 800.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
	float CombatForwardMultiplier = 0.85f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
	float CombatStrafeBackwardMultiplier = 0.85f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
	float Flex = 110.f;
};