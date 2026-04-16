// GGMCharacterMovementComponent.h

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/CharacterMovementReplication.h"
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
	EGGM_LocomotionMode LocomotionMode{};

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement")
	EGGM_LocomotionGroup LocomotionGroup{};

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

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Remote")
	int32 SnapshotSequence = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Remote")
	float MoveSpeed = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Remote")
	float BlendForwardAxis = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Remote")
	float BlendRightAxis = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Remote")
	FVector_NetQuantizeNormal MoveDirection = FVector(1.f, 0.f, 0.f);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Remote")
	float VisualYaw = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Remote")
	EGGM_LocomotionGroup LocomotionGroup = EGGM_LocomotionGroup::Idle;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Remote")
	bool bIsSprinting = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Remote|Debug")
	float SourceVelocity2D = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Remote|Debug")
	float SourceMaxWalkSpeed = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Remote|Debug")
	float SourceSmoothedMoveSpeed = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Remote|Debug")
	float SourceActorYaw = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Remote|Debug")
	float SourceDesiredFacingYaw = 0.f;

	bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
	{
		Ar << SnapshotSequence;
		Ar << MoveSpeed;
		Ar << BlendForwardAxis;
		Ar << BlendRightAxis;
		MoveDirection.NetSerialize(Ar, Map, bOutSuccess);
		Ar << VisualYaw;

		uint8 LocomotionGroupByte = static_cast<uint8>(LocomotionGroup);
		uint8 SprintingBit = bIsSprinting ? 1 : 0;

		Ar << LocomotionGroupByte;
		Ar.SerializeBits(&SprintingBit, 1);

		Ar << SourceVelocity2D;
		Ar << SourceMaxWalkSpeed;
		Ar << SourceSmoothedMoveSpeed;
		Ar << SourceActorYaw;
		Ar << SourceDesiredFacingYaw;

		if (Ar.IsLoading())
		{
			LocomotionGroup = static_cast<EGGM_LocomotionGroup>(LocomotionGroupByte);
			bIsSprinting = SprintingBit != 0;
		}

		bOutSuccess = !Ar.IsError();
		return true;
	}

	bool operator==(const FGGMRemoteLocomotionSnapshot& Other) const
	{
		return FMath::IsNearlyEqual(MoveSpeed, Other.MoveSpeed)
			&& FMath::IsNearlyEqual(BlendForwardAxis, Other.BlendForwardAxis)
			&& FMath::IsNearlyEqual(BlendRightAxis, Other.BlendRightAxis)
			&& MoveDirection == Other.MoveDirection
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

template<>
struct TStructOpsTypeTraits<FGGMRemoteLocomotionSnapshot> : public TStructOpsTypeTraitsBase2<FGGMRemoteLocomotionSnapshot>
{
	enum
	{
		WithNetSerializer = true,
		WithIdenticalViaEquality = true
	};
};

class FSavedMove_GGM : public FSavedMove_Character
{
public:
	typedef FSavedMove_Character Super;

	virtual void Clear() override;
	virtual uint8 GetCompressedFlags() const override;
	virtual bool CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* Character, float MaxDelta) const override;
	virtual void SetMoveFor(ACharacter* Character, float InDeltaTime, const FVector& NewAccel, FNetworkPredictionData_Client_Character& ClientData) override;
	virtual void PrepMoveFor(ACharacter* Character) override;

public:
	uint8 SavedMoveInputIntent = 0;
	uint8 bSavedSprintInputPressed : 1;
	uint8 bSavedSprintForwardHeld : 1;
};

class FGGMCharacterNetworkMoveData : public FCharacterNetworkMoveData
{
public:
	typedef FCharacterNetworkMoveData Super;

	virtual void ClientFillNetworkMoveData(const FSavedMove_Character& ClientMove, ENetworkMoveType MoveType) override;
	virtual bool Serialize(UCharacterMovementComponent& CharacterMovement, FArchive& Ar, UPackageMap* PackageMap, ENetworkMoveType MoveType) override;

public:
	uint8 MoveInputIntent = 0;
	uint8 bSprintInputPressed = 0;
	uint8 bSprintForwardHeld = 0;
};

class FGGMCharacterNetworkMoveDataContainer : public FCharacterNetworkMoveDataContainer
{
public:
	FGGMCharacterNetworkMoveDataContainer();

public:
	FGGMCharacterNetworkMoveData CustomNewMoveData;
	FGGMCharacterNetworkMoveData CustomPendingMoveData;
	FGGMCharacterNetworkMoveData CustomOldMoveData;
};

class FNetworkPredictionData_Client_GGM : public FNetworkPredictionData_Client_Character
{
public:
	explicit FNetworkPredictionData_Client_GGM(const UCharacterMovementComponent& ClientMovement);

	typedef FNetworkPredictionData_Client_Character Super;

	virtual FSavedMovePtr AllocateNewMove() override;
};

UCLASS(BlueprintType, Blueprintable)
class GGM_API UGGMCharacterMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()

public:
	UGGMCharacterMovementComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void UpdateFromCompressedFlags(uint8 Flags) override;
	virtual class FNetworkPredictionData_Client* GetPredictionData_Client() const override;
	virtual void MoveAutonomous(float ClientTimeStamp, float DeltaTime, uint8 CompressedFlags, const FVector& NewAccel) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

public:
	void SetOwnerRawMoveAxes(float InForwardAxis, float InRightAxis);
	void SetOwnerDesiredFacingYaw(float InDesiredFacingYaw);
	void SetSprintInputPressed(bool bPressed);
	void SetSprintForwardHeld(bool bHeld);
	void ApplyInputIntent(EGGM_MoveInputIntent InIntent);
	void ApplyCurrentReplicatedMoveData();

	UFUNCTION(BlueprintCallable, Category = "Movement")
	void SetRawMoveInput(float InForwardAxis, float InRightAxis)
	{
		SetOwnerRawMoveAxes(InForwardAxis, InRightAxis);
	}

	UFUNCTION(BlueprintCallable, Category = "Movement")
	void SetDesiredFacingYaw(float InDesiredFacingYaw)
	{
		SetOwnerDesiredFacingYaw(InDesiredFacingYaw);
	}

	UFUNCTION(BlueprintPure, Category = "Movement")
	float GetMoveForwardIntent() const
	{
		return OwnerRawForwardAxis;
	}

	UFUNCTION(BlueprintPure, Category = "Movement")
	float GetMoveRightIntent() const
	{
		return OwnerRawRightAxis;
	}

	UFUNCTION(BlueprintPure, Category = "Movement")
	float GetBlendForwardAxis() const;

	UFUNCTION(BlueprintPure, Category = "Movement")
	float GetBlendRightAxis() const;

	UFUNCTION(BlueprintPure, Category = "Movement")
	float GetMoveSpeed() const;

	UFUNCTION(BlueprintPure, Category = "Movement")
	float GetVisualYaw() const;

	UFUNCTION(BlueprintPure, Category = "Movement")
	FVector GetMoveDirection() const;

	UFUNCTION(BlueprintPure, Category = "Movement|Remote")
	float GetRemoteVisualYawTarget() const
	{
		return RemoteVisualYawTarget;
	}

	UFUNCTION(BlueprintPure, Category = "Movement")
	float GetOwnerRawForwardAxis() const
	{
		return OwnerRawForwardAxis;
	}

	UFUNCTION(BlueprintPure, Category = "Movement")
	float GetOwnerRawRightAxis() const
	{
		return OwnerRawRightAxis;
	}

	UFUNCTION(BlueprintPure, Category = "Movement")
	float GetDesiredFacingYaw() const
	{
		return DesiredFacingYaw;
	}

	UFUNCTION(BlueprintPure, Category = "Movement")
	EGGM_MoveInputIntent GetMoveInputIntent() const
	{
		return MoveInputIntent;
	}

	UFUNCTION(BlueprintPure, Category = "Movement")
	EGGM_LocomotionMode GetCurrentLocomotionMode() const
	{
		return CurrentLocomotionMode;
	}

	UFUNCTION(BlueprintPure, Category = "Movement")
	EGGM_LocomotionGroup GetCurrentLocomotionGroup() const
	{
		return CurrentLocomotionGroup;
	}

	UFUNCTION(BlueprintPure, Category = "Movement|Sprint")
	bool IsSprinting() const
	{
		return IsSimulatedProxyInstance() ? bRemoteVisualIsSprinting : bIsSprinting;
	}

	UFUNCTION(BlueprintPure, Category = "Movement|Sprint")
	bool IsSprintInputPressed() const
	{
		return bSprintInputPressed;
	}

	UFUNCTION(BlueprintPure, Category = "Movement|Sprint")
	bool IsSprintForwardHeld() const
	{
		return bSprintForwardHeld;
	}

	UFUNCTION(BlueprintPure, Category = "Movement|Sprint")
	FVector GetLockedSprintMoveDirection() const
	{
		return LockedSprintMoveDirection;
	}

	UFUNCTION(BlueprintPure, Category = "Movement|Sprint")
	float GetLockedSprintYaw() const
	{
		return LockedSprintYaw;
	}

	UFUNCTION(BlueprintPure, Category = "Movement|Sprint")
	float GetSprintCurrentSpeed() const
	{
		return SprintCurrentSpeed;
	}

	UFUNCTION(BlueprintPure, Category = "Movement|Sprint")
	bool CanEnterSprint() const;

	UFUNCTION(BlueprintPure, Category = "Movement|Jump")
	bool CanStartJump() const;

	UFUNCTION(BlueprintCallable, Category = "Movement|Sprint")
	void BeginSprint();

	UFUNCTION(BlueprintCallable, Category = "Movement|Sprint")
	void ForceStopSprint(bool bPreserveFutureIntent);

	UFUNCTION(BlueprintPure, Category = "Movement|Remote")
	FGGMRemoteLocomotionSnapshot BuildRemoteLocomotionSnapshot() const;

	UFUNCTION(BlueprintCallable, Category = "Movement|Remote")
	void ApplyRemoteLocomotionSnapshot(const FGGMRemoteLocomotionSnapshot& InSnapshot);

protected:
	void RefreshRuntimeMovementState();
	void RefreshRuntimeMovementSnapshot();
	void RefreshRuntimeInputState();
	void RefreshRuntimeLocomotionModeState();
	void RefreshRuntimeLocomotionGroupState();
	void RefreshRuntimeMovementSpeed();
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
	float GetAnimationRuntimeSpeed() const;

	void ExitSprint();
	void CaptureSprintLockState();
	bool ShouldExitSprintForAirborne() const;
	bool HasMovementInput() const;
	bool IsTravelMode() const;
	bool IsGroundedForSprint() const;
	bool HasSprintStamina() const;

	FVector GetCameraRelativeMoveDirection() const;
	FVector GetAppliedMoveDirection() const;
	float GetAppliedRotationTargetYaw() const;
	float GetNormalizedYaw(float InYaw) const;
	float GetAuthorityFacingYawSource() const;
	float GetMovementBasisYaw() const;

	bool IsSimulatedProxyInstance() const;

	EGGM_MoveInputIntent BuildMoveInputIntentFromAxes(float InForwardAxis, float InRightAxis) const;
	EGGM_LocomotionGroup BuildRuntimeLocomotionGroupFromInputIntent(EGGM_MoveInputIntent InIntent) const;

	void SetDesiredFacingYawInternal(float InDesiredFacingYaw);

protected:
	UPROPERTY()
	TObjectPtr<AGGMCharacter> CachedGGMCharacter;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Intent")
	float DesiredFacingYaw = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Intent")
	float OwnerRawForwardAxis = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Intent")
	float OwnerRawRightAxis = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Intent")
	bool bSprintInputPressed = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Intent")
	bool bSprintForwardHeld = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Intent")
	EGGM_MoveInputIntent MoveInputIntent = EGGM_MoveInputIntent::None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement")
	EGGM_LocomotionMode CurrentLocomotionMode = EGGM_LocomotionMode::Travel;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement")
	EGGM_LocomotionGroup CurrentLocomotionGroup{};

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement")
	FGGMRuntimeLocomotionSnapshot BaseSnapshot;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Sprint")
	bool bIsSprinting = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Remote")
	bool bRemoteVisualIsSprinting = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Remote")
	float RemoteBlendForwardAxis = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Remote")
	float RemoteBlendRightAxis = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Remote")
	float RemoteMoveSpeed = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Remote")
	FVector RemoteMoveDirection = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Remote")
	float RemoteAppliedVisualYaw = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Sprint")
	FVector LockedSprintMoveDirection = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Sprint")
	float LockedSprintYaw = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Sprint")
	float SprintCurrentSpeed = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Sprint")
	float SprintAccelerationRate = 250.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Sprint")
	float SprintDecelerationRate = 350.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Sprint")
	float SprintExitCompleteSpeedTolerance = 2.f;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Airborne")
	bool bWasFallingLastFrame = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Airborne")
	FVector AirborneLockedMoveDirection = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Airborne")
	float AirborneLockedYaw = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Airborne")
	bool bAirborneLockActive = false;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Smoothing")
	FVector SmoothedMoveWorldDirection = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Smoothing")
	float SmoothedMoveSpeed = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Smoothing")
	bool bSmoothedMovementInitialized = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Smoothing")
	float MovementAccelerationRate = 2400.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Smoothing")
	float MovementDecelerationRate = 2800.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Smoothing")
	float MovementDirectionInterpSpeed = 14.f;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Remote")
	float RemoteVisualYawTarget = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement|Remote")
	bool bRemoteVisualYawInitialized = false;

protected:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Rotation")
	float MovementRotationStartSpeedDegPerSec = 4320.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Rotation")
	float MovementRotationMovingSpeedDegPerSec = 2880.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement|Rotation")
	float RemoteMovementVisualYawSyncSpeedDegPerSec = 1080.f;

protected:
	UPROPERTY(Transient)
	mutable int32 RemoteSnapshotSequenceCounter = 0;

	FGGMCharacterNetworkMoveDataContainer GGMNetworkMoveDataContainer;
};