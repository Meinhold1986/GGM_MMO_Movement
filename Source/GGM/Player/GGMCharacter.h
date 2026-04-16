#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Player/GGMMovementTypes.h"
#include "Player/GGMCharacterMovementComponent.h"
#include "GGMCharacter.generated.h"

class UStaticMeshComponent;
class UGGMCombatComponent;

UCLASS()
class GGM_API AGGMCharacter : public ACharacter
{
	GENERATED_BODY()

	friend class UGGMCombatComponent;
	friend class UGGMCharacterMovementComponent;

public:
	AGGMCharacter(const FObjectInitializer& ObjectInitializer);

protected:
	virtual void BeginPlay() override;
	virtual bool CanJumpInternal_Implementation() const override;

public:
	virtual void Tick(float DeltaTime) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UGGMCharacterMovementComponent* GetGGMCharacterMovementComponent() const
	{
		return Cast<UGGMCharacterMovementComponent>(GetCharacterMovement());
	}

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Components")
	UGGMCombatComponent* GetCombatComponent() const
	{
		return CombatComponent;
	}

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Movement")
	bool IsFlexModeEnabled() const
	{
		return bFlexMode;
	}

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Movement")
	bool IsDeadStateActive() const
	{
		return bIsDead;
	}

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Movement")
	const FGGM_MoveSpeedStats& GetMoveSpeedStats() const
	{
		return MoveSpeedStats;
	}

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Movement")
	float GetMoveForwardIntent() const
	{
		if (const UGGMCharacterMovementComponent* MoveComp = GetGGMCharacterMovementComponent())
		{
			return MoveComp->GetMoveForwardIntent();
		}

		return 0.f;
	}

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Movement")
	float GetMoveRightIntent() const
	{
		if (const UGGMCharacterMovementComponent* MoveComp = GetGGMCharacterMovementComponent())
		{
			return MoveComp->GetMoveRightIntent();
		}

		return 0.f;
	}

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Movement|Animation")
	float GetBlendForwardAxis() const
	{
		if (const UGGMCharacterMovementComponent* MoveComp = GetGGMCharacterMovementComponent())
		{
			return MoveComp->GetBlendForwardAxis();
		}

		return 0.f;
	}

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Movement|Animation")
	float GetBlendRightAxis() const
	{
		if (const UGGMCharacterMovementComponent* MoveComp = GetGGMCharacterMovementComponent())
		{
			return MoveComp->GetBlendRightAxis();
		}

		return 0.f;
	}

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Movement|Animation")
	float GetAnimForwardAxis() const
	{
		return GetBlendForwardAxis();
	}

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Movement|Animation")
	float GetAnimRightAxis() const
	{
		return GetBlendRightAxis();
	}

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Movement|Animation")
	bool IsSprintingForAnimation() const
	{
		if (const UGGMCharacterMovementComponent* MoveComp = GetGGMCharacterMovementComponent())
		{
			return MoveComp->IsSprinting();
		}

		return false;
	}

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Movement|Animation")
	float GetVisualYaw() const
	{
		if (const UGGMCharacterMovementComponent* MoveComp = GetGGMCharacterMovementComponent())
		{
			return MoveComp->GetVisualYaw();
		}

		return FRotator::NormalizeAxis(GetActorRotation().Yaw);
	}

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Movement")
	float GetCurrentMoveSpeed() const
	{
		if (const UGGMCharacterMovementComponent* MoveComp = GetGGMCharacterMovementComponent())
		{
			return MoveComp->GetMoveSpeed();
		}

		return 0.f;
	}

	UFUNCTION(BlueprintCallable, Category = "Movement")
	void SubmitRawMoveAxes(float ForwardValue, float RightValue)
	{
		if (UGGMCharacterMovementComponent* MoveComp = GetGGMCharacterMovementComponent())
		{
			MoveComp->SetRawMoveInput(
				FMath::Clamp(ForwardValue, -1.f, 1.f),
				FMath::Clamp(RightValue, -1.f, 1.f));
		}
	}

	UFUNCTION(BlueprintCallable, Category = "Movement")
	void SubmitDesiredFacingYaw(float Yaw)
	{
		if (UGGMCharacterMovementComponent* MoveComp = GetGGMCharacterMovementComponent())
		{
			MoveComp->SetDesiredFacingYaw(Yaw);
		}
	}

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	UGGMCombatComponent* CombatComponent = nullptr;

	UPROPERTY(ReplicatedUsing = OnRep_FlexMode, BlueprintReadOnly, Category = "Movement", meta = (AllowPrivateAccess = "true"))
	bool bFlexMode = false;

	UFUNCTION()
	void OnRep_FlexMode()
	{
	}

	UFUNCTION(Server, Reliable)
	void ServerSetFlexMode(bool bNewFlexMode);

	UFUNCTION(Server, Reliable)
	void ServerSetWeaponDrawn(bool bNewWeaponDrawn);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Movement")
	EGGM_LocomotionMode GetCurrentLocomotionMode() const
	{
		if (bFlexMode)
		{
			return EGGM_LocomotionMode::Flex;
		}

		if (IsWeaponDrawn())
		{
			return EGGM_LocomotionMode::Combat;
		}

		return EGGM_LocomotionMode::Travel;
	}

	void ToggleFlexMode()
	{
		RequestSetFlexMode(!bFlexMode);
	}

	void ToggleWeaponDrawn()
	{
		RequestSetWeaponDrawn(!IsWeaponDrawn());
	}

	void RequestSetFlexMode(bool bNewFlexMode);
	void RequestSetWeaponDrawn(bool bNewWeaponDrawn);

	UFUNCTION(BlueprintCallable, BlueprintPure)
	bool IsAlive() const
	{
		return !bIsDead && CurrentHealth > 0.f;
	}

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Combat")
	bool IsWeaponDrawn() const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Combat")
	bool IsBlocking() const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Combat")
	bool IsAttacking() const;

	void RefreshWeaponDrawnPresentationAndMovementState()
	{
		ApplyWeaponDrawnVisuals();
	}

	UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing = OnRep_MoveSpeedStats, Category = "Stats", meta = (AllowPrivateAccess = "true"))
	FGGM_MoveSpeedStats MoveSpeedStats;

	UFUNCTION()
	void OnRep_MoveSpeedStats()
	{
	}

	void DebugRevive_Pressed();
	void DebugToggleAll_Pressed();

	UFUNCTION(BlueprintCallable, Category = "Combat")
	void StartAttack();

	void StartBlock();
	void StopBlock();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Weapon", meta = (AllowPrivateAccess = "true"))
	UStaticMeshComponent* WeaponMesh = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Shield", meta = (AllowPrivateAccess = "true"))
	UStaticMeshComponent* ShieldMesh = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats", meta = (AllowPrivateAccess = "true"))
	float MaxHealth = 100.f;

	UPROPERTY(ReplicatedUsing = OnRep_CurrentHealth, BlueprintReadOnly, Category = "Stats", meta = (AllowPrivateAccess = "true"))
	float CurrentHealth = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats", meta = (AllowPrivateAccess = "true"))
	float MaxStamina = 100.f;

	UPROPERTY(ReplicatedUsing = OnRep_CurrentStamina, BlueprintReadOnly, Category = "Stats", meta = (AllowPrivateAccess = "true"))
	float CurrentStamina = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats", meta = (AllowPrivateAccess = "true"))
	float StaminaRegenPerSecond = 3.f;

	UPROPERTY(ReplicatedUsing = OnRep_IsDead, BlueprintReadOnly, Category = "Stats", meta = (AllowPrivateAccess = "true"))
	bool bIsDead = false;

	UFUNCTION()
	void OnRep_CurrentHealth();

	UFUNCTION()
	void OnRep_CurrentStamina();

	UFUNCTION()
	void OnRep_IsDead();

	void ReviveToFullServer();

	void ServerAuthSetRemoteLocomotionSnapshot(const FGGMRemoteLocomotionSnapshot& NewSnapshot);

private:
	void ApplyWeaponDrawnVisuals();
	void RefreshDeathPresentationAndMovementState(bool bNewDeadState);
	void ApplyLocalControllerInputLock(bool bLock);

	void ApplyRequestedWeaponDrawnStateChange(bool bNewWeaponDrawn, bool bAuthoritative);
	void ApplyRequestedFlexModeStateChange(bool bNewFlexMode, bool bAuthoritative);
	void ApplyOwnedFlexModeStateChange(bool bNewFlexMode, bool bAuthoritative);

	void ShowDebugScreenMessage(int32 Key, float TimeToDisplay, const FColor& Color, const FString& Message) const;
	void ShowDebugEnabledScreenMessage(int32 Key, float TimeToDisplay, const FColor& Color, const FString& Message) const;
	void ShowDebugAllTickOverlay() const;
	void ApplyRemoteLocomotionSnapshotToMovementComponent();

	UFUNCTION()
	void OnRep_RemoteLocomotionSnapshot();

	UFUNCTION(Server, Reliable)
	void ServerDebugRevive();

	UFUNCTION(Server, Reliable)
	void ServerSetDebugAll(bool bNewEnabled);

	void ApplyDebugAll(bool bNewEnabled);

	UPROPERTY(Transient)
	bool bDebugAll = false;

	UPROPERTY(ReplicatedUsing = OnRep_RemoteLocomotionSnapshot)
	FGGMRemoteLocomotionSnapshot RemoteLocomotionSnapshot;
};