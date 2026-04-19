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
		CachedTuning.MissSound = EffectiveCombatData->MissSound;
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

		if (EquippedWeaponData->MissSound)
		{
			CachedTuning.MissSound = EquippedWeaponData->MissSound;
		}
	}
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
	if (CombatState == NewState)
	{
		return;
	}

	const bool bWasWeaponDrawn = IsWeaponDrawn();
	const bool bWasBlocking = IsBlocking();

	CombatState = NewState;

	const bool bIsWeaponNowDrawn = IsWeaponDrawn();
	const bool bIsBlockingNow = IsBlocking();

	if (bWasBlocking != bIsBlockingNow)
	{
		if (bIsBlockingNow)
		{
			PlayBlockMontageLocal();
		}
		else
		{
			StopBlockMontageLocal();
		}
	}

	if (bWasWeaponDrawn != bIsWeaponNowDrawn)
	{
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

	return bHasOwner && bHasAuthority && bAlive && bStateOk && bAttackLockClear;
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
		return false;
	}

	const float RequiredStamina = GetResolvedAttackStaminaCost(AttackType);
	return (RequiredStamina <= 0.f || OwnerCharacter->CurrentStamina >= RequiredStamina);
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
	if (!OwnerCharacter || !OwnerCharacter->IsAlive())
	{
		return;
	}

	if (GetOwnerRole() == ROLE_Authority)
	{
		StartAttackAuthoritative(AttackType);
		return;
	}

	ServerStartAttack(AttackType);
}

void UGGMCombatComponent::ServerStartAttack_Implementation(EGGMAttackType AttackType)
{
	StartAttackAuthoritative(AttackType);
}

void UGGMCombatComponent::StartAttackAuthoritative(EGGMAttackType AttackType)
{
	if (!CanStartAttackAuthoritative())
	{
		return;
	}

	if (!HasEnoughStaminaForAttack(AttackType))
	{
		return;
	}

	CurrentAttackType = AttackType;

	SpendAttackStaminaAuthoritative(AttackType);

	StartBlockLockAfterAttack();
	SetAttacking(true);

	bAttackHitCommitted = false;

	++AttackMontageRepCounter;
	if (AttackMontageRepCounter == 0)
	{
		++AttackMontageRepCounter;
	}

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
		return;
	}

	if (bAttackHitCommitted)
	{
		return;
	}

	bAttackHitCommitted = true;
	PerformAttackHitTest();
}

void UGGMCombatComponent::FinishAttackAuthoritative()
{
	if (!OwnerCharacter || !OwnerCharacter->HasAuthority())
	{
		return;
	}

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
				continue;
			}

			const FVector ToTargetDir = ToTarget2D.GetSafeNormal();
			const float Dot = FVector::DotProduct(AttackDir, ToTargetDir);
			if (Dot < MinDot)
			{
				continue;
			}

			const bool bStrictlyCloser = DistSq < (BestDistSq - DistanceTieToleranceSq);
			const bool bSameDistanceBucket = FMath::Abs(DistSq - BestDistSq) <= DistanceTieToleranceSq;

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
		HandleMeleeHitServer(BestTarget);
	}
	else
	{
		const FVector MissSoundLocation = Start + (AttackDir * FMath::Max(50.f, ResolvedAttackRange * 0.4f));
		MulticastPlayMissSound(MissSoundLocation);
	}
}

void UGGMCombatComponent::HandleMeleeHitServer(AGGMCharacter* HitTarget)
{
	if (!OwnerCharacter || !OwnerCharacter->HasAuthority() || !HitTarget || !HitTarget->IsAlive())
	{
		return;
	}

	const bool bWasBlocked = ApplyDamageToTargetAuthoritative(HitTarget, GetResolvedAttackDamage());

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
	bWantsBlockHeld = true;

	if (!OwnerCharacter)
	{
		return;
	}

	if (GetOwnerRole() == ROLE_Authority)
	{
		if (CanStartBlockAuthoritative() && !IsBlocking())
		{
			SetBlocking(true);
		}
		return;
	}

	if (!OwnerCharacter->IsAlive() || CombatState != EGGMCombatState::WeaponIdle)
	{
		return;
	}

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
	if (!OwnerCharacter || !OwnerCharacter->IsAlive())
	{
		return;
	}

	if (bNewBlocking)
	{
		if (GetOwnerRole() == ROLE_Authority)
		{
			if (!CanStartBlockAuthoritative())
			{
				return;
			}
		}
		else
		{
			if (CombatState != EGGMCombatState::WeaponIdle)
			{
				return;
			}
		}
	}

	if (IsBlocking() == bNewBlocking)
	{
		if (GetOwnerRole() != ROLE_Authority)
		{
			ServerSetBlocking(bNewBlocking);
		}
		return;
	}

	SetCombatState(
		bNewBlocking
		? EGGMCombatState::Blocking
		: (IsWeaponDrawn() ? EGGMCombatState::WeaponIdle : EGGMCombatState::Unarmed));

	if (GetOwnerRole() != ROLE_Authority)
	{
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
}

void UGGMCombatComponent::UnlockBlockAfterBlockedHit()
{
	if (!OwnerCharacter)
	{
		return;
	}

	bBlockLockedFromBlockedHit = false;
	OwnerCharacter->GetWorldTimerManager().ClearTimer(BlockBlockedHitLockTimerHandle);
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
		return;
	}

	SetCombatState(EGGMCombatState::Blocking);
}

void UGGMCombatComponent::ClientConfirmAttack_Implementation()
{
	PlayAttackMontageLocal();
}

void UGGMCombatComponent::MulticastPlayAttackMontage_Implementation()
{
	if (!OwnerCharacter)
	{
		return;
	}

	if (!OwnerCharacter->HasAuthority() && OwnerCharacter->IsLocallyControlled())
	{
		return;
	}

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

void UGGMCombatComponent::MulticastPlayMissSound_Implementation(FVector SoundLocation)
{
	if (CachedTuning.MissSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, CachedTuning.MissSound, SoundLocation);
	}
}

void UGGMCombatComponent::OnRep_CombatState()
{
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

	if (!ResolvedAttackMontage || !OwnerCharacter)
	{
		return;
	}

	USkeletalMeshComponent* MeshComp = OwnerCharacter->GetMesh();
	if (!MeshComp)
	{
		return;
	}

	UAnimInstance* AnimInstance = MeshComp->GetAnimInstance();
	if (!AnimInstance)
	{
		return;
	}

	AnimInstance->Montage_Stop(0.05f, ResolvedAttackMontage);
	AnimInstance->Montage_Play(ResolvedAttackMontage, 1.f);
}

void UGGMCombatComponent::PlayBlockMontageLocal()
{
	if (!OwnerCharacter)
	{
		return;
	}

	UAnimMontage* ResolvedBlockMontage = GetResolvedBlockMontage();
	if (!ResolvedBlockMontage)
	{
		return;
	}

	USkeletalMeshComponent* MeshComp = OwnerCharacter->GetMesh();
	if (!MeshComp)
	{
		return;
	}

	UAnimInstance* AnimInstance = MeshComp->GetAnimInstance();
	if (!AnimInstance)
	{
		return;
	}

	if (!AnimInstance->Montage_IsPlaying(ResolvedBlockMontage))
	{
		AnimInstance->Montage_Play(ResolvedBlockMontage, 1.f);
	}
}

void UGGMCombatComponent::StopBlockMontageLocal()
{
	if (!OwnerCharacter)
	{
		return;
	}

	UAnimMontage* ResolvedBlockMontage = GetResolvedBlockMontage();
	if (!ResolvedBlockMontage)
	{
		return;
	}

	USkeletalMeshComponent* MeshComp = OwnerCharacter->GetMesh();
	if (!MeshComp)
	{
		return;
	}

	UAnimInstance* AnimInstance = MeshComp->GetAnimInstance();
	if (!AnimInstance)
	{
		return;
	}

	AnimInstance->Montage_Stop(0.15f, ResolvedBlockMontage);
}