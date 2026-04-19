#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/CharacterMovementReplication.h"
#include "Net/UnrealNetwork.h"
#include "Player/GGMMovementTypes.h"
#include "GGMCharacterMovementComponent.generated.h"

class AGGMCharacter;

UENUM(BlueprintType)
enum class EGGM_MoveInputIntent : uint8
{
	None = 0,
	Forward = 1,
	Backward = 2,
	Left = 3,
	Right = 4,
	ForwardLeft = 5,
	ForwardRight = 6,
	BackwardLeft = 7,
	BackwardRight = 8
};

USTRUCT(BlueprintType)
struct FGGMRuntimeLocomotionSnapshot
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement")
	EGGM_LocomotionMode LocomotionMode = EGGM_LocomotionMode::Travel;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement")
	EGGM_LocomotionGroup LocomotionGroup = EGGM_LocomotionGroup::Idle;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement")
	FVector DesiredMoveWorldDirection = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement")
	float MovementRotationTargetYaw = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement")
	float DesiredFacingYaw = 0.f;
};

USTRUCT(BlueprintType)
struct FGGMRemoteLocomotionSnapshot
{
	GENERATED_BODY()

	UPROPERTY()
	uint16 SnapshotSequence = 0;

	UPROPERTY()
	float MoveSpeed = 0.f;

	UPROPERTY()
	float BlendForwardAxis = 0.f;

	UPROPERTY()
	float BlendRightAxis = 0.f;

	UPROPERTY()
	FVector MoveDirection = FVector::ZeroVector;

	UPROPERTY()
	float VisualYaw = 0.f;

	UPROPERTY()
	EGGM_LocomotionGroup LocomotionGroup = EGGM_LocomotionGroup::Idle;

	UPROPERTY()
	bool bIsSprinting = false;

	UPROPERTY()
	float SourceVelocity2D = 0.f;

	UPROPERTY()
	float SourceMaxWalkSpeed = 0.f;

	UPROPERTY()
	float SourceSmoothedMoveSpeed = 0.f;

	UPROPERTY()
	float SourceActorYaw = 0.f;

	UPROPERTY()
	float SourceDesiredFacingYaw = 0.f;

	bool operator==(const FGGMRemoteLocomotionSnapshot& Other) const
	{
		return SnapshotSequence == Other.SnapshotSequence
			&& FMath::IsNearlyEqual(MoveSpeed, Other.MoveSpeed)
			&& FMath::IsNearlyEqual(BlendForwardAxis, Other.BlendForwardAxis)
			&& FMath::IsNearlyEqual(BlendRightAxis, Other.BlendRightAxis)
			&& MoveDirection.Equals(Other.MoveDirection)
			&& FMath::IsNearlyEqual(VisualYaw, Other.VisualYaw)
			&& LocomotionGroup == Other.LocomotionGroup
			&& bIsSprinting == Other.bIsSprinting
			&& FMath::IsNearlyEqual(SourceVelocity2D, Other.SourceVelocity2D)
			&& FMath::IsNearlyEqual(SourceMaxWalkSpeed, Other.SourceMaxWalkSpeed)
			&& FMath::IsNearlyEqual(SourceSmoothedMoveSpeed, Other.SourceSmoothedMoveSpeed)
			&& FMath::IsNearlyEqual(SourceActorYaw, Other.SourceActorYaw)
			&& FMath::IsNearlyEqual(SourceDesiredFacingYaw, Other.SourceDesiredFacingYaw);
	}

	bool operator!=(const FGGMRemoteLocomotionSnapshot& Other) const
	{
		return !(*this == Other);
	}
};

struct FSavedMove_GGM : public FSavedMove_Character
{
	typedef FSavedMove_Character Super;

	uint8 SavedMoveInputIntent = 0;
	bool bSavedSprintInputPressed = false;
	bool bSavedSprintForwardHeld = false;

	virtual void Clear() override;
	virtual uint8 GetCompressedFlags() const override;
	virtual bool CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* Character, float MaxDelta) const override;
	virtual void SetMoveFor(ACharacter* Character, float InDeltaTime, const FVector& NewAccel, FNetworkPredictionData_Client_Character& ClientData) override;
	virtual void PrepMoveFor(ACharacter* Character) override;
};

struct FGGMCharacterNetworkMoveData : public FCharacterNetworkMoveData
{
	typedef FCharacterNetworkMoveData Super;

	uint8 MoveInputIntent = 0;
	uint8 bSprintInputPressed = 0;
	uint8 bSprintForwardHeld = 0;

	virtual void ClientFillNetworkMoveData(const FSavedMove_Character& ClientMove, ENetworkMoveType MoveType) override;
	virtual bool Serialize(UCharacterMovementComponent& CharacterMovement, FArchive& Ar, UPackageMap* PackageMap, ENetworkMoveType MoveType) override;
};

struct FGGMCharacterNetworkMoveDataContainer : public FCharacterNetworkMoveDataContainer
{
	FGGMCharacterNetworkMoveDataContainer();

	FGGMCharacterNetworkMoveData CustomNewMoveData;
	FGGMCharacterNetworkMoveData CustomPendingMoveData;
	FGGMCharacterNetworkMoveData CustomOldMoveData;
};

struct FNetworkPredictionData_Client_GGM : public FNetworkPredictionData_Client_Character
{
	typedef FNetworkPredictionData_Client_Character Super;

	FNetworkPredictionData_Client_GGM(const UCharacterMovementComponent& ClientMovement);

	virtual FSavedMovePtr AllocateNewMove() override;
};

UCLASS()
class GGM_API UGGMCharacterMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()

public:
	UGGMCharacterMovementComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void UpdateFromCompressedFlags(uint8 Flags) override;
	virtual FNetworkPredictionData_Client* GetPredictionData_Client() const override;
	virtual void MoveAutonomous(float ClientTimeStamp, float DeltaTime, uint8 CompressedFlags, const FVector& NewAccel) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	void SetOwnerRawMoveAxes(float InForwardAxis, float InRightAxis);
	void SetRawMoveInput(float InForwardAxis, float InRightAxis) { SetOwnerRawMoveAxes(InForwardAxis, InRightAxis); }

	void SetOwnerDesiredFacingYaw(float InDesiredFacingYaw);
	void SetDesiredFacingYaw(float InDesiredFacingYaw) { SetOwnerDesiredFacingYaw(InDesiredFacingYaw); }

	void SetSprintInputPressed(bool bPressed);
	void SetSprintForwardHeld(bool bHeld);

	void ApplyInputIntent(EGGM_MoveInputIntent InIntent);
	void ApplyCurrentReplicatedMoveData();

	float GetBlendForwardAxis() const;
	float GetBlendRightAxis() const;
	float GetMoveSpeed() const;
	float GetVisualYaw() const;
	FVector GetMoveDirection() const;

	float GetMoveForwardIntent() const { return OwnerRawForwardAxis; }
	float GetMoveRightIntent() const { return OwnerRawRightAxis; }

	void ForceStopSprint(bool bPreserveFutureIntent);
	void ApplyRemoteLocomotionSnapshot(const FGGMRemoteLocomotionSnapshot& InSnapshot);

	bool CanStartJump() const;

	EGGM_MoveInputIntent GetMoveInputIntent() const { return MoveInputIntent; }
	bool IsSprintInputPressed() const { return bSprintInputPressed; }
	bool IsSprintForwardHeld() const { return bSprintForwardHeld; }
	bool IsSprinting() const { return bIsSprinting; }
	bool IsSprintingForAnimation() const { return IsSimulatedProxyInstance() ? bRemoteVisualIsSprinting : bIsSprinting; }

	float GetAnimationRuntimeSpeed() const;
	float GetAuthorityFacingYawSource() const;

protected:
	void RefreshRuntimeMovementState();
	void RefreshRuntimeInputState();
	void RefreshRuntimeLocomotionModeState();
	void RefreshRuntimeLocomotionGroupState();
	void RefreshRuntimeMovementSpeed();
	void RefreshRuntimeMovementSnapshot();

	void TickSmoothedMovementState(float DeltaTime);
	void TickSprint(float DeltaTime);
	void UpdateAirborneState();
	void UpdateRotation(float DeltaTime);
	void ApplyMovementInput();
	void TickRemoteVisualYaw(float DeltaTime);
	void ApplyRemoteVisualYawInstant(float InVisualYaw);

	bool ShouldApplyOwnedRotation() const;
	bool IsAirborneLockActive() const;
	bool IsSprintLockActive() const;
	bool ShouldConsumeInputForGroundMovement() const;

	float GetBaseSpeed() const;
	float GetForwardBaseSpeed() const;
	float GetSprintTargetSpeed() const;

	void ExitSprint();
	void CaptureSprintLockState();
	bool ShouldExitSprintForAirborne() const;
	void BeginSprint();

	bool HasMovementInput() const;
	bool IsTravelMode() const;
	bool IsGroundedForSprint() const;
	bool HasSprintStamina() const;
	bool CanEnterSprint() const;

	float GetMovementBasisYaw() const;
	FVector GetCameraRelativeMoveDirection() const;
	FVector GetAppliedMoveDirection() const;
	float GetAppliedRotationTargetYaw() const;
	float GetNormalizedYaw(float InYaw) const;

	bool IsSimulatedProxyInstance() const;

	EGGM_MoveInputIntent BuildMoveInputIntentFromAxes(float InForwardAxis, float InRightAxis) const;
	EGGM_LocomotionGroup BuildRuntimeLocomotionGroupFromInputIntent(EGGM_MoveInputIntent InIntent) const;

	void SetDesiredFacingYawInternal(float InDesiredFacingYaw);

	FGGMRemoteLocomotionSnapshot BuildRemoteLocomotionSnapshot() const;

protected:
	UPROPERTY(Transient)
	TObjectPtr<AGGMCharacter> CachedGGMCharacter = nullptr;

	UPROPERTY(Transient)
	FGGMRuntimeLocomotionSnapshot BaseSnapshot;

	UPROPERTY(Transient)
	EGGM_MoveInputIntent MoveInputIntent = EGGM_MoveInputIntent::None;

	UPROPERTY(Transient)
	EGGM_LocomotionMode CurrentLocomotionMode = EGGM_LocomotionMode::Travel;

	UPROPERTY(Transient)
	EGGM_LocomotionGroup CurrentLocomotionGroup = EGGM_LocomotionGroup::Idle;

	UPROPERTY(Transient)
	float OwnerRawForwardAxis = 0.f;

	UPROPERTY(Transient)
	float OwnerRawRightAxis = 0.f;

	UPROPERTY(Transient)
	float DesiredFacingYaw = 0.f;

	UPROPERTY(Transient)
	bool bSprintInputPressed = false;

	UPROPERTY(Transient)
	bool bSprintForwardHeld = false;

	UPROPERTY(Transient)
	bool bIsSprinting = false;

	UPROPERTY(Transient)
	float SprintCurrentSpeed = 0.f;

	UPROPERTY(Transient)
	FVector LockedSprintMoveDirection = FVector::ZeroVector;

	UPROPERTY(Transient)
	float LockedSprintYaw = 0.f;

	UPROPERTY(Transient)
	bool bAirborneLockActive = false;

	UPROPERTY(Transient)
	bool bWasFallingLastFrame = false;

	UPROPERTY(Transient)
	FVector AirborneLockedMoveDirection = FVector::ZeroVector;

	UPROPERTY(Transient)
	float AirborneLockedYaw = 0.f;

	UPROPERTY(Transient)
	bool bSmoothedMovementInitialized = false;

	UPROPERTY(Transient)
	FVector SmoothedMoveWorldDirection = FVector::ZeroVector;

	UPROPERTY(Transient)
	float SmoothedMoveSpeed = 0.f;

	UPROPERTY(Transient)
	bool bRemoteVisualYawInitialized = false;

	UPROPERTY(Transient)
	float RemoteVisualYawTarget = 0.f;

	UPROPERTY(Transient)
	float RemoteAppliedVisualYaw = 0.f;

	UPROPERTY(Transient)
	bool bRemoteVisualIsSprinting = false;

	UPROPERTY(Transient)
	float RemoteBlendForwardAxis = 0.f;

	UPROPERTY(Transient)
	float RemoteBlendRightAxis = 0.f;

	UPROPERTY(Transient)
	float RemoteMoveSpeed = 0.f;

	UPROPERTY(Transient)
	FVector RemoteMoveDirection = FVector::ZeroVector;

	UPROPERTY(Transient)
	mutable uint16 RemoteSnapshotSequenceCounter = 0;

	UPROPERTY(EditAnywhere, Category = "GGM|Movement")
	float MovementAccelerationRate = 2400.f;

	UPROPERTY(EditAnywhere, Category = "GGM|Movement")
	float MovementDecelerationRate = 2800.f;

	UPROPERTY(EditAnywhere, Category = "GGM|Movement")
	float MovementDirectionInterpSpeed = 14.f;

	UPROPERTY(EditAnywhere, Category = "GGM|Movement")
	float MovementRotationStartSpeedDegPerSec = 4320.f;

	UPROPERTY(EditAnywhere, Category = "GGM|Movement")
	float MovementRotationMovingSpeedDegPerSec = 2880.f;

	UPROPERTY(EditAnywhere, Category = "GGM|Movement")
	float RemoteMovementVisualYawSyncSpeedDegPerSec = 1080.f;

	UPROPERTY(EditAnywhere, Category = "GGM|Movement")
	float SprintAccelerationRate = 1200.f;

	UPROPERTY(EditAnywhere, Category = "GGM|Movement")
	float SprintDecelerationRate = 1200.f;

	UPROPERTY(EditAnywhere, Category = "GGM|Movement")
	float SprintExitCompleteSpeedTolerance = 1.f;

protected:
	mutable FGGMCharacterNetworkMoveDataContainer GGMNetworkMoveDataContainer;
};