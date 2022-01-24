#pragma once

#include "Templates/SharedPointer.h"
#include "Chaos/AABB.h"
#include "Slate.h"
#include "DMXProtocolCommon.h"


class UItemHandle;
class UToolData;

DECLARE_DELEGATE_OneParam(FUpdateItemDataDelegate, UItemHandle*)
DECLARE_DELEGATE(FItemDataVerificationDelegate);
DECLARE_DELEGATE(FTreeSelectionChangedDelegate);


class SLightTreeHierarchy : public SCompoundWidget
{
public:


    SLATE_BEGIN_ARGS(SLightTreeHierarchy)
        : _Name("Unnamed tree view")
    {}

    SLATE_ARGUMENT(class UToolData*, ToolData)

    SLATE_ARGUMENT(FString, Name)

    SLATE_ARGUMENT(FUpdateItemDataDelegate, DataUpdateDelegate)

    SLATE_ARGUMENT(FItemDataVerificationDelegate, DataVerificationDelegate)
    SLATE_ARGUMENT(float, DataVerificationInterval)

    SLATE_ARGUMENT(FTreeSelectionChangedDelegate, SelectionChangedDelegate)

    SLATE_END_ARGS()

    void Construct(const FArguments& Args);
    void PreDestroy();

    void OnActorSpawned(AActor* Actor);

    void BeginTransaction();


    TSharedRef<ITableRow> AddToTree(::UItemHandle* Item, const TSharedRef<STableViewBase>& OwnerTable);

    void GetTreeItemChildren(::UItemHandle* Item, TArray<UItemHandle*>& Children);
    void SelectionCallback(UItemHandle* Item, ESelectInfo::Type SelectType);
    FReply AddFolderToTree();
    void TreeExpansionCallback(UItemHandle* Item, bool bExpanded);
    void OnToolDataLoadedCallback(uint8 LoadingResult);
    void RegenerateItemHandleWidgets(UItemHandle* ItemHandle);
    EActiveTimerReturnType VerifyLights(double, float);

    FReply DragDropBegin(const FGeometry& Geometry, const FPointerEvent& MouseEvent);
    FReply DragDropEnd(const FDragDropEvent& DragDropEvent);

    void SearchBarOnChanged(const FText& NewString);


    FText GetPresetFilename() const;


    UToolData* ToolData;

    FSlateIcon SaveIcon;
    FSlateIcon SaveAsIcon;
    FSlateIcon LoadIcon;

    FText HeaderText;


    TSharedPtr<STreeView<UItemHandle*>> Tree;
    FString SearchString;

    FUpdateItemDataDelegate DataUpdateDelegate;
    FItemDataVerificationDelegate DataVerificationDelegate;

    //class UToolData* TransactionalVariables;
    TSharedPtr<FActiveTimerHandle> LightVerificationTimer;

    FTreeSelectionChangedDelegate SelectionChangedDelegate;
};
