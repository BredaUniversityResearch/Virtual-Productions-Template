#include "EditorData.h"

#include "ItemHandle.h"

#include "Engine/DirectionalLight.h"
#include "Engine/PointLight.h"
#include "Engine/SkyLight.h"
#include "Engine/SpotLight.h"

#include "ToolData.h"

#include "BaseLight.h"

//#include "LightTreeHierarchy.h"

#include "LightEditorWidget.h"
#include "Interfaces/IPluginManager.h"

UEditorData::UEditorData()
{
    SetFlags(GetFlags() | RF_Transactional);


}

UEditorData::~UEditorData()
{
    //AutoSave();
}

UBaseLight* UEditorData::GetLightByName(FString Name)
{
    for (auto& LightItem : ToolData->ListOfLightItems)
    {
        if (LightItem->Name == Name)
        {
            return LightItem->Item;
        }
    }

    if (GEngine)
    {
        GEngine->AddOnScreenDebugMessage(1999, 0.5f, FColor::Cyan, FString::Printf(TEXT("Could not find item with name \"%s\""), *Name));
    }

    return nullptr;
}

void UEditorData::SetToolData(UToolData* InToolData)
{
    ToolData = InToolData;
    ToolData->OnTransacted.BindUObject(this, &UEditorData::OnToolDataTransacted);
}

void UEditorData::SetWidgetRef(SLightEditorWidget& Widget)
{
    OwningWidget = &Widget;
}


void UEditorData::OnToolDataTransacted(const FTransactionObjectEvent& TransactionEvent)
{

    if (TransactionEvent.GetEventType() == ETransactionObjectEventType::UndoRedo)
    {
        volatile auto Master = GetMasterLight();
        TreeStructureChangedDelegate.ExecuteIfBound();
        check(Master == GetMasterLight());
    }
}

void UEditorData::PostLightTransacted(const FTransactionObjectEvent& TransactionEvent, UBaseLight& Light)
{
	if (TransactionEvent.GetEventType() == ETransactionObjectEventType::UndoRedo)
	{
		if (Light.Handle == GetMasterLight())
		{
            OwningWidget->UpdateSaturationGradient(Light.Hue);
		}
	}
}


bool UEditorData::IsAMasterLightSelected()
{
    return IsValid(GetMasterLight());
}

bool UEditorData::MultipleItemsSelected()
{
    return SelectedItems.Num() > 1;
}

bool UEditorData::MultipleLightsInSelection()
{
    return LightsUnderSelection.Num() > 1;
}

UItemHandle* UEditorData::GetMasterLight()
{
    return SelectionMasterLight;
}

UItemHandle* UEditorData::GetSelectedGroup()
{
    if (SelectedItems.Num() && SelectedItems[0]->Type == Folder)
    {
        return SelectedItems[0];
    }
    return nullptr;
}

UItemHandle* UEditorData::GetSingleSelectedItem()
{
    if (IsSingleGroupSelected())
        return GetSelectedGroup();
    if (IsAMasterLightSelected())
        return GetMasterLight();
    return nullptr;
}

const TArray<UItemHandle*>& UEditorData::GetSelectedLights()
{
    return LightsUnderSelection;
}

TArray<UItemHandle*> UEditorData::GetSelectedItems()
{
    return SelectedItems;
}

void UEditorData::BeginTransaction()
{
    Modify();
}


void UEditorData::ClearAllData()
{
    SelectedItems.Empty();
    LightsUnderSelection.Empty();

    SelectionMasterLight = nullptr;

    ToolData->ClearAllData();

    ClearSelectionDelegate.ExecuteIfBound();

    //GEngine->AddOnScreenDebugMessage(-1, 55.f, FColor::Red, "Tooldata cleared!");

}

bool UEditorData::IsSingleGroupSelected()
{
    return SelectedItems.Num() == 1 && SelectedItems[0]->Type == Folder;
}


FReply UEditorData::SaveCallBack()
{
    if (ToolPresetPath.IsEmpty())
    {
        return SaveAsCallback();
    }
    else
        SaveStateToJson(ToolPresetPath);

    return FReply::Handled();
}


FReply UEditorData::SaveAsCallback()
{
    check(SaveFileDialog.IsBound());


    FString StartPath;

    if (ToolPresetPath.IsEmpty())
    {
        auto ThisPlugin = IPluginManager::Get().FindPlugin("CradleLightControl");
        StartPath = ThisPlugin->GetContentDir();
    }
    else
        StartPath = ToolPresetPath;


    auto Path = SaveFileDialog.Execute("Select file to save tool state to", StartPath);
    if (!Path.IsEmpty())
    {
        SaveStateToJson(Path);
    }
    return FReply::Handled();
}

void UEditorData::SaveStateToJson(FString Path, bool bUpdatePresetPath)
{
    ToolData->SaveStateToJson(Path);
    if (bUpdatePresetPath)
        ToolPresetPath = Path;
}

FReply UEditorData::LoadCallBack()
{
    check(OpenFileDialog.IsBound());

    FString StartPath;

    if (ToolPresetPath.IsEmpty())
    {
        auto ThisPlugin = IPluginManager::Get().FindPlugin("CradleLightControl");
        StartPath = ThisPlugin->GetContentDir();
    }
    else
        StartPath = ToolPresetPath;

    auto Path = OpenFileDialog.Execute("Select file to load tool state from", StartPath);
    if (!Path.IsEmpty())
    {
        LoadStateFromJSON(Path);
    }
    return FReply::Handled();
}

void UEditorData::LoadStateFromJSON(FString Path, bool bUpdatePresetPath)
{
    bCurrentlyLoading = true;


    FString Input;
    ClearSelectionDelegate.ExecuteIfBound();
    //GEngine->AddOnScreenDebugMessage(328 + DataName.Len(), 60.0f, FColor::Magenta,
    //    FString::Printf(TEXT("Trying to load data from %s"), *Path));
	ClearAllData();

    // todo: This must check if the file exists, ideally without loading it in the process
    if (FFileHelper::LoadFileToString(Input, *Path))
    {
        ToolData->LoadStateFromJSON(Path);
        if (bUpdatePresetPath)
            ToolPresetPath = Path;
        TreeStructureChangedDelegate.ExecuteIfBound();        
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("Could not open file %s"), *Path);
        ToolPresetPath = "";
    }

    //GEngine->AddOnScreenDebugMessage(228 + DataName.Len(), 60.0f, FColor::Magenta,
    //    FString::Printf(TEXT("%lu root elements loaded for %s"), RootItems.Num(), *DataName));
    bCurrentlyLoading = false;
}


void UEditorData::AutoSave()
{
    UE_LOG(LogTemp, Display, TEXT("Autosaving light control tool state."));

    if (ToolPresetPath.IsEmpty())
    {
        auto ThisPlugin = IPluginManager::Get().FindPlugin("CradleLightControl");
        auto SavedDir = ThisPlugin->GetBaseDir() + "/Saved";

        SaveStateToJson(SavedDir + "/" + ToolData->DataName + "AutoSave.json", false);
    }
    else
        SaveStateToJson(ToolPresetPath, false);

    SaveMetaData();
}

TSharedPtr<FJsonObject> UEditorData::OpenMetaDataJson()
{
    auto ThisPlugin = IPluginManager::Get().FindPlugin("CradleLightControl");
    auto SavedDir = ThisPlugin->GetBaseDir() + "/Saved";
    FString Input;
    //GEngine->AddOnScreenDebugMessage(128 + DataName.Len(), 60.0f, FColor::Magenta,
    //    FString::Printf(TEXT("Loading metadata for %s"), *DataName));
    if (FFileHelper::LoadFileToString(Input, *(SavedDir + "/" + ToolData->DataName + "MetaData.json")))
    {
        TSharedPtr<FJsonObject> JsonRoot;
        TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<>::Create(Input);
        FJsonSerializer::Deserialize(JsonReader, JsonRoot);

        return JsonRoot;
    }

    return nullptr;

}

void UEditorData::SaveMetaData()
{
    UE_LOG(LogTemp, Display, TEXT("Saving light control meta data."));
    auto ThisPlugin = IPluginManager::Get().FindPlugin("CradleLightControl");
    auto SavedDir = ThisPlugin->GetBaseDir() + "/Saved";

    TSharedPtr<FJsonObject> RootObject = MakeShared<FJsonObject>();

    RootObject->SetStringField("LastUsedPreset", ToolPresetPath);

    MetaDataSaveExtension.ExecuteIfBound(RootObject);

    FString Output;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
    FJsonSerializer::Serialize(RootObject.ToSharedRef(), Writer);

    FFileHelper::SaveStringToFile(Output, *(SavedDir + "/" + ToolData->DataName + "MetaData.json"));
}

void UEditorData::LoadMetaData()
{
    ToolData->LoadMetaData();
}