// Copyright 2017 ~ 2022 Critical Failure Studio Ltd. All rights reserved.

#include "Notifies/PaperZDAnimNotify_PlaySound.h"
#include "PaperZD.h"
#include "PaperZDAnimInstance.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "Engine/World.h"

UPaperZDAnimNotify_PlaySound::UPaperZDAnimNotify_PlaySound(const FObjectInitializer& ObjectInitializer)
	: Super()
{
	VolumeMultiplier = 1.0f;
	PitchMultiplier = 1.0f;

#if WITH_EDITORONLY_DATA
	Color = FColor(196, 142, 255, 255);
	bPreviewIgnoreAttenuation = false;
#endif // WITH_EDITORONLY_DATA
}

void UPaperZDAnimNotify_PlaySound::OnReceiveNotify_Implementation(UPaperZDAnimInstance* OwningInstance /* = nullptr*/)
{
	// We use the SequenceRenderComponent associated to the AnimSequence to know where and how to spawn the sound.
	if (SequenceRenderComponent && Sound)
	{
		if (Sound->IsLooping())
		{
			UObject* AnimSequencePkg = GetContainingAsset();
			UE_LOG(LogAudio, Warning, TEXT("PlaySound notify: Anim %s tried to spawn infinitely looping sound asset %s. Spawning suppressed."), *GetNameSafe(AnimSequencePkg), *GetNameSafe(Sound));
			return;
		}

#if WITH_EDITORONLY_DATA
		UWorld* World = GetWorld();
		if (bPreviewIgnoreAttenuation && World && World->WorldType == EWorldType::EditorPreview)
		{
			UGameplayStatics::PlaySound2D(World, Sound, VolumeMultiplier, PitchMultiplier);
		}
		else
#endif
		{
			if (bFollow)
			{
				UGameplayStatics::SpawnSoundAttached(Sound, SequenceRenderComponent, AttachName, FVector(ForceInit), EAttachLocation::SnapToTarget, false, VolumeMultiplier, PitchMultiplier);
			}
			else
			{
				UGameplayStatics::PlaySoundAtLocation(GetWorld(), Sound, SequenceRenderComponent->GetComponentLocation(), VolumeMultiplier, PitchMultiplier);
			}
		}
	}
}

FName UPaperZDAnimNotify_PlaySound::GetDisplayName_Implementation() const
{
	if (Sound)
	{
		return FName(*Sound->GetName());
	}
	else
	{
		return Super::GetDisplayName_Implementation();
	}
}
