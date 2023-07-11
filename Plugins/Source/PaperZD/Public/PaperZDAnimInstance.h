// Copyright 2017 ~ 2022 Critical Failure Studio Ltd. All rights reserved.

#pragma once
#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Templates/SubclassOf.h"
#include "IPaperZDAnimInstanceManager.h"
#include "PaperZDAnimInstance.generated.h"

class UPaperZDAnimSequence;
class UPaperZDAnimPlayer;
class UWorld;
class UFunction;
class APaperZDCharacter;
struct FPaperZDAnimNode_Sink;

/**
 * Runtime class that the AnimBP gets compiled into.
 */
UCLASS(abstract, BlueprintType)
class PAPERZD_API UPaperZDAnimInstance : public UObject
{
	GENERATED_BODY()

	/* Pointer to the Animation Player that is responsible of the playback of the sequences. */
	UPROPERTY(Transient)
	UPaperZDAnimPlayer* AnimPlayer;

	/* The main sink node that collects the final animation data. */
	FPaperZDAnimNode_Sink* RootNode;

	/* Manager for this AnimInstance, which we query for context information. */
	TScriptInterface<IPaperZDAnimInstanceManager> Manager;
	
	/**
	 * The PaperZD Character that owns this instance, not null when the instance is called through a ZD character.
	 * Maintained only for backwards compatibility, one should prefer using the "GetOwningActor" method instead.
	 */
	UPROPERTY(BlueprintGetter = "GetPaperCharacter", Transient, Category="PaperZD")
	APaperZDCharacter* PaperCharacter;

	/* If true, sequencer is currently running a movie scene through this AnimInstance and hence, we have paused the AnimSequence update and evaluations. */
	bool bSequencerOverride;
	
public:

	/* If this AnimBP should globally ignore time dilation. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "PaperZD")
	bool bIgnoreTimeDilation;

private:
	/**
	 * If we should enable Transitional States, so even after transitioning from State A to State B, we allow for more levels of recursions.
	 * This allows for a simpler more compact graph, by just connecting states and let them naturally flow to the end state, without seeing flicker as it's done on the same frame.
	 * Deactivating this makes each state become an end-state on each tick, and flow has to be done manually, but is overall faster.
	 */
	UPROPERTY(EditAnywhere, Category = "PaperZD")
	bool bAllowTransitionalStates;

public:
	//ctor
	UPaperZDAnimInstance();

	/* We obtain the world from the character defined. */
	virtual class UWorld* GetWorld() const override;

	/* Tick every frame. */
	virtual void Tick(float DeltaTime);

	/* Getter for transitional states */
	bool AllowsTransitionalStates() const;

	/* Tries to find the UFunction that implements the notify with the given name. */
	UFunction* FindAnimNotifyFunction(FName AnimNotifyName) const;

	/**
	 * Called every tick, after all the animations have been processed.
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "PaperZD")
	void OnTick(float DeltaTime);

	/* Init the AnimInstance using the general interface. */
	void Init(TScriptInterface<IPaperZDAnimInstanceManager> InManager);

	/**
	 * Called when the AnimInstance has been initizalized, but before the first tick.
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "PaperZD")
	void OnInit();

	/* Obtain the actor that owns this AnimInstance. */
	UFUNCTION(BlueprintPure, Category = "PaperZD")
	AActor* GetOwningActor() const;

	/* Obtain the PaperZD Character that owns this instance if applicable, can be null. */
	UFUNCTION(BlueprintPure, Category = "PaperZD")
	APaperZDCharacter* GetPaperCharacter() const;

	/**
	 * Changes current execution state and jumps to the given JumpNode name.
	 * @param JumpName			Name of the jump node we wish to go to.
	 * @param StateMachineName	If specified, the jump link will only be applied to the given state machine.
	 */
	UFUNCTION(BlueprintCallable, Category = "PaperZD")
	void JumpToNode(FName JumpName, FName StateMachineName = NAME_None);

	/* Obtains the current player, responsible of storing the playback information of this AnimInstance. */
	UFUNCTION(BlueprintPure, Category = "PaperZD|Playback")
	UPaperZDAnimPlayer* GetPlayer() const;

	/**
	 * Event called when we update playback, changing to a new sequence. 
	 * Only called for Animation Blueprints with "non-blendable" animation sources (like flipbooks), as these will only ever run one animation at a time.
	 * This behavior can be overridden if "bFireSequenceUpdateEvents" is set to true on the AnimPlayer.
	 * 
	 * @param From				The previously played sequence
	 * @param To				The sequence that will be played now
	 * @param CurrentProgress	The progress in which the "From" sequence was before changing, ranging from [0-1]
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Playback")
	void OnAnimSequenceUpdated(const UPaperZDAnimSequence* From, const UPaperZDAnimSequence* To, float CurrentProgress);

	/**
	 * Called when an AnimSequence completes playback. Will only be called for non-looping sequences, as the looping sequences do not really "end" their playback.
	 * @param InAnimSequence	Sequence that reached its end
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Playback")
	void OnAnimSequencePlaybackComplete(const UPaperZDAnimSequence* InAnimSequence);

	/* Called when sequencer overrides the animations that the AnimInstance is showing. */
	void PrepareForMovieSequence();

	/* Called after sequencer finished playing and thus, should return to normal execution path for the animations. */
	void RestorePreMovieSequenceState();

	 /** Gets the length in seconds of the asset referenced in an asset player node */
	UFUNCTION(BlueprintPure, Category="Asset Player", meta=(DisplayName="Length", BlueprintInternalUseOnly="true", AnimGetter="true"))
	float GetInstanceAssetPlayerLength(int32 AssetPlayerIndex);

	/** Get the current accumulated time in seconds for an asset player node */
	UFUNCTION(BlueprintPure, Category="Asset Player", meta = (DisplayName = "Current Time", BlueprintInternalUseOnly = "true", AnimGetter = "true"))
	float GetInstanceAssetPlayerTime(int32 AssetPlayerIndex);

	/** Get the current accumulated time as a fraction for an asset player node */
	UFUNCTION(BlueprintPure, Category="Asset Player", meta=(DisplayName="Current Time (ratio)", BlueprintInternalUseOnly="true", AnimGetter="true"))
	float GetInstanceAssetPlayerTimeFraction(int32 AssetPlayerIndex);

	/** Get the time in seconds from the end of an animation in an asset player node */
	UFUNCTION(BlueprintPure, Category="Asset Player", meta=(DisplayName="Time Remaining", BlueprintInternalUseOnly="true", AnimGetter="true"))
	float GetInstanceAssetPlayerTimeFromEnd(int32 AssetPlayerIndex);

	/** Get the time as a fraction of the asset length of an animation in an asset player node */
	UFUNCTION(BlueprintPure, Category="Asset Player", meta=(DisplayName="Time Remaining (ratio)", BlueprintInternalUseOnly="true", AnimGetter="true"))
	float GetInstanceAssetPlayerTimeFromEndFraction(int32 AssetPlayerIndex);

private:
	/* Obtains the equivalent delta time to use ignoring time dilation. */
	float GetDeltaTimeIgnoredDilation(float DeltaTime);

	/* Process the animation nodes. */
	void ProcessAnimations(float DeltaTime);
};
