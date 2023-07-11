// Copyright 2017 ~ 2022 Critical Failure Studio Ltd. All rights reserved.

#include "Editors/Slate/SPaperZDAnimAssetBrowser.h"
#include "Editors/Util/PaperZDEditorCommands.h"
#include "Editors/Util/PaperZDEditorSettings.h"
#include "Editors/PaperZDAnimationSourceEditor.h"
#include "Factories/PaperZDAnimSequenceFactory.h"
#include "AnimSequences/Sources/PaperZDAnimationSource.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SMenuAnchor.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SSpacer.h"
#include "Toolkits/GlobalEditorCommonCommands.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Commands/GenericCommands.h"
#include "AnimSequences/PaperZDAnimSequence.h"
#include "EditorStyleSet.h"
#include "IContentBrowserSingleton.h"
#include "ContentBrowserModule.h"
#include "AssetToolsModule.h"
#include "AssetRegistryModule.h"
#include "FileHelpers.h"
#include "ObjectTools.h"

#define LOCTEXT_NAMESPACE "PaperZDEditor_AnimAssetBrowser"

void SPaperZDAnimAssetBrowser::Construct(const FArguments& InArgs, TSharedPtr<FPaperZDAnimationSourceEditor> InSourceEditor)
{
	SourceEditorPtr = InSourceEditor;
	CurrentHistoryIdx = INDEX_NONE;

	//Setup arguments
	OnOpenAsset = InArgs._OnOpenAsset;

	//Bind commands
	Commands = MakeShareable(new FUICommandList());
	Commands->MapAction(FPaperZDEditorCommands::Get().CreateAnimSequence, FUIAction(
		FExecuteAction::CreateSP(this, &SPaperZDAnimAssetBrowser::HandleCreateAnimSequence)
	));

	Commands->MapAction(FPaperZDEditorCommands::Get().SaveSelectedAsset, FUIAction(
		FExecuteAction::CreateSP(this, &SPaperZDAnimAssetBrowser::SaveSelectedAssets),
		FCanExecuteAction::CreateSP(this, &SPaperZDAnimAssetBrowser::CanSaveSelectedAssets)
	));
	Commands->MapAction(FGenericCommands::Get().Duplicate, FUIAction(
		FExecuteAction::CreateSP(this, &SPaperZDAnimAssetBrowser::OnDuplicateSelection),
		FCanExecuteAction::CreateSP(this, &SPaperZDAnimAssetBrowser::CanDuplicateSelection)
	));

	Commands->MapAction(FGenericCommands::Get().Delete, FUIAction(
		FExecuteAction::CreateSP(this, &SPaperZDAnimAssetBrowser::OnDeleteSelection),
		FCanExecuteAction::CreateSP(this, &SPaperZDAnimAssetBrowser::CanDeleteSelection)
	));

	Commands->MapAction(FGlobalEditorCommonCommands::Get().FindInContentBrowser, FUIAction(
		FExecuteAction::CreateSP(this, &SPaperZDAnimAssetBrowser::FindInContentBrowser),
		FCanExecuteAction::CreateSP(this, &SPaperZDAnimAssetBrowser::CanFindInContentBrowser)
	));

	//Create the "Add Animation" button
	TSharedPtr<SComboButton> NewAnimationButton = SNew(SComboButton)
		.ComboButtonStyle(FEditorStyle::Get(), "ToolbarComboButton")
		.ButtonStyle(FEditorStyle::Get(), "FlatButton.Success")
		.ForegroundColor(FLinearColor::White)
		.ToolTipText(LOCTEXT("AddNewAsset", "Add a new animation asset."))
		.OnGetMenuContent(this, &SPaperZDAnimAssetBrowser::CreateAddNewMenuWidget)
		.HasDownArrow(true)
		.ContentPadding(FMargin(1, 0, 2, 0))
		.ButtonContent()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(FMargin(0, 1))
			[
				SNew(SImage)
				.Image(FEditorStyle::GetBrush("Plus"))
			]

			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.AutoWidth()
			.Padding(FMargin(2, 0, 2, 0))
			[
				SNew(STextBlock)
				.Text(LOCTEXT("AddNew", "Add New"))
			]
		];

	//Create the "history" buttons
	static const FName DefaultForegroundName("DefaultForeground");
	TSharedRef< SMenuAnchor > BackMenuAnchorPtr = SNew(SMenuAnchor)
		.Placement(MenuPlacement_BelowAnchor)
		.OnGetMenuContent(this, &SPaperZDAnimAssetBrowser::CreateHistoryMenu, true)
		[
			SNew(SButton)
			.OnClicked(this, &SPaperZDAnimAssetBrowser::OnGoBackInHistory)
			.ForegroundColor(FEditorStyle::GetSlateColor(DefaultForegroundName))
			.ButtonStyle(FEditorStyle::Get(), "FlatButton")
			.ContentPadding(FMargin(1, 0))
			.IsEnabled(this, &SPaperZDAnimAssetBrowser::CanStepBackInHistory)
			.ToolTipText(LOCTEXT("Backward_Tooltip", "Step backward in the asset history. Right click to see full history."))
			[
				SNew(STextBlock)
				.TextStyle(FEditorStyle::Get(), "ContentBrowser.TopBar.Font")
				.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.11"))
				.Text(FText::FromString(FString(TEXT("\xf060"))) /*fa-arrow-left*/)
			]
		];

	TSharedRef< SMenuAnchor > FwdMenuAnchorPtr = SNew(SMenuAnchor)
		.Placement(MenuPlacement_BelowAnchor)
		.OnGetMenuContent(this, &SPaperZDAnimAssetBrowser::CreateHistoryMenu, false)
		[
			SNew(SButton)
			.OnClicked(this, &SPaperZDAnimAssetBrowser::OnGoForwardInHistory)
			.ForegroundColor(FEditorStyle::GetSlateColor(DefaultForegroundName))
			.ButtonStyle(FEditorStyle::Get(), "FlatButton")
			.ContentPadding(FMargin(1, 0))
			.IsEnabled(this, &SPaperZDAnimAssetBrowser::CanStepForwardInHistory)
			.ToolTipText(LOCTEXT("Forward_Tooltip", "Step forward in the asset history. Right click to see full history."))
			[
				SNew(STextBlock)
				.TextStyle(FEditorStyle::Get(), "ContentBrowser.TopBar.Font")
				.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.11"))
				.Text(FText::FromString(FString(TEXT("\xf061"))) /*fa-arrow-right*/)
			]
		];

	//Setup the asset picker
	FContentBrowserModule& ContentBrowserModule = FModuleManager::Get().LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));

	//Setup filter
	Filter.bRecursiveClasses = true;
	Filter.ClassNames.Add(UPaperZDAnimSequence::StaticClass()->GetFName());
	Filter.TagsAndValues.Add(UPaperZDAnimSequence::GetAnimSourceMemberName(), FAssetData(SourceEditorPtr.Pin()->GetAnimationSourceBeingEdited()).GetExportTextName());

	//Setup config of the asset picker
	FAssetPickerConfig Config;
	Config.Filter = Filter;
	Config.InitialAssetViewType = EAssetViewType::Column;
	//Config.bAddFilterUI = true;
	Config.bShowPathInColumnView = true;
	Config.bSortByPathInColumnView = true;

	//Setup callbacks
	Config.OnAssetDoubleClicked = FOnAssetDoubleClicked::CreateSP(this, &SPaperZDAnimAssetBrowser::OnRequestOpenAsset, false);
	Config.OnGetAssetContextMenu = FOnGetAssetContextMenu::CreateSP(this, &SPaperZDAnimAssetBrowser::OnGetAssetContextMenu);
	Config.OnAssetTagWantsToBeDisplayed = FOnShouldDisplayAssetTag::CreateSP(this, &SPaperZDAnimAssetBrowser::CanShowColumnForAssetRegistryTag);
	Config.SyncToAssetsDelegates.Add(&SyncToAssetsDelegate);
	Config.GetCurrentSelectionDelegates.Add(&GetCurrentSelectionDelegate);
	Config.SetFilterDelegates.Add(&SetFilterDelegate);
	Config.bFocusSearchBoxWhenOpened = false;
	Config.DefaultFilterMenuExpansion = EAssetTypeCategories::Animation;
	Config.RefreshAssetViewDelegates.Add(&RefreshAssetViewDelegate);

	//Finally build the widget
	ChildSlot
	[
		SNew(SVerticalBox)

		//Top part
		+SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SBorder)
			.Padding(FMargin(3))
			.BorderImage( FEditorStyle::GetBrush("ToolPanel.GroupBorder") )
			[
				SNew(SHorizontalBox)
				+SHorizontalBox::Slot()
				.HAlign(HAlign_Left)
				[
					NewAnimationButton.ToSharedRef()
				]

				+SHorizontalBox::Slot()
				.HAlign(HAlign_Fill)
				[
					SNew(SSpacer)
				]

				+SHorizontalBox::Slot()
				.HAlign(HAlign_Right)
				[
					SNew(SHorizontalBox)
					//Back in history
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SBorder)
						.OnMouseButtonDown(this, &SPaperZDAnimAssetBrowser::OnMouseDownHistory, TWeakPtr<SMenuAnchor>(BackMenuAnchorPtr))
						.BorderImage(FEditorStyle::GetBrush("NoBorder"))
						[
							BackMenuAnchorPtr
						]
					]

					//Forward in history
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SBorder)
						.OnMouseButtonDown(this, &SPaperZDAnimAssetBrowser::OnMouseDownHistory, TWeakPtr<SMenuAnchor>(BackMenuAnchorPtr))
						.BorderImage(FEditorStyle::GetBrush("NoBorder"))
						[
							FwdMenuAnchorPtr
						]
					]
				]
			]
		]

		//Bottom part ~ Asset picker
		+SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SNew(SBorder)
			.Padding(FMargin(3))
			.BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
			[
				ContentBrowserModule.Get().CreateAssetPicker(Config)
			]
		]
	];

	// Create the ignore set for asset registry tags
	AssetRegistryTagsToIgnore.Add(TEXT("AnimSource"));
	AssetRegistryTagsToIgnore.Add(TEXT("AnimBP"));
}

FReply SPaperZDAnimAssetBrowser::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (Commands->ProcessCommandBindings(InKeyEvent))
	{
		return FReply::Handled();
	}

	return FReply::Unhandled();
}

void SPaperZDAnimAssetBrowser::SelectAsset(const FAssetData& AssetData)
{
	if (AssetData.IsValid())
	{
		TArray<FAssetData> CurrentSelection = GetCurrentSelectionDelegate.Execute();
		if (!CurrentSelection.Contains(AssetData))
		{
			TArray<FAssetData> AssetsToSelect;
			AssetsToSelect.Add(AssetData);
			SyncToAssetsDelegate.Execute(AssetsToSelect);
		}
	}
}

TSharedRef<SWidget> SPaperZDAnimAssetBrowser::CreateAddNewMenuWidget()
{
	const bool bShouldCloseWindowAfterMenuSelection = true;
	FMenuBuilder MenuBuilder(bShouldCloseWindowAfterMenuSelection, Commands);
	BuildAddNewMenu(MenuBuilder);

	return MenuBuilder.MakeWidget();
}

void SPaperZDAnimAssetBrowser::BuildAddNewMenu(FMenuBuilder& MenuBuilder)
{
	MenuBuilder.BeginSection("AddNewItem", LOCTEXT("AddOperations", "Add New"));
	{
		MenuBuilder.AddMenuEntry(FPaperZDEditorCommands::Get().CreateAnimSequence);
	}
	MenuBuilder.EndSection();
}

TSharedPtr<SWidget> SPaperZDAnimAssetBrowser::OnGetAssetContextMenu(const TArray<FAssetData>& SelectedAssets)
{
	FMenuBuilder MenuBuilder(/*bInShouldCloseWindowAfterMenuSelection=*/ true, Commands);

	//Common section
	MenuBuilder.BeginSection("Common", LOCTEXT("CommonHeading", "Common"));
	{
		MenuBuilder.AddMenuEntry(FPaperZDEditorCommands::Get().SaveSelectedAsset,
			NAME_None,
			LOCTEXT("SaveAsset", "Save"),
			LOCTEXT("SaveAssetTooltip", "Saves the item to file."),
			FSlateIcon(FEditorStyle::GetStyleSetName(), "Level.SaveIcon16x")
		);

		MenuBuilder.AddMenuEntry(FGenericCommands::Get().Duplicate);
		MenuBuilder.AddMenuEntry(FGenericCommands::Get().Delete);
	}
	MenuBuilder.EndSection();

	//Explore section
	MenuBuilder.BeginSection("Explore", LOCTEXT("ExploreHeading", "Explore"));
	{
		MenuBuilder.AddMenuEntry(FGlobalEditorCommonCommands::Get().FindInContentBrowser);
	}
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

TSharedRef<SWidget> SPaperZDAnimAssetBrowser::CreateHistoryMenu(bool bInBackHistory) const
{
	const bool bShouldCloseWindowAfterMenuSelection = true;
	FMenuBuilder MenuBuilder(bShouldCloseWindowAfterMenuSelection, nullptr);
	if (bInBackHistory)
	{
		for (int32 i = CurrentHistoryIdx - 1; i > 0; i--)
		{
			const FAssetData& AssetData = AssetHistory[i];
			if (AssetData.IsValid())
			{
				const FText DisplayName = FText::FromName(AssetData.AssetName);
				const FText Tooltip = FText::FromString(AssetData.ObjectPath.ToString());

				MenuBuilder.AddMenuEntry(DisplayName, Tooltip, FSlateIcon(), 
					FUIAction(
					FExecuteAction::CreateRaw(const_cast<SPaperZDAnimAssetBrowser*>(this), &SPaperZDAnimAssetBrowser::SetAssetHistoryIndex, i)
					), 
					NAME_None, EUserInterfaceActionType::Button);
			}
		}
	}
	else
	{
		for (int32 i = CurrentHistoryIdx + 1; i < AssetHistory.Num(); i++)
		{
			const FAssetData& AssetData = AssetHistory[i];
			if (AssetData.IsValid())
			{
				const FText DisplayName = FText::FromName(AssetData.AssetName);
				const FText Tooltip = FText::FromString(AssetData.ObjectPath.ToString());

				MenuBuilder.AddMenuEntry(DisplayName, Tooltip, FSlateIcon(),
					FUIAction(
						FExecuteAction::CreateRaw(const_cast<SPaperZDAnimAssetBrowser*>(this), &SPaperZDAnimAssetBrowser::SetAssetHistoryIndex, i)
					),
					NAME_None, EUserInterfaceActionType::Button);
			}
		}
	}

	return MenuBuilder.MakeWidget();
}

FReply SPaperZDAnimAssetBrowser::OnGoBackInHistory()
{
	const int32 NewIndex = CurrentHistoryIdx - 1;
	SetAssetHistoryIndex(NewIndex);
	return FReply::Handled();
}

bool SPaperZDAnimAssetBrowser::CanStepBackInHistory() const
{
	return CurrentHistoryIdx > 0;
}

FReply SPaperZDAnimAssetBrowser::OnGoForwardInHistory()
{
	const int32 NewIndex = CurrentHistoryIdx + 1;
	SetAssetHistoryIndex(NewIndex);
	return FReply::Handled();
}

bool SPaperZDAnimAssetBrowser::CanStepForwardInHistory() const
{
	return CurrentHistoryIdx < (AssetHistory.Num() - 1);
}

FReply SPaperZDAnimAssetBrowser::OnMouseDownHistory(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent, TWeakPtr<SMenuAnchor> InMenuAnchor)
{
	if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		InMenuAnchor.Pin()->SetIsOpen(true);
		return FReply::Handled();
	}

	return FReply::Unhandled();
}

void SPaperZDAnimAssetBrowser::ClearHistory()
{
	if (AssetHistory.IsValidIndex(CurrentHistoryIdx))
	{
		const FAssetData& AssetData = AssetHistory[CurrentHistoryIdx];
		AssetHistory.Empty(1);
		AssetHistory.Add(AssetData);

		SetAssetHistoryIndex(0);
	}
	else
	{
		AssetHistory.Empty();
		CurrentHistoryIdx = INDEX_NONE;
	}
}

void SPaperZDAnimAssetBrowser::ClearForwardHistory()
{
	if (AssetHistory.IsValidIndex(CurrentHistoryIdx))
	{
		for (int32 i = AssetHistory.Num() - 1; i > CurrentHistoryIdx; i--)
		{
			AssetHistory.RemoveAt(i);
		}
	}
}

void SPaperZDAnimAssetBrowser::SetAssetHistoryIndex(int32 NewIndex)
{
	if (AssetHistory.IsValidIndex(NewIndex))
	{
		CurrentHistoryIdx = NewIndex;
		OnRequestOpenAsset(AssetHistory[CurrentHistoryIdx], /**bFromHistory=*/true);
	}
}

void SPaperZDAnimAssetBrowser::OnRequestOpenAsset(const FAssetData& AssetData, bool bFromHistory)
{	
	UPaperZDAnimSequence* AnimationAsset = Cast<UPaperZDAnimSequence>(AssetData.GetAsset());
	if (AnimationAsset)
	{
		if (!bFromHistory)
		{
			//Push to history
			AssetHistory.Add(AssetData);
			CurrentHistoryIdx++;
			ClearForwardHistory();
		}

		//Request an open
		OnOpenAsset.ExecuteIfBound(AssetData);
	}
}

bool SPaperZDAnimAssetBrowser::CanShowColumnForAssetRegistryTag(FName AssetType, FName TagName) const
{
	return !AssetRegistryTagsToIgnore.Contains(TagName);
}

void SPaperZDAnimAssetBrowser::OnDuplicateSelection()
{
	TArray<FAssetData> CurrentSelection = GetCurrentSelectionDelegate.Execute();
	TArray<UObject*> SelectedObjects;
	for (const FAssetData& AssetData : CurrentSelection)
	{
		SelectedObjects.Add(AssetData.GetAsset());
	}

	TArray<UObject*> DuplicatedObjects;
	ObjectTools::DuplicateObjects(SelectedObjects, TEXT(""), TEXT(""), false, &DuplicatedObjects);

	//Optionally open the duplicated object, if operation was successful
	if (DuplicatedObjects.Num() > 0)
	{
		FAssetData DuplicatedAsset(DuplicatedObjects[0]);
		OnRequestOpenAsset(DuplicatedAsset, false);
	}
}

bool SPaperZDAnimAssetBrowser::CanDuplicateSelection() const
{
	//By default we just allow the duplication, but this could change eventually
	return true;
}

void SPaperZDAnimAssetBrowser::OnDeleteSelection()
{
	TArray<FAssetData> CurrentSelection = GetCurrentSelectionDelegate.Execute();
	TArray<UObject*> SelectedObjects;
	for (const FAssetData& AssetData : CurrentSelection)
	{
		SelectedObjects.Add(AssetData.GetAsset());
	}

	//Handle deletion
	ObjectTools::DeleteObjects(SelectedObjects);
}

bool SPaperZDAnimAssetBrowser::CanDeleteSelection() const
{
	//By default we just allow the duplication, but this could change eventually
	return true;
}

void SPaperZDAnimAssetBrowser::FindInContentBrowser()
{
	TArray<FAssetData> CurrentSelection = GetCurrentSelectionDelegate.Execute();
	if (CurrentSelection.Num() > 0)
	{
		FContentBrowserModule& ContentBrowserModule = FModuleManager::Get().LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
		ContentBrowserModule.Get().SyncBrowserToAssets(CurrentSelection);
	}
}

bool SPaperZDAnimAssetBrowser::CanFindInContentBrowser() const
{
	TArray<FAssetData> CurrentSelection = GetCurrentSelectionDelegate.Execute();
	return CurrentSelection.Num() > 0;
}

void SPaperZDAnimAssetBrowser::SaveSelectedAssets() const
{
	TArray<UPackage*> PackagesToSave;
	TArray<FAssetData> ObjectsToSave = GetCurrentSelectionDelegate.Execute();
	GetSelectedPackages(ObjectsToSave, PackagesToSave);

	const bool bCheckDirty = false;
	const bool bPromptToSave = false;
	const FEditorFileUtils::EPromptReturnCode Return = FEditorFileUtils::PromptForCheckoutAndSave(PackagesToSave, bCheckDirty, bPromptToSave);
}

bool SPaperZDAnimAssetBrowser::CanSaveSelectedAssets() const
{
	TArray<UPackage*> Packages;
	TArray<FAssetData> ObjectsToSave = GetCurrentSelectionDelegate.Execute();
	GetSelectedPackages(ObjectsToSave, Packages);
	// Don't offer save option if none of the packages are loaded
	return Packages.Num() > 0;
}

void SPaperZDAnimAssetBrowser::GetSelectedPackages(const TArray<FAssetData>& Assets, TArray<UPackage*>& OutPackages) const
{
	for (int32 AssetIdx = 0; AssetIdx < Assets.Num(); ++AssetIdx)
	{
		UPackage* Package = FindPackage(NULL, *Assets[AssetIdx].PackageName.ToString());

		if (Package)
		{
			OutPackages.Add(Package);
		}
	}
}

void SPaperZDAnimAssetBrowser::HandleCreateAnimSequence()
{
	UPaperZDAnimationSource* EditingSource = SourceEditorPtr.Pin()->GetAnimationSourceBeingEdited();

	// This factory may get gc'd as a side effect of various delegates potentially calling CollectGarbage so protect against it from being gc'd out from under us
	UPaperZDAnimSequenceFactory* AnimSequenceFactory = NewObject<UPaperZDAnimSequenceFactory>(GetTransientPackage());
	AnimSequenceFactory->AddToRoot();
	AnimSequenceFactory->TargetAnimSource = EditingSource;

	//Load asset tools and try to create the AnimSequence
	FAssetToolsModule& AssetToolsModule = FModuleManager::GetModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
	UObject* CreatedAsset = nullptr;

	//Here we change how we create the asset, depending on user settings
	const UPaperZDEditorSettings* Settings = GetDefault<UPaperZDEditorSettings>();
	if (Settings->SequencePlacementPolicy == EAnimSequencePlacementPolicy::AlwaysAsk)
	{
		//We should ask where to put the newly created AnimSequence (a simple call will suffice)
		CreatedAsset = AssetToolsModule.Get().CreateAssetWithDialog(UPaperZDAnimSequence::StaticClass(), AnimSequenceFactory, FName("PaperZDEditor_NewSequence"));
	}
	else
	{
		const FString DefaultAssetName = AnimSequenceFactory->GetDefaultNewAssetName();
		const FString SourcePackageName = EditingSource->GetOutermost()->GetName();
		FString AssetPath;

		if (Settings->SequencePlacementPolicy == EAnimSequencePlacementPolicy::SameFolder)
		{
			//We use the same folder as the path
			AssetPath = FPackageName::GetLongPackagePath(SourcePackageName);
		}
		else
		{
			AssetPath = FPackageName::GetLongPackagePath(SourcePackageName) + TEXT("/") + Settings->SequencePlacementFolderName.ToString();
		}

		//We will save the asset now, it won't prompt the configuration of the factories by itself, so make sure we do it manually
		if (AnimSequenceFactory->ConfigureProperties())
		{
			FString OutAssetName;
			FString OutPackageName;
			AssetToolsModule.Get().CreateUniqueAssetName(AssetPath + TEXT("/") + DefaultAssetName, TEXT(""), OutPackageName, OutAssetName);
			CreatedAsset = AssetToolsModule.Get().CreateAsset(OutAssetName, FPackageName::GetLongPackagePath(OutPackageName), UPaperZDAnimSequence::StaticClass(), AnimSequenceFactory, FName("AnimBPEditor_NewSequence"));
		}
	}

	//Show the newly created if needed
	if (CreatedAsset)
	{
		RefreshAssetViewDelegate.ExecuteIfBound(true);
		OnRequestOpenAsset(FAssetData(CreatedAsset), false);
	}

	//Remember to remove the factory
	AnimSequenceFactory->RemoveFromRoot();
}

#undef LOCTEXT_NAMESPACE
