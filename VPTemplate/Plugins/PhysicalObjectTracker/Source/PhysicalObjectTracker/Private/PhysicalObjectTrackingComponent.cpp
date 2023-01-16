#include "PhysicalObjectTrackingComponent.h"

#include "PhysicalObjectTracker.h"
#include "PhysicalObjectTrackerSerialId.h"
#include "PhysicalObjectTrackingFilterSettings.h"
#include "PhysicalObjectTrackingReferencePoint.h"
#include "PhysicalObjectTrackingUtility.h"
#include "SteamVRFunctionLibrary.h"
#include "SteamVRInputDeviceFunctionLibrary.h"

#include"Engine/EngineTypes.h"

UPhysicalObjectTrackingComponent::UPhysicalObjectTrackingComponent(const FObjectInitializer& ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = true;

	bTickInEditor = true;
	bAutoActivate = true;
}

void UPhysicalObjectTrackingComponent::OnRegister()
{
	Super::OnRegister();
	if (FilterSettings != nullptr)
	{
		FilterSettingsChangedHandle = FilterSettings->OnFilterSettingsChanged.AddUObject(this, &UPhysicalObjectTrackingComponent::OnFilterSettingsChangedCallback);
	}
	if (TrackerSerialId != nullptr)
	{
		SerialIdChangedHandle = TrackerSerialId->OnSerialIdChanged.AddUObject(this, &UPhysicalObjectTrackingComponent::OnTrackerSerialIdChangedCallback);
		RefreshDeviceId();
	}
	ExtractComponentReferenceIfValid();
	OnFilterSettingsChangedCallback();
}

void UPhysicalObjectTrackingComponent::BeginPlay()
{
	Super::BeginPlay();
	if (TrackingSpaceReference == nullptr)
	{
		GEngine->AddOnScreenDebugMessage(1, 30.0f, FColor::Red, 
			FString::Format(TEXT("PhysicalObjectTrackingComponent \"{0}\" does not have reference a tracking space on object \"{1}\""), 
				FStringFormatOrderedArguments({GetName(),  GetOwner()->GetName() })));
	}
	
}

void UPhysicalObjectTrackingComponent::TickComponent(float DeltaTime, ELevelTick Tick,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, Tick, ThisTickFunction);
	if (TrackerSerialId == nullptr) { return; }

	if (CurrentTargetDeviceId == -1)
	{
		DeviceIdAcquireTimer += DeltaTime;
		if (DeviceIdAcquireTimer > DeviceReacquireInterval) 
		{
			RefreshDeviceId();
			DeviceIdAcquireTimer -= DeviceReacquireInterval;
		}
		return;
	}

	FVector trackedPosition;
	FQuat trackedOrientation;	
	if (FPhysicalObjectTrackingUtility::GetTrackedDevicePositionAndRotation(CurrentTargetDeviceId, trackedPosition, trackedOrientation))
	{
		FTransform trackerFromReference;
		if (TrackingSpaceReference != nullptr)
		{
			TArray<int32> baseStationIds;
			TrackingSpaceReference->GetBaseStationIds(baseStationIds);

			TMap<int32, FBaseStationOffset> currentBaseStationOffsets;
			for (int32 baseStation : baseStationIds)
			{
				FVector baseStationPosition;
				FQuat baseStationRotation;
				if (FPhysicalObjectTrackingUtility::GetTrackedDevicePositionAndRotation(baseStation, baseStationPosition, baseStationRotation))
				{
					const FVector positionOffset = baseStationPosition - trackedPosition;
					const FQuat rotationOffset = trackedOrientation * baseStationRotation.Inverse();
					currentBaseStationOffsets.Add(baseStation, FBaseStationOffset{ positionOffset, rotationOffset });
				}
			}

			FTransform relativeTransform;
			TrackingSpaceReference->CalcTransformationFromBaseStations(currentBaseStationOffsets, relativeTransform);

			trackerFromReference = TrackingSpaceReference->ApplyTransformation(relativeTransform.GetLocation(), relativeTransform.GetRotation());
			//trackerFromReference = TrackingSpaceReference->ApplyTransformation(trackedPosition, trackedOrientation);
		}
		else
		{
			trackerFromReference = FTransform(trackedOrientation, trackedPosition);
		}

		if (WorldReferencePoint != nullptr)
		{
			trackerFromReference.SetLocation(WorldReferencePoint->GetActorTransform().TransformPosition(trackerFromReference.GetLocation()));
			//trackerFromReference.SetLocation((WorldReferencePoint->GetActorTransform().GetRotation() * trackerFromReference.GetLocation()) + WorldReferencePoint->GetActorTransform().GetLocation());
			trackerFromReference.SetRotation(WorldReferencePoint->GetActorTransform().TransformRotation(trackerFromReference.GetRotation()));
		}

		m_TransformHistory.AddSample(trackerFromReference);
		const FTransform filteredTransform = m_TransformHistory.GetAveragedTransform(FilterSettings);

		if(HasTransformationTargetComponent && TransformationTargetComponent != nullptr)
		{
			TransformationTargetComponent.Get()->SetWorldTransform(filteredTransform);
		}
		else
		{
			GetOwner()->SetActorTransform(filteredTransform);
		}
	}
	else
	{
		DebugCheckIfTrackingTargetExists();
		//UE_LOG(LogPhysicalObjectTracker, Warning, TEXT("Failed to acquire TrackedDevicePosition for device id %i"), CurrentTargetDeviceId);
	}

}

#if WITH_EDITOR
void UPhysicalObjectTrackingComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.MemberProperty != nullptr)
	{
		//TODO: check if this event is enough to update device id if serial id changes
		//either by being set in the property or the data asset being changed.
		if (PropertyChangedEvent.MemberProperty->GetFName() == FName(TEXT("TrackerSerialId")))
		{
			SerialIdChangedHandle.Reset();
			if (TrackerSerialId != nullptr)
			{
				SerialIdChangedHandle = TrackerSerialId->OnSerialIdChanged.AddUObject(this, &UPhysicalObjectTrackingComponent::OnTrackerSerialIdChangedCallback);
				RefreshDeviceId();
				if (CurrentTargetDeviceId == -1)
				{
					DeviceIdAcquireTimer = 0.0f;
				}
			}
		}
		else if (PropertyChangedEvent.MemberProperty->GetFName() == FName(TEXT("FilterSettings")))
		{
			FilterSettingsChangedHandle.Reset();
			if (FilterSettings != nullptr)
			{
				FilterSettingsChangedHandle = FilterSettings->OnFilterSettingsChanged.AddUObject(this, &UPhysicalObjectTrackingComponent::OnFilterSettingsChangedCallback);
			}
		}
		else if (PropertyChangedEvent.MemberProperty->GetFName() == FName(TEXT("TransformationTargetComponentReference")))
		{
			ExtractComponentReferenceIfValid();
		}
	}
}
#endif

void UPhysicalObjectTrackingComponent::RefreshDeviceId()
{
	if (TrackerSerialId == nullptr)
	{
		if(GetOwner() != nullptr)
		{
			GEngine->AddOnScreenDebugMessage(1, 30.f, FColor::Red,
				FString::Format(TEXT("PhysicalObjectTrackingComponent is refreshing the device id without a TrackerSerialId referenced on object \"{0}\""),
					FStringFormatOrderedArguments({ GetOwner()->GetName() })));
		}
		return;
	}

	int32 foundDeviceId;
	if (FPhysicalObjectTrackingUtility::FindDeviceIdFromSerialId(TrackerSerialId->SerialId, foundDeviceId))
	{
		if (CurrentTargetDeviceId != foundDeviceId)
		{
			CurrentTargetDeviceId = foundDeviceId;
		}
	}
}

const FTransform* UPhysicalObjectTrackingComponent::GetWorldReferencePoint() const
{
	return WorldReferencePoint != nullptr ? &WorldReferencePoint->GetActorTransform() : nullptr;
}

const UPhysicalObjectTrackingReferencePoint* UPhysicalObjectTrackingComponent::GetTrackingReferencePoint() const
{
	return TrackingSpaceReference;
}

void UPhysicalObjectTrackingComponent::DebugCheckIfTrackingTargetExists() const
{
	TArray<int32> deviceIds{};
	USteamVRFunctionLibrary::GetValidTrackedDeviceIds(ESteamVRTrackedDeviceType::Controller, deviceIds);
	if (!deviceIds.Contains(CurrentTargetDeviceId))
	{
		TWideStringBuilder<4096> builder{};
		builder.Appendf(TEXT("Could not find SteamVR Controller with DeviceID: %i. Valid device IDs are: "), CurrentTargetDeviceId);
		for (int32 deviceId : deviceIds)
		{
			builder.Appendf(TEXT("%i, "), deviceId);
		}
		GEngine->AddOnScreenDebugMessage(565498, 0.0f, FColor::Red, builder.ToString(), false);
	}
}

void UPhysicalObjectTrackingComponent::OnFilterSettingsChangedCallback()
{
	m_TransformHistory.SetFromFilterSettings(FilterSettings);
}

void UPhysicalObjectTrackingComponent::OnTrackerSerialIdChangedCallback()
{
	RefreshDeviceId();
	if (CurrentTargetDeviceId == -1)
	{
		DeviceIdAcquireTimer = 0.0f;
	}
}

void UPhysicalObjectTrackingComponent::ExtractComponentReferenceIfValid()
{
    AActor* owningActor =  GetOwner();
	if (HasTransformationTargetComponent && owningActor != nullptr)
	{
		GEngine->AddOnScreenDebugMessage(1, 30.f, FColor::Blue, FString(TEXT("ExtractingComponentReference")));
		UActorComponent* transformationTargetActorComponent = TransformationTargetComponentReference.GetComponent(owningActor);
		if (transformationTargetActorComponent != nullptr)
		{
			TransformationTargetComponent = Cast<USceneComponent>(transformationTargetActorComponent);
			if (TransformationTargetComponent == nullptr)
			{
				GEngine->AddOnScreenDebugMessage(1, 30.f, FColor::Red,
					FString::Format(TEXT("PhysicalObjectTrackingComponent does not reference a component that is or inherits from a scene component as movement target component. Component in actor: \"{0}\""),
						FStringFormatOrderedArguments({ GetOwner()->GetName() })));
			}
		}
		else
		{
			GEngine->AddOnScreenDebugMessage(1, 30.f, FColor::Red,
				FString::Format(TEXT("PhysicalObjectTrackingComponent does not reference a valid component as movement target component. Component in actor: \"{0}\""),
					FStringFormatOrderedArguments({ GetOwner()->GetName() })));
		}
	}
}