// Source/GGM/Game/Combat/GGMWeaponDataAsset.h

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GGMWeaponDataAsset.generated.h"

class UAnimMontage;
class USoundBase;
class UGGMCombatDataAsset;

UENUM(BlueprintType)
enum class EGGMAttackType : uint8
{
	Light = 0,
	Heavy = 1
};

USTRUCT(BlueprintType)
struct FGGMAttackDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack")
	EGGMAttackType AttackType = EGGMAttackType::Light;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack")
	UAnimMontage* Montage = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack")
	float DamageMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack")
	float StaminaCost = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack")
	float HitDelayOverride = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack")
	float DurationOverride = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack")
	float RangeOverride = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack")
	float HitRadiusOverride = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack")
	float ConeHalfAngleOverride = 0.f;
};

UCLASS(BlueprintType)
class GGM_API UGGMWeaponDataAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UGGMWeaponDataAsset();

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon")
	FName WeaponId;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon")
	UGGMCombatDataAsset* CombatTuning;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Attacks")
	FGGMAttackDefinition LightAttack;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Attacks")
	FGGMAttackDefinition HeavyAttack;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation")
	UAnimMontage* BlockMontage;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sound")
	USoundBase* HitFleshSound;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sound")
	USoundBase* HitBlockSound;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sound")
	USoundBase* MissSound;
};