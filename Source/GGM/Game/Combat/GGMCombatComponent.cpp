// Game/Combat/GGMCombatComponent.cpp

#include "GGMCombatComponent.h"

#include "GGMCombatDataAsset.h"
#include "GGMWeaponDataAsset.h"
#include "../../Player/GGMCharacter.h"

#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Net/UnrealNetwork.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"
#include "CollisionQueryParams.h"
#include "TimerManager.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/Engine.h"

DEFINE_LOG_CATEGORY_STATIC(LogGGMCombatDebug, Log, All);

namespace
{
	static bool GGM_IsAttackBlockedFromFront(const AGGMCharacter* Defender, const AGGMCharacter* InstigatorCharacter)
	{
		if (!Defender || !InstigatorCharacter)
		{
			return false;
		}

		const FVector DefenderForward = FVector(
			Defender->GetActorForwardVector().X,
			Defender->GetActorForwardVector().Y,
			0.f).GetSafeNormal();

		const FVector ToAttacker = FVector(
			InstigatorCharacter->GetActorLocation().X - Defender->GetActorLocation().X,
			InstigatorCharacter->GetActorLocation().Y - Defender->GetActorLocation().Y,
			0.f).GetSafeNormal();

		if (DefenderForward.IsNearlyZero() || ToAttacker.IsNearlyZero())
		{
			return false;
		}

		constexpr float BlockHalfAngleDeg = 60.f;
		const float MinDot = FMath::Cos(FMath::DegreesToRadians(BlockHalfAngleDeg));
		const float Dot = FVector::DotProduct(DefenderForward, ToAttacker);

		return Dot >= MinDot;
	}
}

UGGMCombatComponent::UGGMCombatComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UGGMCombatComponent::BeginPlay()
{
	Super::BeginPlay();

	OwnerCharacter = Cast<AGGMCharacter>(GetOwner());
	InitializeCombatTuningCache();
}

void UGGMCombatComponent::InitializeCombatTuningCache()
{
	CachedTuning = FGGMCombatTuningCache();

	UGGMCombatDataAsset* EffectiveCombatData = CombatData;
	if (EquippedWeaponData && EquippedWeaponData->CombatTuning)
	{
		EffectiveCombatData = EquippedWeaponData->CombatTuning;
	}

	if (EffectiveCombatData)
	{
		CachedTuning.AttackDuration = EffectiveCombatData->AttackDuration;
		CachedTuning.BlockLockAfterAttack = EffectiveCombatData->BlockLockAfterAttack;
		CachedTuning.AttackRange = EffectiveCombatData->AttackRange;
		CachedTuning.AttackConeHalfAngleDeg = EffectiveCombatData->AttackConeHalfAngleDeg;
		CachedTuning.AttackHitRadius = EffectiveCombatData->AttackHitRadius;
		CachedTuning.AttackDamage = EffectiveCombatData->AttackDamage;
		CachedTuning.AttackHitDelay = EffectiveCombatData->AttackHitDelay;
		CachedTuning.BlockMoveSpeed = EffectiveCombatData->BlockMoveSpeed;
		CachedTuning.HitFleshSound = EffectiveCombatData->HitFleshSound;
		CachedTuning.HitBlockSound = EffectiveCombatData->HitBlockSound;
		CachedTuning.AttackLockOnBlockedHit = EffectiveCombatData->AttackLockOnBlockedHit;
		CachedTuning.BlockLockOnBlockedHit = EffectiveCombatData->BlockLockOnBlockedHit;
	}

	if (EquippedWeaponData)
	{
		if (EquippedWeaponData->HitFleshSound)
		{
			CachedTuning.HitFleshSound = EquippedWeaponData->HitFleshSound;
		}

		if (EquippedWeaponData->HitBlockSound)
		{
			CachedTuning.HitBlockSound = EquippedWeaponData->HitBlockSound;
		}
	}

	UE_LOG(
		LogGGMCombatDebug,
		Warning,
		TEXT("InitializeCombatTuningCache | Owner=%s EquippedWeaponData=%d CombatData=%d LightMontage=%d HeavyMontage=%d BlockMontage=%d AttackDuration=%.3f AttackHitDelay=%.3f AttackRange=%.3f AttackHitRadius=%.3f AttackConeHalfAngleDeg=%.3f AttackDamage=%.3f"),
		OwnerCharacter ? *OwnerCharacter->GetName() : TEXT("None"),
		EquippedWeaponData ? 1 : 0,
		EffectiveCombatData ? 1 : 0,
		(EquippedWeaponData && EquippedWeaponData->LightAttack.Montage) ? 1 : 0,
		(EquippedWeaponData && EquippedWeaponData->HeavyAttack.Montage) ? 1 : 0,
		(EquippedWeaponData && EquippedWeaponData->BlockMontage) ? 1 : 0,
		CachedTuning.AttackDuration,
		CachedTuning.AttackHitDelay,
		CachedTuning.AttackRange,
		CachedTuning.AttackHitRadius,
		CachedTuning.AttackConeHalfAngleDeg,
		CachedTuning.AttackDamage);
}

void UGGMCombatComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UGGMCombatComponent, CombatState);
	DOREPLIFETIME_CONDITION(UGGMCombatComponent, AttackMontageRepCounter, COND_SkipOwner);
}

bool UGGMCombatComponent::IsWeaponDrawn() const
{
	return CombatState != EGGMCombatState::Unarmed;
}

bool UGGMCombatComponent::IsBlocking() const
{
	return CombatState == EGGMCombatState::Blocking;
}

bool UGGMCombatComponent::IsAttacking() const
{
	return CombatState == EGGMCombatState::Attacking;
}

EGGMCombatState UGGMCombatComponent::GetCombatState() const
{
	return CombatState;
}

UGGMWeaponDataAsset* UGGMCombatComponent::GetEquippedWeaponData() const
{
	return EquippedWeaponData;
}

float UGGMCombatComponent::GetBlockMoveSpeed() const
{
	return FMath::Max(0.f, CachedTuning.BlockMoveSpeed);
}

const FGGMAttackDefinition* UGGMCombatComponent::GetResolvedAttackDefinition(EGGMAttackType AttackType) const
{
	if (!EquippedWeaponData)
	{
		return nullptr;
	}

	switch (AttackType)
	{
	case EGGMAttackType::Heavy:
		return &EquippedWeaponData->HeavyAttack;

	case EGGMAttackType::Light:
	default:
		return &EquippedWeaponData->LightAttack;
	}
}

UAnimMontage* UGGMCombatComponent::GetResolvedAttackMontage() const
{
	const FGGMAttackDefinition* AttackDefinition = GetResolvedAttackDefinition(CurrentAttackType);
	return AttackDefinition ? AttackDefinition->Montage : nullptr;
}

float UGGMCombatComponent::GetResolvedAttackDamage() const
{
	const FGGMAttackDefinition* AttackDefinition = GetResolvedAttackDefinition(CurrentAttackType);
	if (!AttackDefinition)
	{
		return CachedTuning.AttackDamage;
	}

	return CachedTuning.AttackDamage * AttackDefinition->DamageMultiplier;
}

float UGGMCombatComponent::GetResolvedAttackStaminaCost(EGGMAttackType AttackType) const
{
	const FGGMAttackDefinition* AttackDefinition = GetResolvedAttackDefinition(AttackType);
	return AttackDefinition ? FMath::Max(0.f, AttackDefinition->StaminaCost) : 0.f;
}

float UGGMCombatComponent::GetResolvedAttackHitDelay(EGGMAttackType AttackType) const
{
	const FGGMAttackDefinition* AttackDefinition = GetResolvedAttackDefinition(AttackType);
	if (AttackDefinition && AttackDefinition->HitDelayOverride > 0.f)
	{
		return AttackDefinition->HitDelayOverride;
	}

	return CachedTuning.AttackHitDelay;
}

float UGGMCombatComponent::GetResolvedAttackDuration(EGGMAttackType AttackType) const
{
	const FGGMAttackDefinition* AttackDefinition = GetResolvedAttackDefinition(AttackType);
	if (AttackDefinition && AttackDefinition->DurationOverride > 0.f)
	{
		return AttackDefinition->DurationOverride;
	}

	return CachedTuning.AttackDuration;
}

float UGGMCombatComponent::GetResolvedAttackRange(EGGMAttackType AttackType) const
{
	const FGGMAttackDefinition* AttackDefinition = GetResolvedAttackDefinition(AttackType);
	if (AttackDefinition && AttackDefinition->RangeOverride > 0.f)
	{
		return AttackDefinition->RangeOverride;
	}

	return CachedTuning.AttackRange;
}

float UGGMCombatComponent::GetResolvedAttackHitRadius(EGGMAttackType AttackType) const
{
	const FGGMAttackDefinition* AttackDefinition = GetResolvedAttackDefinition(AttackType);
	if (AttackDefinition && AttackDefinition->HitRadiusOverride > 0.f)
	{
		return AttackDefinition->HitRadiusOverride;
	}

	return CachedTuning.AttackHitRadius;
}

float UGGMCombatComponent::GetResolvedAttackConeHalfAngleDeg(EGGMAttackType AttackType) const
{
	const FGGMAttackDefinition* AttackDefinition = GetResolvedAttackDefinition(AttackType);
	if (AttackDefinition && AttackDefinition->ConeHalfAngleOverride > 0.f)
	{
		return AttackDefinition->ConeHalfAngleOverride;
	}

	return CachedTuning.AttackConeHalfAngleDeg;
}

UAnimMontage* UGGMCombatComponent::GetResolvedBlockMontage() const
{
	return EquippedWeaponData ? EquippedWeaponData->BlockMontage : nullptr;
}

float UGGMCombatComponent::GetBlockLockAfterAttack() const
{
	return CachedTuning.BlockLockAfterAttack;
}

float UGGMCombatComponent::GetAttackLockOnBlockedHit() const
{
	return CachedTuning.AttackLockOnBlockedHit;
}

float UGGMCombatComponent::GetBlockLockOnBlockedHit() const
{
	return CachedTuning.BlockLockOnBlockedHit;
}

void UGGMCombatComponent::SetCombatState(EGGMCombatState NewState)
{
	UE_LOG(
		LogGGMCombatDebug,
		Warning,
		TEXT("SetCombatState CALLED | Old=%d New=%d"),
		static_cast<int32>(CombatState),
		static_cast<int32>(NewState));

	if (CombatState == NewState)
	{
		UE_LOG(LogGGMCombatDebug, Warning, TEXT("SetCombatState EARLY RETURN | same state"));
		return;
	}

	const bool bWasWeaponDrawn = IsWeaponDrawn();
	const bool bWasBlocking = IsBlocking();

	CombatState = NewState;

	const bool bIsWeaponNowDrawn = IsWeaponDrawn();
	const bool bIsBlockingNow = IsBlocking();

	UE_LOG(
		LogGGMCombatDebug,
		Warning,
		TEXT("SetCombatState APPLIED | bWasWeaponDrawn=%d bIsWeaponNowDrawn=%d bWasBlocking=%d bIsBlockingNow=%d"),
		bWasWeaponDrawn ? 1 : 0,
		bIsWeaponNowDrawn ? 1 : 0,
		bWasBlocking ? 1 : 0,
		bIsBlockingNow ? 1 : 0);

	if (bWasBlocking != bIsBlockingNow)
	{
		if (bIsBlockingNow)
		{
			UE_LOG(LogGGMCombatDebug, Warning, TEXT("SetCombatState -> PlayBlockMontageLocal"));
			PlayBlockMontageLocal();
		}
		else
		{
			UE_LOG(LogGGMCombatDebug, Warning, TEXT("SetCombatState -> StopBlockMontageLocal"));
			StopBlockMontageLocal();
		}
	}

	if (bWasWeaponDrawn != bIsWeaponNowDrawn)
	{
		UE_LOG(LogGGMCombatDebug, Warning, TEXT("SetCombatState -> NotifyOwnerWeaponDrawnChanged"));
		NotifyOwnerWeaponDrawnChanged();
	}
}

void UGGMCombatComponent::SetAttacking(bool bNewAttacking)
{
	if (bNewAttacking)
	{
		SetCombatState(EGGMCombatState::Attacking);
	}
	else
	{
		SetCombatState(IsWeaponDrawn() ? EGGMCombatState::WeaponIdle : EGGMCombatState::Unarmed);
	}
}

bool UGGMCombatComponent::CanStartAttackAuthoritative() const
{
	const bool bHasOwner = (OwnerCharacter != nullptr);
	const bool bHasAuthority = (OwnerCharacter && OwnerCharacter->HasAuthority());
	const bool bAlive = (OwnerCharacter && OwnerCharacter->IsAlive());
	const bool bStateOk = (CombatState == EGGMCombatState::WeaponIdle);
	const bool bAttackLockClear = !bAttackLockedFromBlockedHit;

	const bool bResult = bHasOwner && bHasAuthority && bAlive && bStateOk && bAttackLockClear;

	UE_LOG(
		LogGGMCombatDebug,
		Warning,
		TEXT("CanStartAttackAuthoritative | Result=%d HasOwner=%d HasAuthority=%d Alive=%d CombatState=%d IsWeaponIdle=%d AttackLockedFromBlockedHit=%d"),
		bResult ? 1 : 0,
		bHasOwner ? 1 : 0,
		bHasAuthority ? 1 : 0,
		bAlive ? 1 : 0,
		static_cast<int32>(CombatState),
		bStateOk ? 1 : 0,
		bAttackLockedFromBlockedHit ? 1 : 0);

	return bResult;
}

bool UGGMCombatComponent::CanStartBlockAuthoritative() const
{
	if (!OwnerCharacter || !OwnerCharacter->HasAuthority() || !OwnerCharacter->IsAlive())
	{
		return false;
	}

	if (CombatState != EGGMCombatState::WeaponIdle)
	{
		return false;
	}

	if (bBlockLockedFromAttack || bBlockLockedFromBlockedHit)
	{
		return false;
	}

	return true;
}

bool UGGMCombatComponent::HasEnoughStaminaForAttack(EGGMAttackType AttackType) const
{
	if (!OwnerCharacter)
	{
		UE_LOG(LogGGMCombatDebug, Warning, TEXT("HasEnoughStaminaForAttack | OwnerCharacter missing"));
		return false;
	}

	const float RequiredStamina = GetResolvedAttackStaminaCost(AttackType);
	const bool bEnough = (RequiredStamina <= 0.f || OwnerCharacter->CurrentStamina >= RequiredStamina);

	UE_LOG(
		LogGGMCombatDebug,
		Warning,
		TEXT("HasEnoughStaminaForAttack | AttackType=%d CurrentStamina=%.3f RequiredStamina=%.3f Result=%d"),
		static_cast<int32>(AttackType),
		OwnerCharacter->CurrentStamina,
		RequiredStamina,
		bEnough ? 1 : 0);

	return bEnough;
}

void UGGMCombatComponent::SpendAttackStaminaAuthoritative(EGGMAttackType AttackType)
{
	if (!OwnerCharacter || !OwnerCharacter->HasAuthority())
	{
		return;
	}

	const float RequiredStamina = GetResolvedAttackStaminaCost(AttackType);
	if (RequiredStamina <= 0.f)
	{
		return;
	}

	OwnerCharacter->CurrentStamina = FMath::Clamp(
		OwnerCharacter->CurrentStamina - RequiredStamina,
		0.f,
		OwnerCharacter->MaxStamina);
}

void UGGMCombatComponent::StartAttack(EGGMAttackType AttackType)
{
	UE_LOG(
		LogGGMCombatDebug,
		Warning,
		TEXT("StartAttack CALLED | OwnerCharacter=%d Alive=%d OwnerRole=%d CombatState=%d AttackType=%d"),
		OwnerCharacter ? 1 : 0,
		(OwnerCharacter && OwnerCharacter->IsAlive()) ? 1 : 0,
		static_cast<int32>(GetOwnerRole()),
		static_cast<int32>(CombatState),
		static_cast<int32>(AttackType));

	if (!OwnerCharacter || !OwnerCharacter->IsAlive())
	{
		UE_LOG(LogGGMCombatDebug, Warning, TEXT("StartAttack EARLY RETURN | invalid owner or dead"));
		return;
	}

	if (GetOwnerRole() == ROLE_Authority)
	{
		UE_LOG(LogGGMCombatDebug, Warning, TEXT("StartAttack -> StartAttackAuthoritative DIRECT"));
		StartAttackAuthoritative(AttackType);
		return;
	}

	UE_LOG(LogGGMCombatDebug, Warning, TEXT("StartAttack -> ServerStartAttack"));
	ServerStartAttack(AttackType);
}

void UGGMCombatComponent::ServerStartAttack_Implementation(EGGMAttackType AttackType)
{
	UE_LOG(
		LogGGMCombatDebug,
		Warning,
		TEXT("ServerStartAttack_Implementation | AttackType=%d Owner=%s"),
		static_cast<int32>(AttackType),
		OwnerCharacter ? *OwnerCharacter->GetName() : TEXT("None"));

	StartAttackAuthoritative(AttackType);
}

void UGGMCombatComponent::StartAttackAuthoritative(EGGMAttackType AttackType)
{
	const FGGMAttackDefinition* AttackDefinition = GetResolvedAttackDefinition(AttackType);
	UAnimMontage* RequestedMontage = AttackDefinition ? AttackDefinition->Montage : nullptr;
	const float RequestedDamageMultiplier = AttackDefinition ? AttackDefinition->DamageMultiplier : -1.f;
	const float RequestedStaminaCost = AttackDefinition ? AttackDefinition->StaminaCost : -1.f;
	const float RequestedHitDelayOverride = AttackDefinition ? AttackDefinition->HitDelayOverride : -1.f;
	const float RequestedDurationOverride = AttackDefinition ? AttackDefinition->DurationOverride : -1.f;
	const float RequestedRangeOverride = AttackDefinition ? AttackDefinition->RangeOverride : -1.f;
	const float RequestedHitRadiusOverride = AttackDefinition ? AttackDefinition->HitRadiusOverride : -1.f;
	const float RequestedConeOverride = AttackDefinition ? AttackDefinition->ConeHalfAngleOverride : -1.f;

	UE_LOG(
		LogGGMCombatDebug,
		Warning,
		TEXT("StartAttackAuthoritative CALLED | AttackType=%d CombatState=%d CurrentAttackType(before)=%d CanStart=%d HasEnoughStamina=%d RequestedAttackDefinition=%d RequestedMontage=%d DamageMultiplier=%.3f StaminaCost=%.3f HitDelayOverride=%.3f DurationOverride=%.3f RangeOverride=%.3f HitRadiusOverride=%.3f ConeHalfAngleOverride=%.3f"),
		static_cast<int32>(AttackType),
		static_cast<int32>(CombatState),
		static_cast<int32>(CurrentAttackType),
		CanStartAttackAuthoritative() ? 1 : 0,
		HasEnoughStaminaForAttack(AttackType) ? 1 : 0,
		AttackDefinition ? 1 : 0,
		RequestedMontage ? 1 : 0,
		RequestedDamageMultiplier,
		RequestedStaminaCost,
		RequestedHitDelayOverride,
		RequestedDurationOverride,
		RequestedRangeOverride,
		RequestedHitRadiusOverride,
		RequestedConeOverride);

	if (!CanStartAttackAuthoritative())
	{
		UE_LOG(
			LogGGMCombatDebug,
			Warning,
			TEXT("StartAttackAuthoritative EARLY RETURN | Owner=%d HasAuthority=%d Alive=%d CombatState=%d AttackLocked=%d"),
			OwnerCharacter ? 1 : 0,
			(OwnerCharacter && OwnerCharacter->HasAuthority()) ? 1 : 0,
			(OwnerCharacter && OwnerCharacter->IsAlive()) ? 1 : 0,
			static_cast<int32>(CombatState),
			bAttackLockedFromBlockedHit ? 1 : 0);
		return;
	}

	if (!HasEnoughStaminaForAttack(AttackType))
	{
		UE_LOG(LogGGMCombatDebug, Warning, TEXT("StartAttackAuthoritative EARLY RETURN | not enough stamina"));
		return;
	}

	CurrentAttackType = AttackType;

	UE_LOG(
		LogGGMCombatDebug,
		Warning,
		TEXT("StartAttackAuthoritative AFTER SET TYPE | CurrentAttackType=%d ResolvedMontageAfterSet=%d ResolvedDamage=%.3f ResolvedHitDelay=%.3f ResolvedDuration=%.3f ResolvedRange=%.3f ResolvedHitRadius=%.3f ResolvedConeHalfAngleDeg=%.3f"),
		static_cast<int32>(CurrentAttackType),
		GetResolvedAttackMontage() ? 1 : 0,
		GetResolvedAttackDamage(),
		GetResolvedAttackHitDelay(AttackType),
		GetResolvedAttackDuration(AttackType),
		GetResolvedAttackRange(AttackType),
		GetResolvedAttackHitRadius(AttackType),
		GetResolvedAttackConeHalfAngleDeg(AttackType));

	SpendAttackStaminaAuthoritative(AttackType);

	StartBlockLockAfterAttack();
	SetAttacking(true);

	bAttackHitCommitted = false;

	++AttackMontageRepCounter;
	if (AttackMontageRepCounter == 0)
	{
		++AttackMontageRepCounter;
	}

	UE_LOG(
		LogGGMCombatDebug,
		Warning,
		TEXT("StartAttackAuthoritative SUCCESS | CurrentAttackType=%d AttackMontageRepCounter=%u CombatState=%d"),
		static_cast<int32>(CurrentAttackType),
		AttackMontageRepCounter,
		static_cast<int32>(CombatState));

	GetOwner()->ForceNetUpdate();
	MulticastPlayAttackMontage();

	OwnerCharacter->GetWorldTimerManager().ClearTimer(AttackHitTimerHandle);
	OwnerCharacter->GetWorldTimerManager().SetTimer(
		AttackHitTimerHandle,
		this,
		&UGGMCombatComponent::CommitAttackHitAuthoritative,
		FMath::Max(0.01f, GetResolvedAttackHitDelay(AttackType)),
		false);

	OwnerCharacter->GetWorldTimerManager().ClearTimer(AttackTimerHandle);
	OwnerCharacter->GetWorldTimerManager().SetTimer(
		AttackTimerHandle,
		this,
		&UGGMCombatComponent::FinishAttackAuthoritative,
		FMath::Max(0.01f, GetResolvedAttackDuration(AttackType)),
		false);

	ClientConfirmAttack();
}

void UGGMCombatComponent::CommitAttackHitAuthoritative()
{
	if (!OwnerCharacter || !OwnerCharacter->HasAuthority() || !IsAttacking() || !OwnerCharacter->IsAlive())
	{
		UE_LOG(
			LogGGMCombatDebug,
			Warning,
			TEXT("CommitAttackHitAuthoritative EARLY RETURN | Owner=%d HasAuthority=%d IsAttacking=%d Alive=%d"),
			OwnerCharacter ? 1 : 0,
			(OwnerCharacter && OwnerCharacter->HasAuthority()) ? 1 : 0,
			IsAttacking() ? 1 : 0,
			(OwnerCharacter && OwnerCharacter->IsAlive()) ? 1 : 0);
		return;
	}

	if (bAttackHitCommitted)
	{
		UE_LOG(LogGGMCombatDebug, Warning, TEXT("CommitAttackHitAuthoritative EARLY RETURN | already committed"));
		return;
	}

	bAttackHitCommitted = true;
	UE_LOG(LogGGMCombatDebug, Warning, TEXT("CommitAttackHitAuthoritative -> PerformAttackHitTest"));
	PerformAttackHitTest();
}

void UGGMCombatComponent::FinishAttackAuthoritative()
{
	if (!OwnerCharacter || !OwnerCharacter->HasAuthority())
	{
		return;
	}

	UE_LOG(
		LogGGMCombatDebug,
		Warning,
		TEXT("FinishAttackAuthoritative | CombatState(before)=%d"),
		static_cast<int32>(CombatState));

	SetAttacking(false);
	bAttackHitCommitted = false;

	OwnerCharacter->GetWorldTimerManager().ClearTimer(AttackTimerHandle);
	OwnerCharacter->GetWorldTimerManager().ClearTimer(AttackHitTimerHandle);
}

void UGGMCombatComponent::PerformAttackHitTest()
{
	if (!OwnerCharacter || !OwnerCharacter->HasAuthority())
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const FVector Start = OwnerCharacter->GetActorLocation();
	const FVector AttackDir = FVector(
		OwnerCharacter->GetActorForwardVector().X,
		OwnerCharacter->GetActorForwardVector().Y,
		0.f).GetSafeNormal();

	const float ResolvedAttackRange = GetResolvedAttackRange(CurrentAttackType);
	const float ResolvedAttackHitRadius = GetResolvedAttackHitRadius(CurrentAttackType);
	const float ResolvedAttackConeHalfAngleDeg = GetResolvedAttackConeHalfAngleDeg(CurrentAttackType);

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(GGM_AttackHitTest), false, OwnerCharacter);
	QueryParams.AddIgnoredActor(OwnerCharacter);

	TArray<FOverlapResult> Overlaps;
	const bool bAnyHit = World->OverlapMultiByChannel(
		Overlaps,
		Start,
		FQuat::Identity,
		ECC_Pawn,
		FCollisionShape::MakeSphere(ResolvedAttackRange),
		QueryParams);

	AGGMCharacter* BestTarget = nullptr;
	float BestDistSq = TNumericLimits<float>::Max();
	float BestDot = -1.f;

	const float ConeHalfAngleRad = FMath::DegreesToRadians(
		FMath::Clamp(ResolvedAttackConeHalfAngleDeg, 1.f, 89.f));
	const float MinDot = FMath::Cos(ConeHalfAngleRad);

	const float DistanceTieToleranceSq = 25.f;
	const float MaxHitDistSq = FMath::Square(ResolvedAttackHitRadius);

	UE_LOG(
		LogGGMCombatDebug,
		Warning,
		TEXT("PerformAttackHitTest | AnyHit=%d OverlapCount=%d Range=%.3f HitRadius=%.3f MaxHitDistSq=%.3f ConeHalfAngleDeg=%.3f MinDot=%.3f AttackDir=(%.3f,%.3f,%.3f)"),
		bAnyHit ? 1 : 0,
		Overlaps.Num(),
		ResolvedAttackRange,
		ResolvedAttackHitRadius,
		MaxHitDistSq,
		ResolvedAttackConeHalfAngleDeg,
		MinDot,
		AttackDir.X,
		AttackDir.Y,
		AttackDir.Z);

	if (bAnyHit)
	{
		for (const FOverlapResult& Overlap : Overlaps)
		{
			AActor* HitActor = Overlap.GetActor();
			if (!HitActor || HitActor == OwnerCharacter)
			{
				continue;
			}

			AGGMCharacter* OtherCharacter = Cast<AGGMCharacter>(HitActor);
			if (!OtherCharacter || !OtherCharacter->IsAlive())
			{
				continue;
			}

			const FVector ToTarget3D = OtherCharacter->GetActorLocation() - Start;
			const FVector ToTarget2D(ToTarget3D.X, ToTarget3D.Y, 0.f);
			const float DistSq = ToTarget2D.SizeSquared();

			if (DistSq <= KINDA_SMALL_NUMBER || DistSq > MaxHitDistSq)
			{
				UE_LOG(
					LogGGMCombatDebug,
					Warning,
					TEXT("PerformAttackHitTest SKIP TARGET | Target=%s DistSq=%.3f Reason=%s"),
					*OtherCharacter->GetName(),
					DistSq,
					(DistSq <= KINDA_SMALL_NUMBER) ? TEXT("TooClose") : TEXT("OutOfHitRadius"));
				continue;
			}

			const FVector ToTargetDir = ToTarget2D.GetSafeNormal();
			const float Dot = FVector::DotProduct(AttackDir, ToTargetDir);
			if (Dot < MinDot)
			{
				UE_LOG(
					LogGGMCombatDebug,
					Warning,
					TEXT("PerformAttackHitTest SKIP TARGET | Target=%s Dot=%.3f MinDot=%.3f Reason=OutsideCone"),
					*OtherCharacter->GetName(),
					Dot,
					MinDot);
				continue;
			}

			const bool bStrictlyCloser = DistSq < (BestDistSq - DistanceTieToleranceSq);
			const bool bSameDistanceBucket = FMath::Abs(DistSq - BestDistSq) <= DistanceTieToleranceSq;

			UE_LOG(
				LogGGMCombatDebug,
				Warning,
				TEXT("PerformAttackHitTest VALID TARGET | Target=%s DistSq=%.3f Dot=%.3f StrictlyCloser=%d SameBucket=%d"),
				*OtherCharacter->GetName(),
				DistSq,
				Dot,
				bStrictlyCloser ? 1 : 0,
				bSameDistanceBucket ? 1 : 0);

			if (BestTarget == nullptr || bStrictlyCloser || (bSameDistanceBucket && Dot > BestDot))
			{
				BestTarget = OtherCharacter;
				BestDistSq = DistSq;
				BestDot = Dot;
			}
		}
	}

	if (BestTarget)
	{
		UE_LOG(
			LogGGMCombatDebug,
			Warning,
			TEXT("PerformAttackHitTest BEST TARGET | Target=%s BestDistSq=%.3f BestDot=%.3f"),
			*BestTarget->GetName(),
			BestDistSq,
			BestDot);

		HandleMeleeHitServer(BestTarget);
	}
	else
	{
		UE_LOG(LogGGMCombatDebug, Warning, TEXT("PerformAttackHitTest RESULT | No valid target"));
	}
}

void UGGMCombatComponent::HandleMeleeHitServer(AGGMCharacter* HitTarget)
{
	if (!OwnerCharacter || !OwnerCharacter->HasAuthority() || !HitTarget || !HitTarget->IsAlive())
	{
		return;
	}

	const bool bWasBlocked = ApplyDamageToTargetAuthoritative(HitTarget, GetResolvedAttackDamage());

	UE_LOG(
		LogGGMCombatDebug,
		Warning,
		TEXT("HandleMeleeHitServer | Target=%s WasBlocked=%d Damage=%.3f"),
		*HitTarget->GetName(),
		bWasBlocked ? 1 : 0,
		GetResolvedAttackDamage());

	MulticastPlayHitSound(bWasBlocked, HitTarget->GetActorLocation());

	if (bWasBlocked)
	{
		ApplyBlockedHitPunishToOwner();
	}
}

bool UGGMCombatComponent::ApplyDamageToTargetAuthoritative(AGGMCharacter* HitTarget, float DamageAmount)
{
	if (!OwnerCharacter || !OwnerCharacter->HasAuthority() || !HitTarget || !HitTarget->IsAlive() || DamageAmount <= 0.f)
	{
		return false;
	}

	bool bBlockedFromFront = false;
	float FinalDamage = DamageAmount;

	if (HitTarget->CombatComponent && HitTarget->CombatComponent->IsBlocking())
	{
		bBlockedFromFront = GGM_IsAttackBlockedFromFront(HitTarget, OwnerCharacter);
		if (bBlockedFromFront)
		{
			FinalDamage = 0.f;
		}
	}

	UE_LOG(
		LogGGMCombatDebug,
		Warning,
		TEXT("ApplyDamageToTargetAuthoritative | Target=%s Blocking=%d BlockedFromFront=%d DamageAmount=%.3f FinalDamage=%.3f HealthBefore=%.3f"),
		*HitTarget->GetName(),
		(HitTarget->CombatComponent && HitTarget->CombatComponent->IsBlocking()) ? 1 : 0,
		bBlockedFromFront ? 1 : 0,
		DamageAmount,
		FinalDamage,
		HitTarget->CurrentHealth);

	HitTarget->CurrentHealth = FMath::Clamp(
		HitTarget->CurrentHealth - FinalDamage,
		0.f,
		HitTarget->MaxHealth);

	if (HitTarget->CurrentHealth <= 0.f)
	{
		HitTarget->CurrentHealth = 0.f;
		HitTarget->bIsDead = true;
		HitTarget->RefreshDeathPresentationAndMovementState(true);
	}

	UE_LOG(
		LogGGMCombatDebug,
		Warning,
		TEXT("ApplyDamageToTargetAuthoritative RESULT | Target=%s HealthAfter=%.3f Dead=%d"),
		*HitTarget->GetName(),
		HitTarget->CurrentHealth,
		HitTarget->bIsDead ? 1 : 0);

	return bBlockedFromFront;
}

void UGGMCombatComponent::CancelAttackFlow()
{
	if (!OwnerCharacter)
	{
		return;
	}

	SetAttacking(false);
	bAttackHitCommitted = false;

	OwnerCharacter->GetWorldTimerManager().ClearTimer(AttackTimerHandle);
	OwnerCharacter->GetWorldTimerManager().ClearTimer(AttackHitTimerHandle);
}

void UGGMCombatComponent::ResetTransientCombatState()
{
	if (!OwnerCharacter)
	{
		return;
	}

	const bool bWasWeaponDrawn = IsWeaponDrawn();

	CancelAttackFlow();

	bWantsBlockHeld = false;
	bBlockLockedFromAttack = false;
	bAttackLockedFromBlockedHit = false;
	bBlockLockedFromBlockedHit = false;
	CurrentAttackType = EGGMAttackType::Light;

	OwnerCharacter->GetWorldTimerManager().ClearTimer(BlockLockTimerHandle);
	OwnerCharacter->GetWorldTimerManager().ClearTimer(AttackBlockedHitLockTimerHandle);
	OwnerCharacter->GetWorldTimerManager().ClearTimer(BlockBlockedHitLockTimerHandle);

	SetCombatState(bWasWeaponDrawn ? EGGMCombatState::WeaponIdle : EGGMCombatState::Unarmed);
}

void UGGMCombatComponent::ApplyWeaponDrawnState(bool bNewWeaponDrawn)
{
	if (!OwnerCharacter || !OwnerCharacter->IsAlive() || IsAttacking())
	{
		return;
	}

	SetCombatState(bNewWeaponDrawn ? EGGMCombatState::WeaponIdle : EGGMCombatState::Unarmed);
}

void UGGMCombatComponent::BeginBlockInput()
{
	UE_LOG(
		LogGGMCombatDebug,
		Warning,
		TEXT("BeginBlockInput CALLED | Owner=%d Alive=%d Role=%d CombatState=%d IsBlocking=%d"),
		OwnerCharacter ? 1 : 0,
		(OwnerCharacter && OwnerCharacter->IsAlive()) ? 1 : 0,
		static_cast<int32>(GetOwnerRole()),
		static_cast<int32>(CombatState),
		IsBlocking() ? 1 : 0);

	bWantsBlockHeld = true;

	if (!OwnerCharacter)
	{
		UE_LOG(LogGGMCombatDebug, Warning, TEXT("BeginBlockInput EARLY RETURN | OwnerCharacter null"));
		return;
	}

	if (GetOwnerRole() == ROLE_Authority)
	{
		UE_LOG(
			LogGGMCombatDebug,
			Warning,
			TEXT("BeginBlockInput AUTHORITY | CanStartBlockAuthoritative=%d IsBlocking=%d"),
			CanStartBlockAuthoritative() ? 1 : 0,
			IsBlocking() ? 1 : 0);

		if (CanStartBlockAuthoritative() && !IsBlocking())
		{
			SetBlocking(true);
		}
		return;
	}

	if (!OwnerCharacter->IsAlive() || CombatState != EGGMCombatState::WeaponIdle)
	{
		UE_LOG(
			LogGGMCombatDebug,
			Warning,
			TEXT("BeginBlockInput EARLY RETURN | client path failed | Alive=%d CombatState=%d"),
			OwnerCharacter->IsAlive() ? 1 : 0,
			static_cast<int32>(CombatState));
		return;
	}

	UE_LOG(LogGGMCombatDebug, Warning, TEXT("BeginBlockInput -> SetBlocking(true)"));
	SetBlocking(true);
}

void UGGMCombatComponent::EndBlockInput()
{
	bWantsBlockHeld = false;

	if (!OwnerCharacter || !IsBlocking())
	{
		return;
	}

	SetBlocking(false);
}

void UGGMCombatComponent::SetBlocking(bool bNewBlocking)
{
	UE_LOG(
		LogGGMCombatDebug,
		Warning,
		TEXT("SetBlocking CALLED | bNewBlocking=%d Owner=%d Alive=%d Role=%d CombatState=%d IsBlocking=%d"),
		bNewBlocking ? 1 : 0,
		OwnerCharacter ? 1 : 0,
		(OwnerCharacter && OwnerCharacter->IsAlive()) ? 1 : 0,
		static_cast<int32>(GetOwnerRole()),
		static_cast<int32>(CombatState),
		IsBlocking() ? 1 : 0);

	if (!OwnerCharacter || !OwnerCharacter->IsAlive())
	{
		UE_LOG(LogGGMCombatDebug, Warning, TEXT("SetBlocking EARLY RETURN | invalid owner or dead"));
		return;
	}

	if (bNewBlocking)
	{
		if (GetOwnerRole() == ROLE_Authority)
		{
			if (!CanStartBlockAuthoritative())
			{
				UE_LOG(
					LogGGMCombatDebug,
					Warning,
					TEXT("SetBlocking EARLY RETURN | authority cannot start block | CombatState=%d BlockLockedFromAttack=%d BlockLockedFromBlockedHit=%d"),
					static_cast<int32>(CombatState),
					bBlockLockedFromAttack ? 1 : 0,
					bBlockLockedFromBlockedHit ? 1 : 0);
				return;
			}
		}
		else
		{
			if (CombatState != EGGMCombatState::WeaponIdle)
			{
				UE_LOG(
					LogGGMCombatDebug,
					Warning,
					TEXT("SetBlocking EARLY RETURN | client CombatState != WeaponIdle | CombatState=%d"),
					static_cast<int32>(CombatState));
				return;
			}
		}
	}

	if (IsBlocking() == bNewBlocking)
	{
		UE_LOG(
			LogGGMCombatDebug,
			Warning,
			TEXT("SetBlocking NO STATE CHANGE | IsBlocking() == bNewBlocking (%d)"),
			bNewBlocking ? 1 : 0);

		if (GetOwnerRole() != ROLE_Authority)
		{
			UE_LOG(LogGGMCombatDebug, Warning, TEXT("SetBlocking -> ServerSetBlocking(%d)"), bNewBlocking ? 1 : 0);
			ServerSetBlocking(bNewBlocking);
		}
		return;
	}

	UE_LOG(
		LogGGMCombatDebug,
		Warning,
		TEXT("SetBlocking APPLY | targetState=%d"),
		bNewBlocking ? static_cast<int32>(EGGMCombatState::Blocking) : static_cast<int32>(IsWeaponDrawn() ? EGGMCombatState::WeaponIdle : EGGMCombatState::Unarmed));

	SetCombatState(
		bNewBlocking
		? EGGMCombatState::Blocking
		: (IsWeaponDrawn() ? EGGMCombatState::WeaponIdle : EGGMCombatState::Unarmed));

	if (GetOwnerRole() != ROLE_Authority)
	{
		UE_LOG(LogGGMCombatDebug, Warning, TEXT("SetBlocking -> ServerSetBlocking(%d)"), bNewBlocking ? 1 : 0);
		ServerSetBlocking(bNewBlocking);
	}
}

void UGGMCombatComponent::ServerSetBlocking_Implementation(bool bNewBlocking)
{
	if (!OwnerCharacter || !OwnerCharacter->IsAlive())
	{
		return;
	}

	if (bNewBlocking && !CanStartBlockAuthoritative())
	{
		return;
	}

	if (IsBlocking() == bNewBlocking)
	{
		return;
	}

	SetCombatState(
		bNewBlocking
		? EGGMCombatState::Blocking
		: (IsWeaponDrawn() ? EGGMCombatState::WeaponIdle : EGGMCombatState::Unarmed));
}

void UGGMCombatComponent::StartBlockLockAfterAttack()
{
	if (!OwnerCharacter || !OwnerCharacter->HasAuthority())
	{
		return;
	}

	bBlockLockedFromAttack = true;

	UE_LOG(
		LogGGMCombatDebug,
		Warning,
		TEXT("StartBlockLockAfterAttack | Duration=%.3f"),
		FMath::Max(0.01f, GetBlockLockAfterAttack()));

	OwnerCharacter->GetWorldTimerManager().ClearTimer(BlockLockTimerHandle);
	OwnerCharacter->GetWorldTimerManager().SetTimer(
		BlockLockTimerHandle,
		this,
		&UGGMCombatComponent::UnlockBlockAfterAttack,
		FMath::Max(0.01f, GetBlockLockAfterAttack()),
		false);
}

void UGGMCombatComponent::UnlockBlockAfterAttack()
{
	if (!OwnerCharacter)
	{
		return;
	}

	bBlockLockedFromAttack = false;
	OwnerCharacter->GetWorldTimerManager().ClearTimer(BlockLockTimerHandle);
	UE_LOG(LogGGMCombatDebug, Warning, TEXT("UnlockBlockAfterAttack"));
	TryReapplyHeldBlockAuthoritative();
}

void UGGMCombatComponent::ApplyBlockedHitPunishToOwner()
{
	if (!OwnerCharacter || !OwnerCharacter->HasAuthority())
	{
		return;
	}

	bAttackLockedFromBlockedHit = true;
	bBlockLockedFromBlockedHit = true;

	UE_LOG(
		LogGGMCombatDebug,
		Warning,
		TEXT("ApplyBlockedHitPunishToOwner | AttackLockDuration=%.3f BlockLockDuration=%.3f"),
		FMath::Max(0.01f, GetAttackLockOnBlockedHit()),
		FMath::Max(0.01f, GetBlockLockOnBlockedHit()));

	if (IsBlocking())
	{
		SetCombatState(EGGMCombatState::WeaponIdle);
	}

	OwnerCharacter->GetWorldTimerManager().ClearTimer(AttackBlockedHitLockTimerHandle);
	OwnerCharacter->GetWorldTimerManager().SetTimer(
		AttackBlockedHitLockTimerHandle,
		this,
		&UGGMCombatComponent::UnlockAttackAfterBlockedHit,
		FMath::Max(0.01f, GetAttackLockOnBlockedHit()),
		false);

	OwnerCharacter->GetWorldTimerManager().ClearTimer(BlockBlockedHitLockTimerHandle);
	OwnerCharacter->GetWorldTimerManager().SetTimer(
		BlockBlockedHitLockTimerHandle,
		this,
		&UGGMCombatComponent::UnlockBlockAfterBlockedHit,
		FMath::Max(0.01f, GetBlockLockOnBlockedHit()),
		false);
}

void UGGMCombatComponent::UnlockAttackAfterBlockedHit()
{
	if (!OwnerCharacter)
	{
		return;
	}

	bAttackLockedFromBlockedHit = false;
	OwnerCharacter->GetWorldTimerManager().ClearTimer(AttackBlockedHitLockTimerHandle);
	UE_LOG(LogGGMCombatDebug, Warning, TEXT("UnlockAttackAfterBlockedHit"));
}

void UGGMCombatComponent::UnlockBlockAfterBlockedHit()
{
	if (!OwnerCharacter)
	{
		return;
	}

	bBlockLockedFromBlockedHit = false;
	OwnerCharacter->GetWorldTimerManager().ClearTimer(BlockBlockedHitLockTimerHandle);
	UE_LOG(LogGGMCombatDebug, Warning, TEXT("UnlockBlockAfterBlockedHit"));
	TryReapplyHeldBlockAuthoritative();
}

void UGGMCombatComponent::TryReapplyHeldBlockAuthoritative()
{
	if (!OwnerCharacter || !OwnerCharacter->HasAuthority())
	{
		return;
	}

	if (!bWantsBlockHeld || IsBlocking() || !CanStartBlockAuthoritative())
	{
		UE_LOG(
			LogGGMCombatDebug,
			Warning,
			TEXT("TryReapplyHeldBlockAuthoritative SKIP | WantsHeld=%d IsBlocking=%d CanStart=%d"),
			bWantsBlockHeld ? 1 : 0,
			IsBlocking() ? 1 : 0,
			CanStartBlockAuthoritative() ? 1 : 0);
		return;
	}

	UE_LOG(LogGGMCombatDebug, Warning, TEXT("TryReapplyHeldBlockAuthoritative -> SetCombatState(Blocking)"));
	SetCombatState(EGGMCombatState::Blocking);
}

void UGGMCombatComponent::ClientConfirmAttack_Implementation()
{
	UE_LOG(
		LogGGMCombatDebug,
		Warning,
		TEXT("ClientConfirmAttack_Implementation | Owner=%s LocalControlled=%d HasAuthority=%d"),
		OwnerCharacter ? *OwnerCharacter->GetName() : TEXT("None"),
		(OwnerCharacter && OwnerCharacter->IsLocallyControlled()) ? 1 : 0,
		(OwnerCharacter && OwnerCharacter->HasAuthority()) ? 1 : 0);
}

void UGGMCombatComponent::MulticastPlayAttackMontage_Implementation()
{
	UE_LOG(
		LogGGMCombatDebug,
		Warning,
		TEXT("MulticastPlayAttackMontage_Implementation CALLED | Owner=%s HasAuthority=%d IsLocallyControlled=%d CurrentAttackType=%d ResolvedMontage=%d"),
		OwnerCharacter ? *OwnerCharacter->GetName() : TEXT("None"),
		(OwnerCharacter && OwnerCharacter->HasAuthority()) ? 1 : 0,
		(OwnerCharacter && OwnerCharacter->IsLocallyControlled()) ? 1 : 0,
		static_cast<int32>(CurrentAttackType),
		GetResolvedAttackMontage() ? 1 : 0);

	if (!OwnerCharacter)
	{
		UE_LOG(LogGGMCombatDebug, Warning, TEXT("MulticastPlayAttackMontage EARLY RETURN | no owner"));
		return;
	}

	if (!OwnerCharacter->HasAuthority() && OwnerCharacter->IsLocallyControlled())
	{
		UE_LOG(LogGGMCombatDebug, Warning, TEXT("MulticastPlayAttackMontage EARLY RETURN | locally controlled client skip"));
		return;
	}

	UE_LOG(LogGGMCombatDebug, Warning, TEXT("MulticastPlayAttackMontage -> PlayAttackMontageLocal"));
	PlayAttackMontageLocal();
}

void UGGMCombatComponent::MulticastPlayHitSound_Implementation(bool bBlocked, FVector SoundLocation)
{
	if (bBlocked)
	{
		if (CachedTuning.HitBlockSound)
		{
			UGameplayStatics::PlaySoundAtLocation(this, CachedTuning.HitBlockSound, SoundLocation);
		}
	}
	else
	{
		if (CachedTuning.HitFleshSound)
		{
			UGameplayStatics::PlaySoundAtLocation(this, CachedTuning.HitFleshSound, SoundLocation);
		}
	}
}

void UGGMCombatComponent::OnRep_CombatState()
{
	UE_LOG(
		LogGGMCombatDebug,
		Warning,
		TEXT("OnRep_CombatState | CombatState=%d IsBlocking=%d IsWeaponDrawn=%d"),
		static_cast<int32>(CombatState),
		IsBlocking() ? 1 : 0,
		IsWeaponDrawn() ? 1 : 0);

	if (IsBlocking())
	{
		PlayBlockMontageLocal();
	}
	else
	{
		StopBlockMontageLocal();
	}

	NotifyOwnerWeaponDrawnChanged();
}

void UGGMCombatComponent::OnRep_AttackMontageRepCounter()
{
	UE_LOG(
		LogGGMCombatDebug,
		Warning,
		TEXT("OnRep_AttackMontageRepCounter | Counter=%u CurrentAttackType=%d ResolvedMontage=%d"),
		AttackMontageRepCounter,
		static_cast<int32>(CurrentAttackType),
		GetResolvedAttackMontage() ? 1 : 0);

	PlayAttackMontageLocal();
}

void UGGMCombatComponent::NotifyOwnerWeaponDrawnChanged()
{
	if (!OwnerCharacter)
	{
		OwnerCharacter = Cast<AGGMCharacter>(GetOwner());
	}

	if (OwnerCharacter)
	{
		OwnerCharacter->RefreshWeaponDrawnPresentationAndMovementState();
	}
}

void UGGMCombatComponent::PlayAttackMontageLocal()
{
	UAnimMontage* ResolvedAttackMontage = GetResolvedAttackMontage();

	UE_LOG(
		LogGGMCombatDebug,
		Warning,
		TEXT("PlayAttackMontageLocal CALLED | Montage=%d OwnerCharacter=%d CurrentAttackType=%d"),
		ResolvedAttackMontage ? 1 : 0,
		OwnerCharacter ? 1 : 0,
		static_cast<int32>(CurrentAttackType));

	if (!ResolvedAttackMontage || !OwnerCharacter)
	{
		UE_LOG(
			LogGGMCombatDebug,
			Warning,
			TEXT("PlayAttackMontageLocal EARLY RETURN | no montage or no owner | Montage=%d Owner=%d"),
			ResolvedAttackMontage ? 1 : 0,
			OwnerCharacter ? 1 : 0);
		return;
	}

	USkeletalMeshComponent* MeshComp = OwnerCharacter->GetMesh();
	if (!MeshComp)
	{
		UE_LOG(LogGGMCombatDebug, Warning, TEXT("PlayAttackMontageLocal EARLY RETURN | MeshComp null"));
		return;
	}

	UAnimInstance* AnimInstance = MeshComp->GetAnimInstance();
	if (!AnimInstance)
	{
		UE_LOG(LogGGMCombatDebug, Warning, TEXT("PlayAttackMontageLocal EARLY RETURN | AnimInstance null"));
		return;
	}

	UE_LOG(
		LogGGMCombatDebug,
		Warning,
		TEXT("PlayAttackMontageLocal -> Montage_Play | MontageName=%s AnimInstance=%s"),
		*ResolvedAttackMontage->GetName(),
		*AnimInstance->GetName());

	AnimInstance->Montage_Stop(0.05f, ResolvedAttackMontage);
	AnimInstance->Montage_Play(ResolvedAttackMontage, 1.f);
}

void UGGMCombatComponent::PlayBlockMontageLocal()
{
	UE_LOG(
		LogGGMCombatDebug,
		Warning,
		TEXT("PlayBlockMontageLocal CALLED | OwnerCharacter=%d BlockMontage=%d"),
		OwnerCharacter ? 1 : 0,
		GetResolvedBlockMontage() ? 1 : 0);

	if (!OwnerCharacter)
	{
		UE_LOG(LogGGMCombatDebug, Warning, TEXT("PlayBlockMontageLocal EARLY RETURN | OwnerCharacter null"));
		return;
	}

	UAnimMontage* ResolvedBlockMontage = GetResolvedBlockMontage();
	if (!ResolvedBlockMontage)
	{
		UE_LOG(LogGGMCombatDebug, Warning, TEXT("PlayBlockMontageLocal EARLY RETURN | BlockMontage null"));
		return;
	}

	USkeletalMeshComponent* MeshComp = OwnerCharacter->GetMesh();
	if (!MeshComp)
	{
		UE_LOG(LogGGMCombatDebug, Warning, TEXT("PlayBlockMontageLocal EARLY RETURN | MeshComp null"));
		return;
	}

	UAnimInstance* AnimInstance = MeshComp->GetAnimInstance();
	if (!AnimInstance)
	{
		UE_LOG(LogGGMCombatDebug, Warning, TEXT("PlayBlockMontageLocal EARLY RETURN | AnimInstance null"));
		return;
	}

	if (!AnimInstance->Montage_IsPlaying(ResolvedBlockMontage))
	{
		UE_LOG(LogGGMCombatDebug, Warning, TEXT("PlayBlockMontageLocal -> Montage_Play"));
		AnimInstance->Montage_Play(ResolvedBlockMontage, 1.f);
	}
	else
	{
		UE_LOG(LogGGMCombatDebug, Warning, TEXT("PlayBlockMontageLocal SKIP | already playing"));
	}
}

void UGGMCombatComponent::StopBlockMontageLocal()
{
	UE_LOG(
		LogGGMCombatDebug,
		Warning,
		TEXT("StopBlockMontageLocal CALLED | OwnerCharacter=%d BlockMontage=%d"),
		OwnerCharacter ? 1 : 0,
		GetResolvedBlockMontage() ? 1 : 0);

	if (!OwnerCharacter)
	{
		UE_LOG(LogGGMCombatDebug, Warning, TEXT("StopBlockMontageLocal EARLY RETURN | OwnerCharacter null"));
		return;
	}

	UAnimMontage* ResolvedBlockMontage = GetResolvedBlockMontage();
	if (!ResolvedBlockMontage)
	{
		UE_LOG(LogGGMCombatDebug, Warning, TEXT("StopBlockMontageLocal EARLY RETURN | BlockMontage null"));
		return;
	}

	USkeletalMeshComponent* MeshComp = OwnerCharacter->GetMesh();
	if (!MeshComp)
	{
		UE_LOG(LogGGMCombatDebug, Warning, TEXT("StopBlockMontageLocal EARLY RETURN | MeshComp null"));
		return;
	}

	UAnimInstance* AnimInstance = MeshComp->GetAnimInstance();
	if (!AnimInstance)
	{
		UE_LOG(LogGGMCombatDebug, Warning, TEXT("StopBlockMontageLocal EARLY RETURN | AnimInstance null"));
		return;
	}

	UE_LOG(LogGGMCombatDebug, Warning, TEXT("StopBlockMontageLocal -> Montage_Stop"));
	AnimInstance->Montage_Stop(0.15f, ResolvedBlockMontage);
}