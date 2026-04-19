// Source/GGM/Game/Combat/GGMCombatDataAsset.h

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Sound/SoundBase.h"
#include "GGMCombatDataAsset.generated.h"

UCLASS(BlueprintType)
class GGM_API UGGMCombatDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
	float AttackDuration = 0.75f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
	float BlockLockAfterAttack = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
	float AttackRange = 200.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
	float AttackConeHalfAngleDeg = 30.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
	float AttackHitRadius = 120.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
	float AttackDamage = 20.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
	float AttackHitDelay = 0.22f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat|Movement")
	float BlockMoveSpeed = 180.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat|Audio")
	TObjectPtr<USoundBase> HitFleshSound = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat|Audio")
	TObjectPtr<USoundBase> HitBlockSound = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat|Audio")
	TObjectPtr<USoundBase> MissSound = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat|Punish")
	float AttackLockOnBlockedHit = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat|Punish")
	float BlockLockOnBlockedHit = 1.0f;
};