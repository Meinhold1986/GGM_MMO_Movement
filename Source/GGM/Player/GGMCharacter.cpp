#include "GGMCharacter.h"
#include "Player/GGMCharacterMovementComponent.h"
#include "../Game/Combat/GGMCombatComponent.h"

#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Net/UnrealNetwork.h"
#include "Net/Core/PushModel/PushModel.h"
#include "Engine/Engine.h"

DEFINE_LOG_CATEGORY_STATIC(LogGGMAnimReplicationDebug, Log, All);

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
namespace
{
	static int32 GGM_MakePerCharacterDebugKey(const AActor* Actor, int32 BaseKey)
	{
		return BaseKey + static_cast<int32>(Actor ? (Actor->GetUniqueID() % 5000) : 0);
	}
}
#endif

AGGMCharacter::AGGMCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UGGMCharacterMovementComponent>(ACharacter::CharacterMovementComponentName))
{
	PrimaryActorTick.bCanEverTick = true;

	bReplicates = true;
	SetReplicateMovement(true);

	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	CombatComponent = CreateDefaultSubobject<UGGMCombatComponent>(TEXT("CombatComponent"));

	if (UGGMCharacterMovementComponent* MoveComp = GetGGMCharacterMovementComponent())
	{
		MoveComp->bOrientRotationToMovement = false;
		MoveComp->bUseControllerDesiredRotation = false;
		MoveComp->RotationRate = FRotator::ZeroRotator;
		MoveComp->AirControl = 0.f;
		MoveComp->MaxAcceleration = 100000.f;
		MoveComp->BrakingDecelerationWalking = 100000.f;
		MoveComp->BrakingFrictionFactor = 0.f;
		MoveComp->bUseSeparateBrakingFriction = false;
	}

	if (USkeletalMeshComponent* CharacterMesh = GetMesh())
	{
		CharacterMesh->bEnableUpdateRateOptimizations = false;
		CharacterMesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	}

	WeaponMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("WeaponMesh"));
	if (WeaponMesh)
	{
		WeaponMesh->SetupAttachment(GetMesh(), TEXT("WeaponSocket_R"));
		WeaponMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		WeaponMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
		WeaponMesh->SetGenerateOverlapEvents(false);
		WeaponMesh->SetCanEverAffectNavigation(false);
		WeaponMesh->CastShadow = true;
		WeaponMesh->SetHiddenInGame(true);
		WeaponMesh->SetVisibility(false, true);
	}

	ShieldMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ShieldMesh"));
	if (ShieldMesh)
	{
		ShieldMesh->SetupAttachment(GetMesh(), TEXT("ShieldSocket_L"));
		ShieldMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		ShieldMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
		ShieldMesh->SetGenerateOverlapEvents(false);
		ShieldMesh->SetCanEverAffectNavigation(false);
		ShieldMesh->CastShadow = true;
		ShieldMesh->SetHiddenInGame(true);
		ShieldMesh->SetVisibility(false, true);
	}
}

void AGGMCharacter::BeginPlay()
{
	Super::BeginPlay();

	CurrentHealth = FMath::Clamp(CurrentHealth, 0.f, MaxHealth);
	CurrentStamina = FMath::Clamp(CurrentStamina, 0.f, MaxStamina);
	bIsDead = CurrentHealth <= 0.f;

	RefreshDeathPresentationAndMovementState(bIsDead);
	RefreshWeaponDrawnPresentationAndMovementState();

	if (!HasAuthority() && !IsLocallyControlled())
	{
		ApplyRemoteLocomotionSnapshotToMovementComponent();

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
		UE_LOG(
			LogGGMAnimReplicationDebug,
			Warning,
			TEXT("[BeginPlay][RemoteProxy] Name=%s applied initial RemoteLocomotionSnapshot. Seq=%u Speed=%.2f Blend=(%.2f,%.2f)"),
			*GetName(),
			RemoteLocomotionSnapshot.SnapshotSequence,
			RemoteLocomotionSnapshot.MoveSpeed,
			RemoteLocomotionSnapshot.BlendForwardAxis,
			RemoteLocomotionSnapshot.BlendRightAxis
		);
#endif
	}
}

bool AGGMCharacter::CanJumpInternal_Implementation() const
{
	if (GetCurrentLocomotionMode() != EGGM_LocomotionMode::Travel)
	{
		return false;
	}

	if (!Super::CanJumpInternal_Implementation())
	{
		return false;
	}

	const UGGMCharacterMovementComponent* MoveComp = GetGGMCharacterMovementComponent();
	return MoveComp ? MoveComp->CanStartJump() : true;
}

void AGGMCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (HasAuthority() && !bIsDead && MaxStamina > 0.f && CurrentStamina < MaxStamina)
	{
		CurrentStamina = FMath::Clamp(
			CurrentStamina + (StaminaRegenPerSecond * DeltaTime),
			0.f,
			MaxStamina);
	}

	ShowDebugAllTickOverlay();
}

void AGGMCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AGGMCharacter, bFlexMode);
	DOREPLIFETIME(AGGMCharacter, MoveSpeedStats);
	DOREPLIFETIME(AGGMCharacter, CurrentHealth);
	DOREPLIFETIME(AGGMCharacter, CurrentStamina);
	DOREPLIFETIME(AGGMCharacter, bIsDead);
	DOREPLIFETIME_CONDITION_NOTIFY(AGGMCharacter, RemoteLocomotionSnapshot, COND_SimulatedOnly, REPNOTIFY_Always);
}

bool AGGMCharacter::IsWeaponDrawn() const
{
	const UGGMCombatComponent* CombatComp = GetCombatComponent();
	if (!CombatComp)
	{
		return false;
	}

	return CombatComp->IsWeaponDrawn();
}

bool AGGMCharacter::IsBlocking() const
{
	const UGGMCombatComponent* CombatComp = GetCombatComponent();
	if (!CombatComp)
	{
		return false;
	}

	return CombatComp->IsBlocking();
}

bool AGGMCharacter::IsAttacking() const
{
	const UGGMCombatComponent* CombatComp = GetCombatComponent();
	if (!CombatComp)
	{
		return false;
	}

	return CombatComp->IsAttacking();
}

void AGGMCharacter::ApplyWeaponDrawnVisuals()
{
	const bool bDrawn = IsWeaponDrawn();

	if (WeaponMesh)
	{
		WeaponMesh->SetHiddenInGame(!bDrawn);
		WeaponMesh->SetVisibility(bDrawn, true);
	}

	if (ShieldMesh)
	{
		ShieldMesh->SetHiddenInGame(!bDrawn);
		ShieldMesh->SetVisibility(bDrawn, true);
	}
}

void AGGMCharacter::RequestSetWeaponDrawn(bool bNewWeaponDrawn)
{
	if (!GetCombatComponent())
	{
		return;
	}

	const bool bAuthoritative = HasAuthority();
	ApplyRequestedWeaponDrawnStateChange(bNewWeaponDrawn, bAuthoritative);

	if (!bAuthoritative)
	{
		ServerSetWeaponDrawn(bNewWeaponDrawn);
	}
}

void AGGMCharacter::RequestSetFlexMode(bool bNewFlexMode)
{
	const bool bAuthoritative = HasAuthority();
	ApplyRequestedFlexModeStateChange(bNewFlexMode, bAuthoritative);

	if (!bAuthoritative)
	{
		ServerSetFlexMode(bNewFlexMode);
	}
}

void AGGMCharacter::ApplyOwnedFlexModeStateChange(bool bNewFlexMode, bool bAuthoritative)
{
	if ((bAuthoritative && !HasAuthority()) || (!bAuthoritative && !IsLocallyControlled()))
	{
		return;
	}

	if (bFlexMode == bNewFlexMode)
	{
		return;
	}

	bFlexMode = bNewFlexMode;
}

void AGGMCharacter::ApplyRequestedWeaponDrawnStateChange(bool bNewWeaponDrawn, bool bAuthoritative)
{
	UGGMCombatComponent* CombatComp = GetCombatComponent();
	if ((bAuthoritative && !HasAuthority()) || (!bAuthoritative && !IsLocallyControlled()) || !CombatComp)
	{
		return;
	}

	if (bNewWeaponDrawn && bFlexMode)
	{
		ApplyOwnedFlexModeStateChange(false, bAuthoritative);
	}

	CombatComp->ApplyWeaponDrawnState(bNewWeaponDrawn);
	RefreshWeaponDrawnPresentationAndMovementState();
}

void AGGMCharacter::ApplyRequestedFlexModeStateChange(bool bNewFlexMode, bool bAuthoritative)
{
	UGGMCombatComponent* CombatComp = GetCombatComponent();
	if ((bAuthoritative && !HasAuthority()) || (!bAuthoritative && !IsLocallyControlled()) || !CombatComp)
	{
		return;
	}

	if (bNewFlexMode && CombatComp->IsWeaponDrawn())
	{
		CombatComp->ApplyWeaponDrawnState(false);
		ApplyWeaponDrawnVisuals();
	}

	ApplyOwnedFlexModeStateChange(bNewFlexMode, bAuthoritative);
}

void AGGMCharacter::ServerSetWeaponDrawn_Implementation(bool bNewWeaponDrawn)
{
	ApplyRequestedWeaponDrawnStateChange(bNewWeaponDrawn, true);
}

void AGGMCharacter::ServerSetFlexMode_Implementation(bool bNewFlexMode)
{
	ApplyRequestedFlexModeStateChange(bNewFlexMode, true);
}

void AGGMCharacter::StartAttack()
{
	UGGMCombatComponent* CombatComp = GetCombatComponent();
	if (!CombatComp)
	{
		return;
	}

	CombatComp->StartAttack();
}

void AGGMCharacter::StartBlock()
{
	UGGMCombatComponent* CombatComp = GetCombatComponent();
	if (!CombatComp)
	{
		return;
	}

	CombatComp->BeginBlockInput();
}

void AGGMCharacter::StopBlock()
{
	UGGMCombatComponent* CombatComp = GetCombatComponent();
	if (!CombatComp)
	{
		return;
	}

	CombatComp->EndBlockInput();
}

void AGGMCharacter::DebugRevive_Pressed()
{
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	if (!HasAuthority())
	{
		ServerDebugRevive();
		return;
	}

	ReviveToFullServer();
#endif
}

void AGGMCharacter::ServerDebugRevive_Implementation()
{
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	ReviveToFullServer();
#endif
}

void AGGMCharacter::DebugToggleAll_Pressed()
{
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	const bool bNewEnabled = !bDebugAll;

	if (HasAuthority())
	{
		ApplyDebugAll(bNewEnabled);
	}
	else
	{
		ServerSetDebugAll(bNewEnabled);
		ApplyDebugAll(bNewEnabled);
	}

	ShowDebugScreenMessage(
		GGM_MakePerCharacterDebugKey(this, 46000),
		1.5f,
		bNewEnabled ? FColor::Green : FColor::Yellow,
		bNewEnabled ? TEXT("DEBUG ALL: ON") : TEXT("DEBUG ALL: OFF"));
#endif
}

void AGGMCharacter::ServerSetDebugAll_Implementation(bool bNewEnabled)
{
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	ApplyDebugAll(bNewEnabled);
#else
	(void)bNewEnabled;
#endif
}

void AGGMCharacter::ApplyDebugAll(bool bNewEnabled)
{
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	bDebugAll = bNewEnabled;

	ShowDebugScreenMessage(
		GGM_MakePerCharacterDebugKey(this, 46100),
		1.5f,
		bDebugAll ? FColor::Green : FColor::Yellow,
		bDebugAll ? TEXT("SERVER DEBUG ALL: ON") : TEXT("SERVER DEBUG ALL: OFF"));
#else
	(void)bNewEnabled;
#endif
}

void AGGMCharacter::ApplyLocalControllerInputLock(bool bLock)
{
	if (!IsLocallyControlled())
	{
		return;
	}

	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC)
	{
		return;
	}

	PC->SetIgnoreMoveInput(bLock);
	PC->SetIgnoreLookInput(bLock);
}

void AGGMCharacter::RefreshDeathPresentationAndMovementState(bool bNewDeadState)
{
	if (UGGMCombatComponent* CombatComp = GetCombatComponent())
	{
		CombatComp->ResetTransientCombatState();
	}

	if (UGGMCharacterMovementComponent* MoveComp = GetGGMCharacterMovementComponent())
	{
		MoveComp->SetRawMoveInput(0.f, 0.f);
	}

	SetActorEnableCollision(!bNewDeadState);
	SetActorHiddenInGame(bNewDeadState);
	ApplyLocalControllerInputLock(bNewDeadState);
}

void AGGMCharacter::OnRep_CurrentHealth()
{
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	ShowDebugEnabledScreenMessage(
		GGM_MakePerCharacterDebugKey(this, 43500),
		2.0f,
		FColor::Orange,
		FString::Printf(TEXT("TARGET %d HP NOW %.1f / %.1f"), GetUniqueID(), CurrentHealth, MaxHealth));
#endif
}

void AGGMCharacter::OnRep_CurrentStamina()
{
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	ShowDebugEnabledScreenMessage(
		GGM_MakePerCharacterDebugKey(this, 43700),
		2.0f,
		FColor::Cyan,
		FString::Printf(TEXT("TARGET %d STAMINA NOW %.1f / %.1f"), GetUniqueID(), CurrentStamina, MaxStamina));
#endif
}

void AGGMCharacter::OnRep_IsDead()
{
	RefreshDeathPresentationAndMovementState(bIsDead);

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	ShowDebugEnabledScreenMessage(
		GGM_MakePerCharacterDebugKey(this, 43600),
		2.0f,
		bIsDead ? FColor::Red : FColor::Green,
		FString::Printf(TEXT("TARGET %d DEAD=%d"), GetUniqueID(), bIsDead ? 1 : 0));
#endif
}

void AGGMCharacter::ReviveToFullServer()
{
	if (!HasAuthority())
	{
		return;
	}

	CurrentHealth = MaxHealth;
	CurrentStamina = MaxStamina;
	bIsDead = false;

	RefreshDeathPresentationAndMovementState(false);
	ServerAuthSetRemoteLocomotionSnapshot(FGGMRemoteLocomotionSnapshot());

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	ShowDebugEnabledScreenMessage(
		GGM_MakePerCharacterDebugKey(this, 43200),
		2.0f,
		FColor::Green,
		FString::Printf(
			TEXT("TARGET %d REVIVED TO FULL | HP %.1f / %.1f | STA %.1f / %.1f"),
			GetUniqueID(),
			CurrentHealth,
			MaxHealth,
			CurrentStamina,
			MaxStamina));
#endif
}

void AGGMCharacter::ServerAuthSetRemoteLocomotionSnapshot(const FGGMRemoteLocomotionSnapshot& NewSnapshot)
{
	if (!HasAuthority())
	{
		return;
	}

	if (RemoteLocomotionSnapshot == NewSnapshot)
	{
		return;
	}

	RemoteLocomotionSnapshot = NewSnapshot;
	MARK_PROPERTY_DIRTY_FROM_NAME(AGGMCharacter, RemoteLocomotionSnapshot, this);
	ForceNetUpdate();

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	UE_LOG(
		LogGGMAnimReplicationDebug,
		Warning,
		TEXT("[ServerAuthSetRemoteLocomotionSnapshot] Name=%s Seq=%u Speed=%.2f Blend=(%.2f,%.2f) Dir=(%.2f,%.2f) VisualYaw=%.2f Sprint=%d SrcVel2D=%.2f SrcMaxWalk=%.2f SrcSmooth=%.2f SrcActorYaw=%.2f SrcDesiredYaw=%.2f"),
		*GetName(),
		RemoteLocomotionSnapshot.SnapshotSequence,
		RemoteLocomotionSnapshot.MoveSpeed,
		RemoteLocomotionSnapshot.BlendForwardAxis,
		RemoteLocomotionSnapshot.BlendRightAxis,
		RemoteLocomotionSnapshot.MoveDirection.X,
		RemoteLocomotionSnapshot.MoveDirection.Y,
		RemoteLocomotionSnapshot.VisualYaw,
		RemoteLocomotionSnapshot.bIsSprinting ? 1 : 0,
		RemoteLocomotionSnapshot.SourceVelocity2D,
		RemoteLocomotionSnapshot.SourceMaxWalkSpeed,
		RemoteLocomotionSnapshot.SourceSmoothedMoveSpeed,
		RemoteLocomotionSnapshot.SourceActorYaw,
		RemoteLocomotionSnapshot.SourceDesiredFacingYaw
	);
#endif
}

void AGGMCharacter::ApplyRemoteLocomotionSnapshotToMovementComponent()
{
	if (IsLocallyControlled())
	{
		return;
	}

	UGGMCharacterMovementComponent* MoveComp = GetGGMCharacterMovementComponent();
	if (!MoveComp)
	{
		return;
	}

	MoveComp->ApplyRemoteLocomotionSnapshot(RemoteLocomotionSnapshot);
}

void AGGMCharacter::OnRep_RemoteLocomotionSnapshot()
{
	ApplyRemoteLocomotionSnapshotToMovementComponent();

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	UE_LOG(
		LogGGMAnimReplicationDebug,
		Warning,
		TEXT("[OnRep_RemoteLocomotionSnapshot] Name=%s Local=%d Seq=%u Speed=%.2f Blend=(%.2f,%.2f) Dir=(%.2f,%.2f) VisualYaw=%.2f Sprint=%d SrcVel2D=%.2f SrcMaxWalk=%.2f SrcSmooth=%.2f SrcActorYaw=%.2f SrcDesiredYaw=%.2f"),
		*GetName(),
		IsLocallyControlled() ? 1 : 0,
		RemoteLocomotionSnapshot.SnapshotSequence,
		RemoteLocomotionSnapshot.MoveSpeed,
		RemoteLocomotionSnapshot.BlendForwardAxis,
		RemoteLocomotionSnapshot.BlendRightAxis,
		RemoteLocomotionSnapshot.MoveDirection.X,
		RemoteLocomotionSnapshot.MoveDirection.Y,
		RemoteLocomotionSnapshot.VisualYaw,
		RemoteLocomotionSnapshot.bIsSprinting ? 1 : 0,
		RemoteLocomotionSnapshot.SourceVelocity2D,
		RemoteLocomotionSnapshot.SourceMaxWalkSpeed,
		RemoteLocomotionSnapshot.SourceSmoothedMoveSpeed,
		RemoteLocomotionSnapshot.SourceActorYaw,
		RemoteLocomotionSnapshot.SourceDesiredFacingYaw
	);
#endif
}

void AGGMCharacter::ShowDebugScreenMessage(int32 Key, float TimeToDisplay, const FColor& Color, const FString& Message) const
{
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(Key, TimeToDisplay, Color, Message);
	}
#else
	(void)Key;
	(void)TimeToDisplay;
	(void)Color;
	(void)Message;
#endif
}

void AGGMCharacter::ShowDebugEnabledScreenMessage(int32 Key, float TimeToDisplay, const FColor& Color, const FString& Message) const
{
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	if (!bDebugAll)
	{
		return;
	}

	ShowDebugScreenMessage(Key, TimeToDisplay, Color, Message);
#else
	(void)Key;
	(void)TimeToDisplay;
	(void)Color;
	(void)Message;
#endif
}

void AGGMCharacter::ShowDebugAllTickOverlay() const
{
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	if (!bDebugAll)
	{
		return;
	}

	const UGGMCharacterMovementComponent* MoveComp = GetGGMCharacterMovementComponent();
	const UCharacterMovementComponent* BaseMove = GetCharacterMovement();

	const bool bAuth = HasAuthority();
	const bool bLocal = IsLocallyControlled();
	const bool bSimulatedProxy = !bAuth && !bLocal;
	const int32 MovementModeValue = BaseMove ? static_cast<int32>(BaseMove->MovementMode) : -1;
	const float VelocitySize = BaseMove ? BaseMove->Velocity.Size2D() : 0.f;
	const float MaxSpeed = BaseMove ? BaseMove->MaxWalkSpeed : 0.f;
	const float VisualYaw = GetVisualYaw();
	const FVector MoveDir = MoveComp ? MoveComp->GetMoveDirection() : FVector::ZeroVector;
	const float MoveSpeed = GetCurrentMoveSpeed();
	const float BlendForward = GetBlendForwardAxis();
	const float BlendRight = GetBlendRightAxis();

	const FString Msg = FString::Printf(
		TEXT("[%s][L=%d][Sim=%d] Seq=%u Vel=%.1f Max=%.1f Dir=(%.2f,%.2f) MoveSpeed=%.1f SnapshotSpeed=%.1f Blend=(%.2f,%.2f) SnapshotBlend=(%.2f,%.2f) SnapshotSrcVel=%.1f SnapshotSrcMax=%.1f SnapshotSrcSmooth=%.1f SprintSnapshot=%d Flex=%d Weapon=%d LocMode=%d Mode=%d ActorYaw=%.2f VisualYaw=%.2f Dead=%d"),
		bAuth ? TEXT("SERVER") : TEXT("CLIENT"),
		bLocal ? 1 : 0,
		bSimulatedProxy ? 1 : 0,
		RemoteLocomotionSnapshot.SnapshotSequence,
		VelocitySize,
		MaxSpeed,
		MoveDir.X,
		MoveDir.Y,
		MoveSpeed,
		RemoteLocomotionSnapshot.MoveSpeed,
		BlendForward,
		BlendRight,
		RemoteLocomotionSnapshot.BlendForwardAxis,
		RemoteLocomotionSnapshot.BlendRightAxis,
		RemoteLocomotionSnapshot.SourceVelocity2D,
		RemoteLocomotionSnapshot.SourceMaxWalkSpeed,
		RemoteLocomotionSnapshot.SourceSmoothedMoveSpeed,
		RemoteLocomotionSnapshot.bIsSprinting ? 1 : 0,
		bFlexMode ? 1 : 0,
		IsWeaponDrawn() ? 1 : 0,
		static_cast<int32>(GetCurrentLocomotionMode()),
		MovementModeValue,
		GetActorRotation().Yaw,
		VisualYaw,
		bIsDead ? 1 : 0
	);

	ShowDebugScreenMessage(
		GGM_MakePerCharacterDebugKey(this, 10000),
		0.2f,
		bAuth ? FColor::Green : FColor::Cyan,
		Msg
	);
#else
	(void)this;
#endif
}