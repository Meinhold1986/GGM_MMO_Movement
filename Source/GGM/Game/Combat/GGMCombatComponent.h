// Game/Combat/GGMCombatComponent.h

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Sound/SoundBase.h"
#include "Animation/AnimMontage.h"
#include "GGMWeaponDataAsset.h"
#include "GGMCombatComponent.generated.h"

class AGGMCharacter;
class UGGMCombatDataAsset;
class UGGMWeaponDataAsset;

UENUM(BlueprintType)
enum class EGGMCombatState : uint8
{
	Unarmed = 0,
	WeaponIdle = 1,
	Attacking = 2,
	Blocking = 3
};

USTRUCT()
struct FGGMCombatTuningCache
{
	GENERATED_BODY()

	UPROPERTY()
	float AttackDuration = 0.75f;

	UPROPERTY()
	float BlockLockAfterAttack = 0.35f;

	UPROPERTY()
	float AttackRange = 200.f;

	UPROPERTY()
	float AttackConeHalfAngleDeg = 30.f;

	UPROPERTY()
	float AttackHitRadius = 120.f;

	UPROPERTY()
	float AttackDamage = 20.f;

	UPROPERTY()
	float AttackHitDelay = 0.22f;

	UPROPERTY()
	float BlockMoveSpeed = 180.f;

	UPROPERTY()
	TObjectPtr<USoundBase> HitFleshSound = nullptr;

	UPROPERTY()
	TObjectPtr<USoundBase> HitBlockSound = nullptr;

	UPROPERTY()
	TObjectPtr<USoundBase> MissSound = nullptr;

	UPROPERTY()
	float AttackLockOnBlockedHit = 0.f;

	UPROPERTY()
	float BlockLockOnBlockedHit = 0.f;
};

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class GGM_API UGGMCombatComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UGGMCombatComponent();

protected:
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

public:
	UFUNCTION(BlueprintPure)
	bool IsWeaponDrawn() const;

	UFUNCTION(BlueprintPure)
	bool IsBlocking() const;

	UFUNCTION(BlueprintPure)
	bool IsAttacking() const;

	UFUNCTION(BlueprintPure)
	EGGMCombatState GetCombatState() const;

	UFUNCTION(BlueprintPure)
	UGGMWeaponDataAsset* GetEquippedWeaponData() const;

	UFUNCTION(BlueprintPure)
	float GetBlockMoveSpeed() const;

	UFUNCTION(BlueprintPure)
	float GetBlockLockAfterAttack() const;

	UFUNCTION(BlueprintPure)
	float GetAttackLockOnBlockedHit() const;

	UFUNCTION(BlueprintPure)
	float GetBlockLockOnBlockedHit() const;

	void StartAttack(EGGMAttackType AttackType = EGGMAttackType::Light);
	void ApplyWeaponDrawnState(bool bNewWeaponDrawn);
	void BeginBlockInput();
	void EndBlockInput();
	void ResetTransientCombatState();

protected:
	void InitializeCombatTuningCache();

	const FGGMAttackDefinition* GetResolvedAttackDefinition(EGGMAttackType AttackType) const;
	UAnimMontage* GetResolvedAttackMontage() const;
	float GetResolvedAttackDamage() const;
	float GetResolvedAttackStaminaCost(EGGMAttackType AttackType) const;
	float GetResolvedAttackHitDelay(EGGMAttackType AttackType) const;
	float GetResolvedAttackDuration(EGGMAttackType AttackType) const;
	float GetResolvedAttackRange(EGGMAttackType AttackType) const;
	float GetResolvedAttackHitRadius(EGGMAttackType AttackType) const;
	float GetResolvedAttackConeHalfAngleDeg(EGGMAttackType AttackType) const;
	UAnimMontage* GetResolvedBlockMontage() const;

	void SetCombatState(EGGMCombatState NewState);
	void SetAttacking(bool bNewAttacking);
	void SetBlocking(bool bNewBlocking);

	bool CanStartAttackAuthoritative() const;
	bool CanStartBlockAuthoritative() const;
	bool HasEnoughStaminaForAttack(EGGMAttackType AttackType) const;
	void SpendAttackStaminaAuthoritative(EGGMAttackType AttackType);

	UFUNCTION(Server, Reliable)
	void ServerStartAttack(EGGMAttackType AttackType);

	void StartAttackAuthoritative(EGGMAttackType AttackType);
	void CommitAttackHitAuthoritative();
	void FinishAttackAuthoritative();
	void PerformAttackHitTest();
	void HandleMeleeHitServer(AGGMCharacter* HitTarget);
	bool ApplyDamageToTargetAuthoritative(AGGMCharacter* HitTarget, float DamageAmount);

	void CancelAttackFlow();

	UFUNCTION(Server, Reliable)
	void ServerSetBlocking(bool bNewBlocking);

	void StartBlockLockAfterAttack();
	void UnlockBlockAfterAttack();

	void ApplyBlockedHitPunishToOwner();
	void UnlockAttackAfterBlockedHit();
	void UnlockBlockAfterBlockedHit();
	void TryReapplyHeldBlockAuthoritative();

	UFUNCTION(Client, Reliable)
	void ClientConfirmAttack();

	UFUNCTION(NetMulticast, Reliable)
	void MulticastPlayAttackMontage();

	UFUNCTION(NetMulticast, Unreliable)
	void MulticastPlayHitSound(bool bBlocked, FVector SoundLocation);

	UFUNCTION(NetMulticast, Unreliable)
	void MulticastPlayMissSound(FVector SoundLocation);

	UFUNCTION()
	void OnRep_CombatState();

	UFUNCTION()
	void OnRep_AttackMontageRepCounter();

	void NotifyOwnerWeaponDrawnChanged();
	void PlayAttackMontageLocal();
	void PlayBlockMontageLocal();
	void StopBlockMontageLocal();

protected:
	UPROPERTY()
	TObjectPtr<AGGMCharacter> OwnerCharacter = nullptr;

	UPROPERTY(EditDefaultsOnly, Category = "Combat")
	TObjectPtr<UGGMCombatDataAsset> CombatData = nullptr;

	UPROPERTY(EditDefaultsOnly, Category = "Combat")
	TObjectPtr<UGGMWeaponDataAsset> EquippedWeaponData = nullptr;

	UPROPERTY()
	FGGMCombatTuningCache CachedTuning;

	UPROPERTY(ReplicatedUsing = OnRep_CombatState, VisibleAnywhere, Category = "Combat")
	EGGMCombatState CombatState = EGGMCombatState::Unarmed;

	UPROPERTY(ReplicatedUsing = OnRep_AttackMontageRepCounter)
	uint8 AttackMontageRepCounter = 0;

	UPROPERTY()
	EGGMAttackType CurrentAttackType = EGGMAttackType::Light;

	UPROPERTY()
	bool bAttackHitCommitted = false;

	UPROPERTY()
	bool bWantsBlockHeld = false;

	UPROPERTY()
	bool bBlockLockedFromAttack = false;

	UPROPERTY()
	bool bAttackLockedFromBlockedHit = false;

	UPROPERTY()
	bool bBlockLockedFromBlockedHit = false;

	UPROPERTY()
	FTimerHandle AttackTimerHandle;

	UPROPERTY()
	FTimerHandle AttackHitTimerHandle;

	UPROPERTY()
	FTimerHandle BlockLockTimerHandle;

	UPROPERTY()
	FTimerHandle AttackBlockedHitLockTimerHandle;

	UPROPERTY()
	FTimerHandle BlockBlockedHitLockTimerHandle;
};