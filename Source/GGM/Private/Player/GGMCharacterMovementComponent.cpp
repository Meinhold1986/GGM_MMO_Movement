// GGMCharacterMovementComponent.cpp

#include "Player/GGMCharacterMovementComponent.h"
#include "../../Player/GGMCharacter.h"

#include "GameFramework/Character.h"
#include "GameFramework/Controller.h"
#include "Math/RotationMatrix.h"

namespace
{
	static void GGM_DecodeMoveIntentToAxes(EGGM_MoveInputIntent Intent, float& OutForwardAxis, float& OutRightAxis)
	{
		OutForwardAxis = 0.f;
		OutRightAxis = 0.f;

		switch (Intent)
		{
		case EGGM_MoveInputIntent::Forward:
			OutForwardAxis = 1.f;
			break;

		case EGGM_MoveInputIntent::Backward:
			OutForwardAxis = -1.f;
			break;

		case EGGM_MoveInputIntent::Left:
			OutRightAxis = -1.f;
			break;

		case EGGM_MoveInputIntent::Right:
			OutRightAxis = 1.f;
			break;

		case EGGM_MoveInputIntent::ForwardLeft:
			OutForwardAxis = 1.f;
			OutRightAxis = -1.f;
			break;

		case EGGM_MoveInputIntent::ForwardRight:
			OutForwardAxis = 1.f;
			OutRightAxis = 1.f;
			break;

		case EGGM_MoveInputIntent::BackwardLeft:
			OutForwardAxis = -1.f;
			OutRightAxis = -1.f;
			break;

		case EGGM_MoveInputIntent::BackwardRight:
			OutForwardAxis = -1.f;
			OutRightAxis = 1.f;
			break;

		case EGGM_MoveInputIntent::None:
		default:
			break;
		}
	}
}

FGGMCharacterNetworkMoveDataContainer::FGGMCharacterNetworkMoveDataContainer()
	: FCharacterNetworkMoveDataContainer()
{
	NewMoveData = &CustomNewMoveData;
	PendingMoveData = &CustomPendingMoveData;
	OldMoveData = &CustomOldMoveData;
}

void FGGMCharacterNetworkMoveData::ClientFillNetworkMoveData(const FSavedMove_Character& ClientMove, ENetworkMoveType MoveType)
{
	Super::ClientFillNetworkMoveData(ClientMove, MoveType);

	const FSavedMove_GGM& GGMMove = static_cast<const FSavedMove_GGM&>(ClientMove);

	MoveInputIntent = GGMMove.SavedMoveInputIntent;
	bSprintInputPressed = GGMMove.bSavedSprintInputPressed ? 1 : 0;
	bSprintForwardHeld = GGMMove.bSavedSprintForwardHeld ? 1 : 0;
}

bool FGGMCharacterNetworkMoveData::Serialize(UCharacterMovementComponent& CharacterMovement, FArchive& Ar, UPackageMap* PackageMap, ENetworkMoveType MoveType)
{
	const bool bSuperSuccess = Super::Serialize(CharacterMovement, Ar, PackageMap, MoveType);

	Ar << MoveInputIntent;

	uint8 SprintInputPressedBit = bSprintInputPressed ? 1 : 0;
	uint8 SprintForwardHeldBit = bSprintForwardHeld ? 1 : 0;

	Ar.SerializeBits(&SprintInputPressedBit, 1);
	Ar.SerializeBits(&SprintForwardHeldBit, 1);

	if (Ar.IsLoading())
	{
		bSprintInputPressed = SprintInputPressedBit ? 1 : 0;
		bSprintForwardHeld = SprintForwardHeldBit ? 1 : 0;
	}

	return !Ar.IsError() && bSuperSuccess;
}

FNetworkPredictionData_Client_GGM::FNetworkPredictionData_Client_GGM(const UCharacterMovementComponent& ClientMovement)
	: Super(ClientMovement)
{
}

FSavedMovePtr FNetworkPredictionData_Client_GGM::AllocateNewMove()
{
	return FSavedMovePtr(new FSavedMove_GGM());
}

UGGMCharacterMovementComponent::UGGMCharacterMovementComponent()
{
	bOrientRotationToMovement = false;
	bUseControllerDesiredRotation = false;
	RotationRate = FRotator::ZeroRotator;

	NetworkSmoothingMode = ENetworkSmoothingMode::Disabled;
	SetNetworkMoveDataContainer(GGMNetworkMoveDataContainer);
}

void UGGMCharacterMovementComponent::BeginPlay()
{
	Super::BeginPlay();

	CachedGGMCharacter = Cast<AGGMCharacter>(CharacterOwner);

	if (CharacterOwner)
	{
		const float InitialYaw = FRotator::NormalizeAxis(CharacterOwner->GetActorRotation().Yaw);
		DesiredFacingYaw = InitialYaw;
		RemoteVisualYawTarget = InitialYaw;
		RemoteAppliedVisualYaw = InitialYaw;
		bRemoteVisualYawInitialized = true;
		LockedSprintYaw = InitialYaw;
		AirborneLockedYaw = InitialYaw;
		BaseSnapshot.MovementRotationTargetYaw = InitialYaw;
		BaseSnapshot.DesiredFacingYaw = InitialYaw;
	}

	bRemoteVisualIsSprinting = false;
	RemoteBlendForwardAxis = 0.f;
	RemoteBlendRightAxis = 0.f;
	RemoteMoveSpeed = 0.f;
	RemoteMoveDirection = FVector::ZeroVector;
}

void UGGMCharacterMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	if (!CharacterOwner)
	{
		Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
		return;
	}

	UpdateAirborneState();
	RefreshRuntimeMovementState();
	TickSprint(DeltaTime);
	RefreshRuntimeMovementSnapshot();
	TickSmoothedMovementState(DeltaTime);
	RefreshRuntimeMovementSpeed();
	ApplyMovementInput();

	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	UpdateRotation(DeltaTime);
	TickRemoteVisualYaw(DeltaTime);

	if (CharacterOwner->HasAuthority() && CachedGGMCharacter)
	{
		const FGGMRemoteLocomotionSnapshot RemoteSnapshot = BuildRemoteLocomotionSnapshot();
		CachedGGMCharacter->ServerAuthSetRemoteLocomotionSnapshot(RemoteSnapshot);
	}
}

void UGGMCharacterMovementComponent::UpdateFromCompressedFlags(uint8 Flags)
{
	Super::UpdateFromCompressedFlags(Flags);
	bSprintInputPressed = (Flags & FSavedMove_Character::FLAG_Custom_0) != 0;
}

FNetworkPredictionData_Client* UGGMCharacterMovementComponent::GetPredictionData_Client() const
{
	check(PawnOwner != nullptr);

	if (ClientPredictionData == nullptr)
	{
		UGGMCharacterMovementComponent* MutableThis = const_cast<UGGMCharacterMovementComponent*>(this);
		MutableThis->ClientPredictionData = new FNetworkPredictionData_Client_GGM(*this);
	}

	return ClientPredictionData;
}

void UGGMCharacterMovementComponent::MoveAutonomous(float ClientTimeStamp, float DeltaTime, uint8 CompressedFlags, const FVector& NewAccel)
{
	ApplyCurrentReplicatedMoveData();
	UpdateFromCompressedFlags(CompressedFlags);

	Super::MoveAutonomous(ClientTimeStamp, DeltaTime, CompressedFlags, NewAccel);
}

void UGGMCharacterMovementComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
}

void UGGMCharacterMovementComponent::SetOwnerRawMoveAxes(float InForwardAxis, float InRightAxis)
{
	OwnerRawForwardAxis = FMath::Clamp(InForwardAxis, -1.f, 1.f);
	OwnerRawRightAxis = FMath::Clamp(InRightAxis, -1.f, 1.f);
}

void UGGMCharacterMovementComponent::SetOwnerDesiredFacingYaw(float InDesiredFacingYaw)
{
	SetDesiredFacingYawInternal(InDesiredFacingYaw);
}

void UGGMCharacterMovementComponent::SetSprintInputPressed(bool bPressed)
{
	bSprintInputPressed = bPressed;
}

void UGGMCharacterMovementComponent::SetSprintForwardHeld(bool bHeld)
{
	bSprintForwardHeld = bHeld;
}

void UGGMCharacterMovementComponent::ApplyInputIntent(EGGM_MoveInputIntent InIntent)
{
	float DecodedForward = 0.f;
	float DecodedRight = 0.f;
	GGM_DecodeMoveIntentToAxes(InIntent, DecodedForward, DecodedRight);

	SetOwnerRawMoveAxes(DecodedForward, DecodedRight);
	MoveInputIntent = InIntent;
}

void UGGMCharacterMovementComponent::ApplyCurrentReplicatedMoveData()
{
	const FCharacterNetworkMoveData* CurrentMoveData = GetCurrentNetworkMoveData();
	const FGGMCharacterNetworkMoveData* GGMMoveData = static_cast<const FGGMCharacterNetworkMoveData*>(CurrentMoveData);

	if (GGMMoveData == nullptr)
	{
		return;
	}

	ApplyInputIntent(static_cast<EGGM_MoveInputIntent>(GGMMoveData->MoveInputIntent));
	SetSprintInputPressed(GGMMoveData->bSprintInputPressed != 0);
	SetSprintForwardHeld(GGMMoveData->bSprintForwardHeld != 0);
}

float UGGMCharacterMovementComponent::GetBlendForwardAxis() const
{
	return IsSimulatedProxyInstance()
		? RemoteBlendForwardAxis
		: OwnerRawForwardAxis;
}

float UGGMCharacterMovementComponent::GetBlendRightAxis() const
{
	return IsSimulatedProxyInstance()
		? RemoteBlendRightAxis
		: OwnerRawRightAxis;
}

float UGGMCharacterMovementComponent::GetMoveSpeed() const
{
	return IsSimulatedProxyInstance()
		? RemoteMoveSpeed
		: GetAnimationRuntimeSpeed();
}

float UGGMCharacterMovementComponent::GetVisualYaw() const
{
	if (IsSimulatedProxyInstance())
	{
		return FRotator::NormalizeAxis(RemoteAppliedVisualYaw);
	}

	return CharacterOwner
		? FRotator::NormalizeAxis(CharacterOwner->GetActorRotation().Yaw)
		: 0.f;
}

FVector UGGMCharacterMovementComponent::GetMoveDirection() const
{
	if (IsSimulatedProxyInstance())
	{
		return RemoteMoveDirection.IsNearlyZero()
			? FVector::ZeroVector
			: RemoteMoveDirection.GetSafeNormal();
	}

	return SmoothedMoveWorldDirection.IsNearlyZero()
		? FVector::ZeroVector
		: SmoothedMoveWorldDirection.GetSafeNormal();
}

void UGGMCharacterMovementComponent::ForceStopSprint(bool bPreserveFutureIntent)
{
	ExitSprint();

	if (!bPreserveFutureIntent)
	{
		SetDesiredFacingYawInternal(CharacterOwner ? CharacterOwner->GetActorRotation().Yaw : DesiredFacingYaw);
	}
}

void UGGMCharacterMovementComponent::RefreshRuntimeMovementState()
{
	RefreshRuntimeInputState();
	RefreshRuntimeLocomotionModeState();
	RefreshRuntimeLocomotionGroupState();

	BaseSnapshot.LocomotionMode = CurrentLocomotionMode;
	BaseSnapshot.LocomotionGroup = CurrentLocomotionGroup;
	BaseSnapshot.DesiredFacingYaw = GetAuthorityFacingYawSource();
}

void UGGMCharacterMovementComponent::RefreshRuntimeInputState()
{
	MoveInputIntent = BuildMoveInputIntentFromAxes(OwnerRawForwardAxis, OwnerRawRightAxis);
}

void UGGMCharacterMovementComponent::RefreshRuntimeLocomotionModeState()
{
	if (CachedGGMCharacter)
	{
		CurrentLocomotionMode = CachedGGMCharacter->GetCurrentLocomotionMode();
		return;
	}

	CurrentLocomotionMode = EGGM_LocomotionMode::Travel;
}

void UGGMCharacterMovementComponent::RefreshRuntimeLocomotionGroupState()
{
	CurrentLocomotionGroup = BuildRuntimeLocomotionGroupFromInputIntent(MoveInputIntent);
}

void UGGMCharacterMovementComponent::RefreshRuntimeMovementSpeed()
{
	MaxWalkSpeed = GetBaseSpeed();
}

void UGGMCharacterMovementComponent::TickSmoothedMovementState(float DeltaTime)
{
	if (IsSimulatedProxyInstance())
	{
		return;
	}

	const FVector TargetDirection = GetAppliedMoveDirection();
	const float TargetSpeed = GetBaseSpeed();

	if (!bSmoothedMovementInitialized)
	{
		SmoothedMoveWorldDirection = TargetDirection.GetSafeNormal();
		SmoothedMoveSpeed = 0.f;
		bSmoothedMovementInitialized = true;
	}

	if (IsAirborneLockActive())
	{
		SmoothedMoveWorldDirection = AirborneLockedMoveDirection.GetSafeNormal();
		SmoothedMoveSpeed = 0.f;
		return;
	}

	if (IsSprintLockActive())
	{
		SmoothedMoveWorldDirection = LockedSprintMoveDirection.GetSafeNormal();
		SmoothedMoveSpeed = SprintCurrentSpeed;
		return;
	}

	const FVector CurrentDirection = SmoothedMoveWorldDirection.GetSafeNormal();
	const FVector DesiredDirection = TargetDirection.GetSafeNormal();

	if (DesiredDirection.IsNearlyZero())
	{
		SmoothedMoveWorldDirection = CurrentDirection;
	}
	else if (CurrentDirection.IsNearlyZero())
	{
		SmoothedMoveWorldDirection = DesiredDirection;
	}
	else
	{
		const FVector BlendedDirection = FMath::VInterpNormalRotationTo(
			CurrentDirection,
			DesiredDirection,
			DeltaTime,
			FMath::Max(0.f, MovementDirectionInterpSpeed));

		SmoothedMoveWorldDirection = BlendedDirection.GetSafeNormal();
	}

	const bool bAccelerating = SmoothedMoveSpeed < TargetSpeed;
	const float InterpRate = bAccelerating ? MovementAccelerationRate : MovementDecelerationRate;

	SmoothedMoveSpeed = FMath::FInterpConstantTo(
		SmoothedMoveSpeed,
		TargetSpeed,
		DeltaTime,
		FMath::Max(0.f, InterpRate));
}

void UGGMCharacterMovementComponent::TickSprint(float DeltaTime)
{
	if (ShouldExitSprintForAirborne())
	{
		ExitSprint();
		return;
	}

	const bool bWantsSprint = bSprintInputPressed && CanEnterSprint();

	if (bWantsSprint)
	{
		if (!bIsSprinting)
		{
			BeginSprint();
		}

		const float TargetSprintSpeed = GetSprintTargetSpeed();

		SprintCurrentSpeed = FMath::FInterpConstantTo(
			SprintCurrentSpeed,
			TargetSprintSpeed,
			DeltaTime,
			SprintAccelerationRate);

		return;
	}

	if (bIsSprinting)
	{
		const float TargetBaseSpeed = GetForwardBaseSpeed();

		SprintCurrentSpeed = FMath::FInterpConstantTo(
			SprintCurrentSpeed,
			TargetBaseSpeed,
			DeltaTime,
			SprintDecelerationRate);

		if (FMath::Abs(SprintCurrentSpeed - TargetBaseSpeed) <= SprintExitCompleteSpeedTolerance)
		{
			ExitSprint();
		}
	}
}

void UGGMCharacterMovementComponent::UpdateAirborneState()
{
	const bool bIsNowFalling = IsFalling();

	if (bIsNowFalling && !bWasFallingLastFrame)
	{
		bAirborneLockActive = true;

		const FVector CurrentDirection = GetAppliedMoveDirection();
		AirborneLockedMoveDirection = CurrentDirection.IsNearlyZero()
			? Velocity.GetSafeNormal2D()
			: CurrentDirection.GetSafeNormal();

		AirborneLockedYaw = CharacterOwner
			? FRotator::NormalizeAxis(CharacterOwner->GetActorRotation().Yaw)
			: AirborneLockedYaw;
	}
	else if (!bIsNowFalling && bWasFallingLastFrame)
	{
		bAirborneLockActive = false;
	}

	bWasFallingLastFrame = bIsNowFalling;
}

void UGGMCharacterMovementComponent::UpdateRotation(float DeltaTime)
{
	if (!CharacterOwner)
	{
		return;
	}

	if (IsSimulatedProxyInstance())
	{
		return;
	}

	if (!ShouldApplyOwnedRotation())
	{
		return;
	}

	const float CurrentYaw = FRotator::NormalizeAxis(CharacterOwner->GetActorRotation().Yaw);
	const float TargetYaw = GetAppliedRotationTargetYaw();

	const bool bHasMovement = HasMovementInput();
	const bool bLowSpeed = Velocity.SizeSquared2D() <= FMath::Square(5.f);
	const float TurnSpeed = (bHasMovement && bLowSpeed)
		? MovementRotationStartSpeedDegPerSec
		: MovementRotationMovingSpeedDegPerSec;

	const float NewYaw = FMath::FixedTurn(
		CurrentYaw,
		TargetYaw,
		TurnSpeed * DeltaTime);

	CharacterOwner->SetActorRotation(FRotator(0.f, NewYaw, 0.f));
}

void UGGMCharacterMovementComponent::ApplyMovementInput()
{
	if (!CharacterOwner)
	{
		return;
	}

	if (IsSimulatedProxyInstance())
	{
		return;
	}

	if (!ShouldConsumeInputForGroundMovement())
	{
		return;
	}

	const FVector AppliedDirection = GetAppliedMoveDirection();
	if (!AppliedDirection.IsNearlyZero())
	{
		AddInputVector(AppliedDirection);
	}
}

void UGGMCharacterMovementComponent::TickRemoteVisualYaw(float DeltaTime)
{
	if (!CharacterOwner || !IsSimulatedProxyInstance())
	{
		return;
	}

	if (!bRemoteVisualYawInitialized)
	{
		ApplyRemoteVisualYawInstant(CharacterOwner->GetActorRotation().Yaw);
		return;
	}

	const float CurrentYaw = FRotator::NormalizeAxis(RemoteAppliedVisualYaw);
	const float TargetYaw = FRotator::NormalizeAxis(RemoteVisualYawTarget);

	const float NewYaw = FMath::FixedTurn(
		CurrentYaw,
		TargetYaw,
		FMath::Max(0.f, RemoteMovementVisualYawSyncSpeedDegPerSec) * DeltaTime);

	RemoteAppliedVisualYaw = FRotator::NormalizeAxis(NewYaw);
}

void UGGMCharacterMovementComponent::ApplyRemoteVisualYawInstant(float InVisualYaw)
{
	const float NormalizedYaw = FRotator::NormalizeAxis(InVisualYaw);
	RemoteVisualYawTarget = NormalizedYaw;
	RemoteAppliedVisualYaw = NormalizedYaw;
	bRemoteVisualYawInitialized = true;
}

bool UGGMCharacterMovementComponent::ShouldApplyOwnedRotation() const
{
	if (!CharacterOwner)
	{
		return false;
	}

	if (IsSimulatedProxyInstance())
	{
		return false;
	}

	if (!HasMovementInput() && !IsSprintLockActive())
	{
		return false;
	}

	if (IsAirborneLockActive())
	{
		return false;
	}

	if (IsSprintLockActive())
	{
		return false;
	}

	return true;
}

bool UGGMCharacterMovementComponent::IsAirborneLockActive() const
{
	return bAirborneLockActive;
}

bool UGGMCharacterMovementComponent::IsSprintLockActive() const
{
	return bIsSprinting;
}

bool UGGMCharacterMovementComponent::ShouldConsumeInputForGroundMovement() const
{
	if (IsAirborneLockActive())
	{
		return false;
	}

	if (IsSprintLockActive())
	{
		return !LockedSprintMoveDirection.IsNearlyZero();
	}

	return HasMovementInput();
}

float UGGMCharacterMovementComponent::GetBaseSpeed() const
{
	if (!CachedGGMCharacter)
	{
		return 0.f;
	}

	const FGGM_MoveSpeedStats& Stats = CachedGGMCharacter->MoveSpeedStats;

	if (bIsSprinting)
	{
		return SprintCurrentSpeed;
	}

	switch (CurrentLocomotionMode)
	{
	case EGGM_LocomotionMode::Flex:
		return Stats.Flex;

	case EGGM_LocomotionMode::Combat:
		switch (CurrentLocomotionGroup)
		{
		case EGGM_LocomotionGroup::Forward:
			return Stats.Forward * Stats.CombatForwardMultiplier;

		case EGGM_LocomotionGroup::Strafe:
		case EGGM_LocomotionGroup::Backward:
			return Stats.StrafeBackward * Stats.CombatStrafeBackwardMultiplier;

		case EGGM_LocomotionGroup::Idle:
		default:
			return 0.f;
		}

	case EGGM_LocomotionMode::Travel:
	default:
		switch (CurrentLocomotionGroup)
		{
		case EGGM_LocomotionGroup::Forward:
			return Stats.Forward;

		case EGGM_LocomotionGroup::Strafe:
		case EGGM_LocomotionGroup::Backward:
			return Stats.StrafeBackward;

		case EGGM_LocomotionGroup::Idle:
		default:
			return 0.f;
		}
	}
}

float UGGMCharacterMovementComponent::GetForwardBaseSpeed() const
{
	if (!CachedGGMCharacter)
	{
		return 600.f;
	}

	const FGGM_MoveSpeedStats& Stats = CachedGGMCharacter->MoveSpeedStats;

	switch (CurrentLocomotionMode)
	{
	case EGGM_LocomotionMode::Combat:
		return Stats.Forward * Stats.CombatForwardMultiplier;

	case EGGM_LocomotionMode::Flex:
		return Stats.Flex;

	case EGGM_LocomotionMode::Travel:
	default:
		return Stats.Forward;
	}
}

float UGGMCharacterMovementComponent::GetSprintTargetSpeed() const
{
	if (!CachedGGMCharacter)
	{
		return 800.f;
	}

	return CachedGGMCharacter->MoveSpeedStats.Sprint;
}

float UGGMCharacterMovementComponent::GetAnimationRuntimeSpeed() const
{
	if (!bSmoothedMovementInitialized)
	{
		return 0.f;
	}

	if (IsAirborneLockActive())
	{
		return 0.f;
	}

	if (IsSprintLockActive())
	{
		return SprintCurrentSpeed;
	}

	return SmoothedMoveSpeed;
}

void UGGMCharacterMovementComponent::ExitSprint()
{
	bIsSprinting = false;
	SprintCurrentSpeed = 0.f;
	LockedSprintMoveDirection = FVector::ZeroVector;
}

void UGGMCharacterMovementComponent::CaptureSprintLockState()
{
	const float LockedYawSource = FRotator::NormalizeAxis(GetAuthorityFacingYawSource());

	const FVector ForwardOnly = FRotationMatrix(FRotator(0.f, LockedYawSource, 0.f)).GetUnitAxis(EAxis::X);
	LockedSprintMoveDirection = FVector(ForwardOnly.X, ForwardOnly.Y, 0.f).GetSafeNormal();

	if (LockedSprintMoveDirection.IsNearlyZero())
	{
		LockedSprintMoveDirection = GetAppliedMoveDirection();
	}

	LockedSprintMoveDirection = LockedSprintMoveDirection.GetSafeNormal();
	LockedSprintYaw = LockedYawSource;
}

bool UGGMCharacterMovementComponent::ShouldExitSprintForAirborne() const
{
	return bIsSprinting && IsFalling();
}

void UGGMCharacterMovementComponent::BeginSprint()
{
	if (bIsSprinting)
	{
		return;
	}

	CaptureSprintLockState();
	bIsSprinting = true;
	SprintCurrentSpeed = FMath::Max(SprintCurrentSpeed, GetForwardBaseSpeed());
}

bool UGGMCharacterMovementComponent::HasMovementInput() const
{
	return !FMath::IsNearlyZero(OwnerRawForwardAxis) || !FMath::IsNearlyZero(OwnerRawRightAxis);
}

bool UGGMCharacterMovementComponent::IsTravelMode() const
{
	return CurrentLocomotionMode == EGGM_LocomotionMode::Travel;
}

bool UGGMCharacterMovementComponent::IsGroundedForSprint() const
{
	return !IsFalling();
}

bool UGGMCharacterMovementComponent::HasSprintStamina() const
{
	return true;
}

bool UGGMCharacterMovementComponent::CanEnterSprint() const
{
	return IsTravelMode()
		&& IsGroundedForSprint()
		&& HasSprintStamina()
		&& bSprintForwardHeld
		&& OwnerRawForwardAxis > 0.f;
}

bool UGGMCharacterMovementComponent::CanStartJump() const
{
	return !bIsSprinting;
}

float UGGMCharacterMovementComponent::GetMovementBasisYaw() const
{
	return GetAuthorityFacingYawSource();
}

FVector UGGMCharacterMovementComponent::GetCameraRelativeMoveDirection() const
{
	const float BasisYaw = GetMovementBasisYaw();

	const FVector Forward = FRotationMatrix(FRotator(0.f, BasisYaw, 0.f)).GetUnitAxis(EAxis::X);
	const FVector Right = FRotationMatrix(FRotator(0.f, BasisYaw, 0.f)).GetUnitAxis(EAxis::Y);

	FVector Direction = (Forward * OwnerRawForwardAxis) + (Right * OwnerRawRightAxis);
	Direction.Z = 0.f;
	return Direction.GetSafeNormal();
}

FVector UGGMCharacterMovementComponent::GetAppliedMoveDirection() const
{
	if (IsSprintLockActive())
	{
		return LockedSprintMoveDirection.GetSafeNormal();
	}

	if (IsAirborneLockActive())
	{
		return AirborneLockedMoveDirection.GetSafeNormal();
	}

	return GetCameraRelativeMoveDirection();
}

float UGGMCharacterMovementComponent::GetAppliedRotationTargetYaw() const
{
	if (IsSprintLockActive())
	{
		return LockedSprintYaw;
	}

	if (IsAirborneLockActive())
	{
		return AirborneLockedYaw;
	}

	return GetAuthorityFacingYawSource();
}

float UGGMCharacterMovementComponent::GetNormalizedYaw(float InYaw) const
{
	return FRotator::NormalizeAxis(InYaw);
}

float UGGMCharacterMovementComponent::GetAuthorityFacingYawSource() const
{
	if (CharacterOwner && CharacterOwner->HasAuthority())
	{
		if (const AController* OwnerController = CharacterOwner->GetController())
		{
			return FRotator::NormalizeAxis(OwnerController->GetControlRotation().Yaw);
		}

		return FRotator::NormalizeAxis(CharacterOwner->GetActorRotation().Yaw);
	}

	return DesiredFacingYaw;
}

bool UGGMCharacterMovementComponent::IsSimulatedProxyInstance() const
{
	return CharacterOwner && !CharacterOwner->HasAuthority() && !CharacterOwner->IsLocallyControlled();
}

EGGM_MoveInputIntent UGGMCharacterMovementComponent::BuildMoveInputIntentFromAxes(float InForwardAxis, float InRightAxis) const
{
	const bool bForward = InForwardAxis > 0.f;
	const bool bBackward = InForwardAxis < 0.f;
	const bool bRight = InRightAxis > 0.f;
	const bool bLeft = InRightAxis < 0.f;

	if (bForward && bLeft)
	{
		return EGGM_MoveInputIntent::ForwardLeft;
	}
	if (bForward && bRight)
	{
		return EGGM_MoveInputIntent::ForwardRight;
	}
	if (bBackward && bLeft)
	{
		return EGGM_MoveInputIntent::BackwardLeft;
	}
	if (bBackward && bRight)
	{
		return EGGM_MoveInputIntent::BackwardRight;
	}
	if (bForward)
	{
		return EGGM_MoveInputIntent::Forward;
	}
	if (bBackward)
	{
		return EGGM_MoveInputIntent::Backward;
	}
	if (bLeft)
	{
		return EGGM_MoveInputIntent::Left;
	}
	if (bRight)
	{
		return EGGM_MoveInputIntent::Right;
	}

	return EGGM_MoveInputIntent::None;
}

EGGM_LocomotionGroup UGGMCharacterMovementComponent::BuildRuntimeLocomotionGroupFromInputIntent(EGGM_MoveInputIntent InIntent) const
{
	switch (InIntent)
	{
	case EGGM_MoveInputIntent::Forward:
	case EGGM_MoveInputIntent::ForwardLeft:
	case EGGM_MoveInputIntent::ForwardRight:
		return EGGM_LocomotionGroup::Forward;

	case EGGM_MoveInputIntent::Backward:
	case EGGM_MoveInputIntent::BackwardLeft:
	case EGGM_MoveInputIntent::BackwardRight:
		return EGGM_LocomotionGroup::Backward;

	case EGGM_MoveInputIntent::Left:
	case EGGM_MoveInputIntent::Right:
		return EGGM_LocomotionGroup::Strafe;

	case EGGM_MoveInputIntent::None:
	default:
		return EGGM_LocomotionGroup::Idle;
	}
}

void UGGMCharacterMovementComponent::SetDesiredFacingYawInternal(float InDesiredFacingYaw)
{
	DesiredFacingYaw = GetNormalizedYaw(InDesiredFacingYaw);
	BaseSnapshot.DesiredFacingYaw = GetAuthorityFacingYawSource();
}

void UGGMCharacterMovementComponent::RefreshRuntimeMovementSnapshot()
{
	BaseSnapshot.DesiredFacingYaw = GetAuthorityFacingYawSource();
	BaseSnapshot.LocomotionMode = CurrentLocomotionMode;
	BaseSnapshot.LocomotionGroup = CurrentLocomotionGroup;

	if (IsSprintLockActive())
	{
		BaseSnapshot.DesiredMoveWorldDirection = LockedSprintMoveDirection.GetSafeNormal();
		BaseSnapshot.MovementRotationTargetYaw = LockedSprintYaw;
		return;
	}

	if (IsAirborneLockActive())
	{
		BaseSnapshot.DesiredMoveWorldDirection = AirborneLockedMoveDirection.GetSafeNormal();
		BaseSnapshot.MovementRotationTargetYaw = AirborneLockedYaw;
		return;
	}

	BaseSnapshot.DesiredMoveWorldDirection = GetAppliedMoveDirection();
	BaseSnapshot.MovementRotationTargetYaw = GetAuthorityFacingYawSource();
}

FGGMRemoteLocomotionSnapshot UGGMCharacterMovementComponent::BuildRemoteLocomotionSnapshot() const
{
	FGGMRemoteLocomotionSnapshot Snapshot;
	Snapshot.SnapshotSequence = ++RemoteSnapshotSequenceCounter;
	Snapshot.MoveSpeed = GetAnimationRuntimeSpeed();
	Snapshot.BlendForwardAxis = OwnerRawForwardAxis;
	Snapshot.BlendRightAxis = OwnerRawRightAxis;

	const FVector Dir = SmoothedMoveWorldDirection.IsNearlyZero()
		? FVector::ZeroVector
		: SmoothedMoveWorldDirection.GetSafeNormal();

	Snapshot.MoveDirection = Dir.IsNearlyZero()
		? FVector(1.f, 0.f, 0.f)
		: Dir;

	Snapshot.VisualYaw = CharacterOwner
		? FRotator::NormalizeAxis(CharacterOwner->GetActorRotation().Yaw)
		: RemoteVisualYawTarget;

	Snapshot.LocomotionGroup = CurrentLocomotionGroup;
	Snapshot.bIsSprinting = bIsSprinting;
	Snapshot.SourceVelocity2D = Velocity.Size2D();
	Snapshot.SourceMaxWalkSpeed = MaxWalkSpeed;
	Snapshot.SourceSmoothedMoveSpeed = SmoothedMoveSpeed;
	Snapshot.SourceActorYaw = CharacterOwner ? FRotator::NormalizeAxis(CharacterOwner->GetActorRotation().Yaw) : 0.f;
	Snapshot.SourceDesiredFacingYaw = GetAuthorityFacingYawSource();
	return Snapshot;
}

void UGGMCharacterMovementComponent::ApplyRemoteLocomotionSnapshot(const FGGMRemoteLocomotionSnapshot& InSnapshot)
{
	RemoteBlendForwardAxis = FMath::Clamp(InSnapshot.BlendForwardAxis, -1.f, 1.f);
	RemoteBlendRightAxis = FMath::Clamp(InSnapshot.BlendRightAxis, -1.f, 1.f);
	RemoteMoveSpeed = FMath::Max(0.f, InSnapshot.MoveSpeed);
	RemoteMoveDirection = InSnapshot.MoveDirection.IsNearlyZero()
		? FVector::ZeroVector
		: InSnapshot.MoveDirection.GetSafeNormal();

	CurrentLocomotionGroup = InSnapshot.LocomotionGroup;
	bRemoteVisualIsSprinting = InSnapshot.bIsSprinting;
	RemoteVisualYawTarget = FRotator::NormalizeAxis(InSnapshot.VisualYaw);

	if (!bRemoteVisualYawInitialized)
	{
		ApplyRemoteVisualYawInstant(InSnapshot.VisualYaw);
	}
}

void FSavedMove_GGM::Clear()
{
	Super::Clear();

	SavedMoveInputIntent = 0;
	bSavedSprintInputPressed = false;
	bSavedSprintForwardHeld = false;
}

uint8 FSavedMove_GGM::GetCompressedFlags() const
{
	uint8 Result = Super::GetCompressedFlags();

	if (bSavedSprintInputPressed)
	{
		Result |= FLAG_Custom_0;
	}

	return Result;
}

bool FSavedMove_GGM::CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* Character, float MaxDelta) const
{
	const FSavedMove_GGM* NewGGMMove = static_cast<const FSavedMove_GGM*>(NewMove.Get());
	if (NewGGMMove == nullptr)
	{
		return false;
	}

	if (SavedMoveInputIntent != NewGGMMove->SavedMoveInputIntent)
	{
		return false;
	}

	if (bSavedSprintInputPressed != NewGGMMove->bSavedSprintInputPressed)
	{
		return false;
	}

	if (bSavedSprintForwardHeld != NewGGMMove->bSavedSprintForwardHeld)
	{
		return false;
	}

	return Super::CanCombineWith(NewMove, Character, MaxDelta);
}

void FSavedMove_GGM::SetMoveFor(ACharacter* Character, float InDeltaTime, const FVector& NewAccel, FNetworkPredictionData_Client_Character& ClientData)
{
	Super::SetMoveFor(Character, InDeltaTime, NewAccel, ClientData);

	const UGGMCharacterMovementComponent* MoveComp = Character
		? Cast<UGGMCharacterMovementComponent>(Character->GetCharacterMovement())
		: nullptr;

	if (MoveComp)
	{
		SavedMoveInputIntent = static_cast<uint8>(MoveComp->GetMoveInputIntent());
		bSavedSprintInputPressed = MoveComp->IsSprintInputPressed();
		bSavedSprintForwardHeld = MoveComp->IsSprintForwardHeld();
	}
	else
	{
		SavedMoveInputIntent = 0;
		bSavedSprintInputPressed = false;
		bSavedSprintForwardHeld = false;
	}
}

void FSavedMove_GGM::PrepMoveFor(ACharacter* Character)
{
	Super::PrepMoveFor(Character);

	UGGMCharacterMovementComponent* MoveComp = Character
		? Cast<UGGMCharacterMovementComponent>(Character->GetCharacterMovement())
		: nullptr;

	if (MoveComp)
	{
		MoveComp->ApplyInputIntent(static_cast<EGGM_MoveInputIntent>(SavedMoveInputIntent));
		MoveComp->SetSprintInputPressed(bSavedSprintInputPressed);
		MoveComp->SetSprintForwardHeld(bSavedSprintForwardHeld);
	}
}