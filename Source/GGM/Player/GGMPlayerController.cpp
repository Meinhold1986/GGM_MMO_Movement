// GGMPlayerController.cpp

#include "GGMPlayerController.h"
#include "GGMCharacter.h"
#include "Player/GGMCharacterMovementComponent.h"

#include "../Core/GGMGameMode.h"

#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputMappingContext.h"

#include "Engine/LocalPlayer.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "InputCoreTypes.h"
#include "Engine/Engine.h"

DEFINE_LOG_CATEGORY_STATIC(LogGGMInputDebug, Log, All);

void AGGMPlayerController::BeginPlay()
{
	Super::BeginPlay();

	bShowMouseCursor = false;
	bEnableClickEvents = false;
	bEnableMouseOverEvents = false;

	ResetCachedMoveAxes(false);
	bForwardHeld = false;
	PendingCombatLookYawInput = 0.f;
	CombatIdleLookYawOffset = 0.f;
	CombatMoveInputGraceRemaining = 0.f;

	if (!IsLocalController())
	{
		return;
	}

	EnterGameOnlyInputMode();

	if (ULocalPlayer* LP = GetLocalPlayer())
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsys = LP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
		{
			if (DefaultMappingContext)
			{
				Subsys->AddMappingContext(DefaultMappingContext, 0);
			}
		}
	}

	RefreshCameraCacheFromPawn(GetPawn());
	PushCachedMoveAxesToPawn();
	PushSprintForwardHeldToPawn();

	if (APawn* P = GetPawn())
	{
		CombatDesiredFacingYaw = FRotator::NormalizeAxis(P->GetActorRotation().Yaw);
	}
	else
	{
		CombatDesiredFacingYaw = FRotator::NormalizeAxis(GetControlRotation().Yaw);
	}
}

void AGGMPlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	bAutoManageActiveCameraTarget = false;
	ResetCachedMoveAxes(false);
	bForwardHeld = false;
	PendingCombatLookYawInput = 0.f;
	CombatIdleLookYawOffset = 0.f;
	CombatMoveInputGraceRemaining = 0.f;

	if (InPawn)
	{
		SetViewTarget(InPawn);
		RefreshCameraCacheFromPawn(InPawn);
		SetControlRotation(FRotator(InitialCameraPitch, InPawn->GetActorRotation().Yaw, 0.f));
		CombatDesiredFacingYaw = FRotator::NormalizeAxis(InPawn->GetActorRotation().Yaw);
	}

	PushCachedMoveAxesToPawn();
	PushSprintForwardHeldToPawn();
	HideDeathScreenDebug();
}

void AGGMPlayerController::AcknowledgePossession(APawn* P)
{
	Super::AcknowledgePossession(P);

	if (IsLocalController())
	{
		ResetCachedMoveAxes(false);
		bForwardHeld = false;
		PendingCombatLookYawInput = 0.f;
		CombatIdleLookYawOffset = 0.f;
		CombatMoveInputGraceRemaining = 0.f;
		RefreshCameraCacheFromPawn(P);

		if (P)
		{
			SetControlRotation(FRotator(InitialCameraPitch, P->GetActorRotation().Yaw, 0.f));
			CombatDesiredFacingYaw = FRotator::NormalizeAxis(P->GetActorRotation().Yaw);
		}

		PushCachedMoveAxesToPawn();
		PushSprintForwardHeldToPawn();
	}

	HideDeathScreenDebug();
}

void AGGMPlayerController::OnRep_Pawn()
{
	Super::OnRep_Pawn();

	if (!IsLocalController())
	{
		return;
	}

	ResetCachedMoveAxes(false);
	bForwardHeld = false;
	PendingCombatLookYawInput = 0.f;
	CombatIdleLookYawOffset = 0.f;
	CombatMoveInputGraceRemaining = 0.f;
	RefreshCameraCacheFromPawn(GetPawn());

	if (APawn* P = GetPawn())
	{
		SetControlRotation(FRotator(InitialCameraPitch, P->GetActorRotation().Yaw, 0.f));
		CombatDesiredFacingYaw = FRotator::NormalizeAxis(P->GetActorRotation().Yaw);
	}

	PushCachedMoveAxesToPawn();
	PushSprintForwardHeldToPawn();
	HideDeathScreenDebug();
}

void AGGMPlayerController::CacheSpringArmFromPawn(APawn* InPawn)
{
	CachedSpringArm = nullptr;

	if (!InPawn)
	{
		return;
	}

	TArray<USpringArmComponent*> Arms;
	InPawn->GetComponents<USpringArmComponent>(Arms);

	if (!PreferredSpringArmName.IsNone())
	{
		for (USpringArmComponent* Arm : Arms)
		{
			if (Arm && Arm->GetFName() == PreferredSpringArmName)
			{
				CachedSpringArm = Arm;
				return;
			}
		}
	}

	for (USpringArmComponent* Arm : Arms)
	{
		if (Arm)
		{
			CachedSpringArm = Arm;
			return;
		}
	}
}

void AGGMPlayerController::RefreshCameraCacheFromPawn(APawn* InPawn)
{
	CacheSpringArmFromPawn(InPawn);

	if (!IsLocalController())
	{
		return;
	}

	if (USpringArmComponent* Arm = GetCachedSpringArm())
	{
		TargetArmLength = Arm->TargetArmLength;
	}
}

USpringArmComponent* AGGMPlayerController::GetCachedSpringArm() const
{
	return CachedSpringArm.Get();
}

AGGMCharacter* AGGMPlayerController::GetGGMCharacterPawn() const
{
	return Cast<AGGMCharacter>(GetPawn());
}

void AGGMPlayerController::EnterGameOnlyInputMode()
{
	bShowMouseCursor = false;
	bEnableClickEvents = false;
	bEnableMouseOverEvents = false;

	FInputModeGameOnly InputMode;
	SetInputMode(InputMode);
}

void AGGMPlayerController::EnterGameAndUIInputMode()
{
	bShowMouseCursor = true;
	bEnableClickEvents = true;
	bEnableMouseOverEvents = true;

	FInputModeGameAndUI InputMode;
	SetInputMode(InputMode);
}

void AGGMPlayerController::PushCachedMoveAxesToPawn()
{
	if (!IsLocalController())
	{
		return;
	}

	if (AGGMCharacter* GGMChar = GetGGMCharacterPawn())
	{
		GGMChar->SubmitRawMoveAxes(CachedMoveForwardAxis, CachedMoveRightAxis);
	}
}

void AGGMPlayerController::PushSprintForwardHeldToPawn()
{
	if (!IsLocalController())
	{
		return;
	}

	if (AGGMCharacter* GGMChar = GetGGMCharacterPawn())
	{
		if (UGGMCharacterMovementComponent* MoveComp = Cast<UGGMCharacterMovementComponent>(GGMChar->GetCharacterMovement()))
		{
			MoveComp->SetSprintForwardHeld(bForwardHeld);
		}
	}
}

void AGGMPlayerController::ResetCachedMoveAxes(bool bPushToPawn)
{
	CachedMoveForwardAxis = 0.f;
	CachedMoveRightAxis = 0.f;

	if (bPushToPawn)
	{
		PushCachedMoveAxesToPawn();
	}
}

void AGGMPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	check(InputComponent);

	if (UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(InputComponent))
	{
		if (IA_Look)
		{
			EIC->BindAction(IA_Look, ETriggerEvent::Triggered, this, &AGGMPlayerController::Look);
		}

		if (IA_Zoom)
		{
			EIC->BindAction(IA_Zoom, ETriggerEvent::Triggered, this, &AGGMPlayerController::Zoom);
		}

		if (IA_Jump)
		{
			EIC->BindAction(IA_Jump, ETriggerEvent::Started, this, &AGGMPlayerController::JumpPressed);
			EIC->BindAction(IA_Jump, ETriggerEvent::Completed, this, &AGGMPlayerController::JumpReleased);
		}
	}

	InputComponent->BindAxis("MoveForward", this, &AGGMPlayerController::MoveForwardAxis);
	InputComponent->BindAxis("MoveRight", this, &AGGMPlayerController::MoveRightAxis);

	InputComponent->BindAction("ToggleWeaponDrawn", IE_Pressed, this, &AGGMPlayerController::ToggleWeaponDrawnPressed);
	InputComponent->BindAction("ToggleFlexMode", IE_Pressed, this, &AGGMPlayerController::ToggleFlexModePressed);

	InputComponent->BindAction("LMB", IE_Pressed, this, &AGGMPlayerController::AttackPressed);
	InputComponent->BindAction("ReviveDebug", IE_Pressed, this, &AGGMPlayerController::DebugRevivePressed);
	InputComponent->BindAction("Respawn", IE_Pressed, this, &AGGMPlayerController::RequestRespawnPressed);

	InputComponent->BindKey(EKeys::RightMouseButton, IE_Pressed, this, &AGGMPlayerController::BlockPressed);
	InputComponent->BindKey(EKeys::RightMouseButton, IE_Released, this, &AGGMPlayerController::BlockReleased);

	InputComponent->BindKey(EKeys::LeftShift, IE_Pressed, this, &AGGMPlayerController::SprintPressed);
	InputComponent->BindKey(EKeys::LeftShift, IE_Released, this, &AGGMPlayerController::SprintReleased);

	InputComponent->BindKey(EKeys::W, IE_Pressed, this, &AGGMPlayerController::ForwardHeldPressed);
	InputComponent->BindKey(EKeys::W, IE_Released, this, &AGGMPlayerController::ForwardHeldReleased);

	InputComponent->BindKey(EKeys::F9, IE_Pressed, this, &AGGMPlayerController::ToggleAttackDebugPressed);
}

void AGGMPlayerController::MoveForwardAxis(float Value)
{
	if (!IsLocalController())
	{
		return;
	}

	CachedMoveForwardAxis = FMath::Clamp(Value, -1.f, 1.f);
	PushCachedMoveAxesToPawn();
}

void AGGMPlayerController::MoveRightAxis(float Value)
{
	if (!IsLocalController())
	{
		return;
	}

	CachedMoveRightAxis = FMath::Clamp(Value, -1.f, 1.f);
	PushCachedMoveAxesToPawn();
}

void AGGMPlayerController::SprintPressed()
{
	if (!IsLocalController())
	{
		return;
	}

	if (AGGMCharacter* GGMChar = GetGGMCharacterPawn())
	{
		if (UGGMCharacterMovementComponent* MoveComp = Cast<UGGMCharacterMovementComponent>(GGMChar->GetCharacterMovement()))
		{
			MoveComp->SetSprintInputPressed(true);
		}
	}
}

void AGGMPlayerController::SprintReleased()
{
	if (!IsLocalController())
	{
		return;
	}

	if (AGGMCharacter* GGMChar = GetGGMCharacterPawn())
	{
		if (UGGMCharacterMovementComponent* MoveComp = Cast<UGGMCharacterMovementComponent>(GGMChar->GetCharacterMovement()))
		{
			MoveComp->SetSprintInputPressed(false);
		}
	}
}

void AGGMPlayerController::ForwardHeldPressed()
{
	if (!IsLocalController())
	{
		return;
	}

	bForwardHeld = true;
	PushSprintForwardHeldToPawn();
}

void AGGMPlayerController::ForwardHeldReleased()
{
	if (!IsLocalController())
	{
		return;
	}

	bForwardHeld = false;
	PushSprintForwardHeldToPawn();
}

void AGGMPlayerController::ToggleWeaponDrawnPressed()
{
	if (!IsLocalController())
	{
		return;
	}

	if (AGGMCharacter* GGMChar = GetGGMCharacterPawn())
	{
		GGMChar->ToggleWeaponDrawn();
	}
}

void AGGMPlayerController::ToggleFlexModePressed()
{
	if (!IsLocalController())
	{
		return;
	}

	if (AGGMCharacter* GGMChar = GetGGMCharacterPawn())
	{
		GGMChar->ToggleFlexMode();
	}
}

void AGGMPlayerController::Look(const FInputActionValue& Value)
{
	if (!IsLocalController())
	{
		return;
	}

	AGGMCharacter* GGMChar = GetGGMCharacterPawn();
	if (!GGMChar)
	{
		return;
	}

	const FVector2D LookAxis = Value.Get<FVector2D>();

	const bool bHasRawMoveInput =
		!FMath::IsNearlyZero(CachedMoveForwardAxis) ||
		!FMath::IsNearlyZero(CachedMoveRightAxis);

	const bool bHasCombatMoveInput = bHasRawMoveInput || (CombatMoveInputGraceRemaining > 0.f);

	if (GGMChar->GetCurrentLocomotionMode() == EGGM_LocomotionMode::Combat)
	{
		if (!bHasCombatMoveInput)
		{
			PendingCombatLookYawInput = 0.f;
			CombatIdleLookYawOffset = FMath::Clamp(
				CombatIdleLookYawOffset + LookAxis.X,
				-CombatIdleLookMaxYawDeg,
				CombatIdleLookMaxYawDeg);

			AddPitchInput(LookAxis.Y * CombatLookPitchMultiplier);
			return;
		}

		PendingCombatLookYawInput += LookAxis.X;
		AddPitchInput(LookAxis.Y * CombatLookPitchMultiplier);
		return;
	}

	CombatIdleLookYawOffset = 0.f;
	AddYawInput(LookAxis.X);
	AddPitchInput(LookAxis.Y);
}

void AGGMPlayerController::Zoom(const FInputActionValue& Value)
{
	if (!IsLocalController())
	{
		return;
	}

	const float Wheel = Value.Get<float>();
	if (FMath::IsNearlyZero(Wheel))
	{
		return;
	}

	APawn* CurrentPawn = GetPawn();
	if (!CurrentPawn)
	{
		return;
	}

	USpringArmComponent* Arm = GetCachedSpringArm();
	if (!Arm)
	{
		RefreshCameraCacheFromPawn(CurrentPawn);
		Arm = GetCachedSpringArm();
	}

	if (!Arm)
	{
		return;
	}

	if (TargetArmLength < 0.f)
	{
		TargetArmLength = Arm->TargetArmLength;
	}

	TargetArmLength = FMath::Clamp(
		TargetArmLength + (-Wheel * CameraZoomStep),
		CameraArmMin,
		CameraArmMax);
}

void AGGMPlayerController::JumpPressed(const FInputActionValue& Value)
{
	if (!IsLocalController())
	{
		return;
	}

	if (AGGMCharacter* GGMChar = GetGGMCharacterPawn())
	{
		GGMChar->Jump();
	}
}

void AGGMPlayerController::JumpReleased(const FInputActionValue& Value)
{
	if (!IsLocalController())
	{
		return;
	}

	if (AGGMCharacter* GGMChar = GetGGMCharacterPawn())
	{
		GGMChar->StopJumping();
	}
}

void AGGMPlayerController::AttackPressed()
{
	if (!IsLocalController())
	{
		return;
	}

	UE_LOG(LogGGMInputDebug, Warning, TEXT("AttackPressed CALLED"));

	if (AGGMCharacter* GGMChar = GetGGMCharacterPawn())
	{
		UE_LOG(LogGGMInputDebug, Warning, TEXT("AttackPressed -> Character found -> StartAttack()"));
		GGMChar->StartAttack();
	}
	else
	{
		UE_LOG(LogGGMInputDebug, Error, TEXT("AttackPressed -> Character NULL"));
	}
}

void AGGMPlayerController::BlockPressed()
{
	if (!IsLocalController())
	{
		return;
	}

	UE_LOG(LogGGMInputDebug, Warning, TEXT("BlockPressed CALLED"));

	if (AGGMCharacter* GGMChar = GetGGMCharacterPawn())
	{
		UE_LOG(LogGGMInputDebug, Warning, TEXT("BlockPressed -> Character found -> StartBlock()"));
		GGMChar->StartBlock();
	}
	else
	{
		UE_LOG(LogGGMInputDebug, Error, TEXT("BlockPressed -> Character NULL"));
	}
}

void AGGMPlayerController::BlockReleased()
{
	if (!IsLocalController())
	{
		return;
	}

	UE_LOG(LogGGMInputDebug, Warning, TEXT("BlockReleased CALLED"));

	if (AGGMCharacter* GGMChar = GetGGMCharacterPawn())
	{
		UE_LOG(LogGGMInputDebug, Warning, TEXT("BlockReleased -> Character found -> StopBlock()"));
		GGMChar->StopBlock();
	}
	else
	{
		UE_LOG(LogGGMInputDebug, Error, TEXT("BlockReleased -> Character NULL"));
	}
}

void AGGMPlayerController::DebugRevivePressed()
{
	if (!IsLocalController())
	{
		return;
	}

	if (AGGMCharacter* GGMChar = GetGGMCharacterPawn())
	{
		GGMChar->DebugRevive_Pressed();
	}
}

void AGGMPlayerController::RequestRespawnPressed()
{
	if (!IsLocalController())
	{
		return;
	}

	AGGMCharacter* GGMChar = GetGGMCharacterPawn();
	if (!GGMChar || !GGMChar->IsDeadStateActive())
	{
		return;
	}

	ServerRequestRespawn();
}

void AGGMPlayerController::ToggleAttackDebugPressed()
{
	if (!IsLocalController())
	{
		return;
	}

	if (AGGMCharacter* GGMChar = GetGGMCharacterPawn())
	{
		GGMChar->DebugToggleAll_Pressed();
	}
}

void AGGMPlayerController::ServerRequestRespawn_Implementation()
{
	AGGMGameMode* GM = GetWorld() ? Cast<AGGMGameMode>(GetWorld()->GetAuthGameMode()) : nullptr;
	if (!GM)
	{
		return;
	}

	AGGMCharacter* GGMChar = GetGGMCharacterPawn();
	if (!GGMChar || !GGMChar->IsDeadStateActive())
	{
		return;
	}

	GM->RespawnPlayerFromDeath(this);
}

void AGGMPlayerController::ShowDeathScreenDebug()
{
	if (!IsLocalController() || bDeathScreenActive)
	{
		return;
	}

	ResetCachedMoveAxes(true);
	bForwardHeld = false;
	PushSprintForwardHeldToPawn();
	PendingCombatLookYawInput = 0.f;
	CombatIdleLookYawOffset = 0.f;
	CombatMoveInputGraceRemaining = 0.f;

	if (PlayerCameraManager)
	{
		PlayerCameraManager->StartCameraFade(0.f, 1.f, 0.25f, FLinearColor::Black, false, true);
	}

	bDeathScreenActive = true;
	EnterGameAndUIInputMode();
}

void AGGMPlayerController::HideDeathScreenDebug()
{
	if (!IsLocalController())
	{
		return;
	}

	if (PlayerCameraManager)
	{
		PlayerCameraManager->StartCameraFade(1.f, 0.f, 0.25f, FLinearColor::Black, false, false);
	}

	bDeathScreenActive = false;
	EnterGameOnlyInputMode();
}

void AGGMPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);

	if (!IsLocalController())
	{
		return;
	}

	AGGMCharacter* GGMChar = GetGGMCharacterPawn();
	if (!GGMChar)
	{
		return;
	}

	const bool bHasRawMoveInput =
		!FMath::IsNearlyZero(CachedMoveForwardAxis) ||
		!FMath::IsNearlyZero(CachedMoveRightAxis);

	if (GGMChar->GetCurrentLocomotionMode() == EGGM_LocomotionMode::Combat)
	{
		if (bHasRawMoveInput)
		{
			CombatMoveInputGraceRemaining = CombatMoveInputGraceTime;
		}
		else
		{
			CombatMoveInputGraceRemaining = FMath::Max(0.f, CombatMoveInputGraceRemaining - DeltaTime);
		}
	}
	else
	{
		CombatMoveInputGraceRemaining = 0.f;
	}

	const bool bHasCombatMoveInput = bHasRawMoveInput || (CombatMoveInputGraceRemaining > 0.f);

	if (GGMChar->GetCurrentLocomotionMode() == EGGM_LocomotionMode::Combat)
	{
		const float ActorYaw = FRotator::NormalizeAxis(GGMChar->GetActorRotation().Yaw);
		const FRotator CurrentControlRotation = GetControlRotation();

		if (!bHasCombatMoveInput)
		{
			CombatDesiredFacingYaw = ActorYaw;
			PendingCombatLookYawInput = 0.f;

			const float CameraYaw = FRotator::NormalizeAxis(ActorYaw + CombatIdleLookYawOffset);
			SetControlRotation(FRotator(CurrentControlRotation.Pitch, CameraYaw, 0.f));
		}
		else
		{
			CombatIdleLookYawOffset = FMath::FInterpConstantTo(
				CombatIdleLookYawOffset,
				0.f,
				DeltaTime,
				CombatIdleLookReturnSpeedDegPerSec);

			const float MaxYawStepThisTick = FMath::Max(0.f, CombatMoveYawSpeedDegPerSec) * DeltaTime;
			const float AppliedYawStep = FMath::Clamp(PendingCombatLookYawInput, -MaxYawStepThisTick, MaxYawStepThisTick);

			CombatDesiredFacingYaw = FRotator::NormalizeAxis(ActorYaw + AppliedYawStep);
			PendingCombatLookYawInput = 0.f;

			const float CameraYaw = FRotator::NormalizeAxis(ActorYaw + CombatIdleLookYawOffset);
			SetControlRotation(FRotator(CurrentControlRotation.Pitch, CameraYaw, 0.f));
		}

		GGMChar->SubmitDesiredFacingYaw(CombatDesiredFacingYaw);
	}
	else
	{
		PendingCombatLookYawInput = 0.f;
		CombatIdleLookYawOffset = 0.f;
		CombatDesiredFacingYaw = FRotator::NormalizeAxis(GetControlRotation().Yaw);
		GGMChar->SubmitDesiredFacingYaw(GetControlRotation().Yaw);
	}

	if (GGMChar->IsDeadStateActive())
	{
		ShowDeathScreenDebug();
	}
	else if (bDeathScreenActive)
	{
		HideDeathScreenDebug();
	}

	if (USpringArmComponent* Arm = GetCachedSpringArm())
	{
		if (TargetArmLength < 0.f)
		{
			TargetArmLength = Arm->TargetArmLength;
		}

		const float NewLen = FMath::FInterpTo(
			Arm->TargetArmLength,
			TargetArmLength,
			DeltaTime,
			FMath::Max(0.1f, CameraZoomInterpSpeed));

		Arm->TargetArmLength = NewLen;
	}
}