#include "PhysicalObjectTrackingComponent.h"

#include "PhysicalObjectTracker.h"
#include "PhysicalObjectTrackingReferencePoint.h"
#include "PhysicalObjectTrackingUtility.h"
#include "SteamVRFunctionLibrary.h"
#include "SteamVRInputDeviceFunctionLibrary.h"

UPhysicalObjectTrackingComponent::UPhysicalObjectTrackingComponent(const FObjectInitializer& ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = true;

	bTickInEditor = true;
	bAutoActivate = true;
}

void UPhysicalObjectTrackingComponent::OnRegister()
{
	Super::OnRegister();
	RefreshDeviceId();
}

void UPhysicalObjectTrackingComponent::BeginPlay()
{
	Super::BeginPlay();
	if (TrackingSpaceReference == nullptr)
	{
		GEngine->AddOnScreenDebugMessage(1, 30.0f, FColor::Red, 
			FString::Format(TEXT("PhysicalObjectTrackingComponent does not have a reference referenced on object \"{0}\""), 
				FStringFormatOrderedArguments({ GetOwner()->GetName() })));
	}
}

void UPhysicalObjectTrackingComponent::TickComponent(float DeltaTime, ELevelTick Tick,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, Tick, ThisTickFunction);

	FPhysicalObjectTracker::DebugDrawTrackingReferenceLocations(TrackingSpaceReference);

	if (CurrentTargetDeviceId == -1)
	{		
		DeviceIdAcquireTimer += DeltaTime;
		if (DeviceIdAcquireTimer < DeviceReacquireInterval)
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
			trackerFromReference = TrackingSpaceReference->ApplyTransformation(trackedPosition, trackedOrientation);
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

		GetOwner()->SetActorTransform(trackerFromReference);
		//SetRelativeTransform(trackerFromReference);
	}
	else
	{
		DebugCheckIfTrackingTargetExists();
		//UE_LOG(LogPhysicalObjectTracker, Warning, TEXT("Failed to acquire TrackedDevicePosition for device id %i"), CurrentTargetDeviceId);
	}

}

void UPhysicalObjectTrackingComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.MemberProperty != nullptr && 
		PropertyChangedEvent.MemberProperty->HasMetaData("DeviceSerialId"))
	{
		RefreshDeviceId();
		if (CurrentTargetDeviceId == -1)
		{
			DeviceIdAcquireTimer = 0.0f;
		}
		
	}
}

void UPhysicalObjectTrackingComponent::SelectTracker()
{
	auto& TrackerEditorModule = FModuleManager::Get().GetModuleChecked<FPhysicalObjectTracker>("PhysicalObjectTracker");
	TrackerEditorModule.DeviceDetectionEvent.Broadcast(this);
	
}

void UPhysicalObjectTrackingComponent::RefreshDeviceId()
{
	CurrentTargetDeviceId = FPhysicalObjectTracker::GetDeviceIdFromSerialId(SerialId);
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
