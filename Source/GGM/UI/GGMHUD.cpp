#include "GGMHUD.h"

#include "Engine/Canvas.h"
#include "Engine/Font.h"
#include "GameFramework/PlayerController.h"
#include "../Player/GGMCharacter.h"

void AGGMHUD::DrawHUD()
{
	Super::DrawHUD();

	if (!Canvas)
	{
		return;
	}

	APlayerController* PC = GetOwningPlayerController();
	if (!PC)
	{
		return;
	}

	AGGMCharacter* Character = Cast<AGGMCharacter>(PC->GetPawn());
	if (!Character)
	{
		return;
	}

	const FString HealthText = BuildHealthPercentText();
	if (HealthText.IsEmpty())
	{
		return;
	}

	UFont* DrawFont = GEngine ? GEngine->GetLargeFont() : nullptr;
	if (!DrawFont)
	{
		return;
	}

	float TextW = 0.f;
	float TextH = 0.f;
	Canvas->StrLen(DrawFont, HealthText, TextW, TextH);

	const float Scale = 1.8f;
	const float FinalW = TextW * Scale;
	const float FinalH = TextH * Scale;

	const float X = (Canvas->ClipX * 0.5f) - (FinalW * 0.5f);
	const float Y = Canvas->ClipY - FinalH - 40.f;

	FCanvasTextItem ShadowItem(FVector2D(X + 2.f, Y + 2.f), FText::FromString(HealthText), DrawFont, FLinearColor::Black);
	ShadowItem.Scale = FVector2D(Scale, Scale);
	Canvas->DrawItem(ShadowItem);

	FCanvasTextItem TextItem(FVector2D(X, Y), FText::FromString(HealthText), DrawFont, FLinearColor::Red);
	TextItem.Scale = FVector2D(Scale, Scale);
	Canvas->DrawItem(TextItem);
}

FString AGGMHUD::BuildHealthPercentText() const
{
	APlayerController* PC = GetOwningPlayerController();
	if (!PC)
	{
		return FString();
	}

	AGGMCharacter* Character = Cast<AGGMCharacter>(PC->GetPawn());
	if (!Character)
	{
		return FString();
	}

	if (Character->MaxHealth <= 0.f)
	{
		return TEXT("0%");
	}

	const float HealthPct = (Character->CurrentHealth / Character->MaxHealth) * 100.f;
	const int32 RoundedPct = FMath::Clamp(FMath::RoundToInt(HealthPct), 0, 100);

	return FString::Printf(TEXT("%d%%"), RoundedPct);
}