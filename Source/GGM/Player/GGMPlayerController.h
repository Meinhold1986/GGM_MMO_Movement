// GGMPlayerController.h

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "InputActionValue.h"
#include "GGMPlayerController.generated.h"

class UInputMappingContext;
class UInputAction;
class USpringArmComponent;
class AGGMCharacter;

UCLASS()
class GGM_API AGGMPlayerController : public APlayerController
{
	GENERATED_BODY()

protected:
	virtual void BeginPlay() override;
	virtual void PlayerTick(float DeltaTime) override;
	virtual void SetupInputComponent() override;
	virtual void OnPossess(APawn* InPawn) override;
	virtual void AcknowledgePossession(APawn* P) override;
	virtual void OnRep_Pawn() override;

private:
	void AttackPressed();
	void BlockPressed();
	void BlockReleased();
	void DebugRevivePressed();
	void RequestRespawnPressed();
	void ToggleAttackDebugPressed();

	void MoveForwardAxis(float Value);
	void MoveRightAxis(float Value);

	void SprintPressed();
	void SprintReleased();

	void ForwardHeldPressed();
	void ForwardHeldReleased();

	void ToggleWeaponDrawnPressed();
	void ToggleFlexModePressed();

	UFUNCTION(Server, Reliable)
	void ServerRequestRespawn();

	void ShowDeathScreenDebug();
	void HideDeathScreenDebug();

	void Look(const FInputActionValue& Value);
	void Zoom(const FInputActionValue& Value);
	void JumpPressed(const FInputActionValue& Value);
	void JumpReleased(const FInputActionValue& Value);

	void CacheSpringArmFromPawn(APawn* InPawn);
	void RefreshCameraCacheFromPawn(APawn* InPawn);
	USpringArmComponent* GetCachedSpringArm() const;
	AGGMCharacter* GetGGMCharacterPawn() const;

	void EnterGameOnlyInputMode();
	void EnterGameAndUIInputMode();

	void PushCachedMoveAxesToPawn();
	void PushSprintForwardHeldToPawn();
	void ResetCachedMoveAxes(bool bPushToPawn);

	bool bDeathScreenActive = false;
	bool bForwardHeld = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Zoom", meta = (AllowPrivateAccess = "true"))
	float CameraArmMin = 200.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Zoom", meta = (AllowPrivateAccess = "true"))
	float CameraArmMax = 650.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Zoom", meta = (AllowPrivateAccess = "true"))
	float CameraZoomStep = 60.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Zoom", meta = (AllowPrivateAccess = "true"))
	float CameraZoomInterpSpeed = 12.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Spawn", meta = (AllowPrivateAccess = "true"))
	FName PreferredSpringArmName = TEXT("CameraBoom");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Spawn", meta = (AllowPrivateAccess = "true"))
	float InitialCameraPitch = -30.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Combat", meta = (AllowPrivateAccess = "true"))
	float CombatMoveYawSpeedDegPerSec = 1440.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Combat", meta = (AllowPrivateAccess = "true"))
	float CombatLookPitchMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Combat", meta = (AllowPrivateAccess = "true"))
	float CombatIdleLookMaxYawDeg = 60.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Combat", meta = (AllowPrivateAccess = "true"))
	float CombatIdleLookReturnSpeedDegPerSec = 720.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Combat", meta = (AllowPrivateAccess = "true"))
	float CombatMoveInputGraceTime = 0.08f;

	UPROPERTY(Transient)
	TObjectPtr<USpringArmComponent> CachedSpringArm = nullptr;

	UPROPERTY(Transient)
	float TargetArmLength = -1.f;

	UPROPERTY(Transient)
	float CachedMoveForwardAxis = 0.f;

	UPROPERTY(Transient)
	float CachedMoveRightAxis = 0.f;

	UPROPERTY(Transient)
	float CombatDesiredFacingYaw = 0.f;

	UPROPERTY(Transient)
	float PendingCombatLookYawInput = 0.f;

	UPROPERTY(Transient)
	float CombatIdleLookYawOffset = 0.f;

	UPROPERTY(Transient)
	float CombatMoveInputGraceRemaining = 0.f;

public:
	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputMappingContext> DefaultMappingContext;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> IA_Look;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> IA_Zoom;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> IA_Jump;
};