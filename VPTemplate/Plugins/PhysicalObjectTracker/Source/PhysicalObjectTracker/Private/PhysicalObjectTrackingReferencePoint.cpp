#include "PhysicalObjectTrackingReferencePoint.h"

#include "PhysicalObjectTrackingUtility.h"
#include "SteamVRFunctionLibrary.h"
#include "Misc/CoreDelegates.h"

void UPhysicalObjectTrackingReferencePoint::SetTrackerCalibrationTransform(const FTransform& InTransform)
{
	TrackerCalibrationTransform = InTransform;
}

void UPhysicalObjectTrackingReferencePoint::SetBaseStationCalibrationTransform(
	const FString& BaseStationSerialId,
	const FTransform& OffsetCalibrationTransform,
	const FColor& Color,
	bool StaticCalibration)
{
	if(!BaseStationSerialId.IsEmpty())
	{
		BaseStationCalibrationTransforms.Add(BaseStationSerialId, OffsetCalibrationTransform);
		BaseStationCalibrationInfo.Add(BaseStationSerialId, { StaticCalibration, Color });
	}
}

void UPhysicalObjectTrackingReferencePoint::ResetBaseStationOffsets()
{
	BaseStationCalibrationTransforms.Empty();
	BaseStationCalibrationInfo.Empty();
}

const FTransform& UPhysicalObjectTrackingReferencePoint::GetTrackerCalibrationTransform() const
{
	return TrackerCalibrationTransform;
}

const TMap<FString, FTransform>& UPhysicalObjectTrackingReferencePoint::GetBaseStationCalibrationTransforms() const
{
	return BaseStationCalibrationTransforms;
}

FTransform UPhysicalObjectTrackingReferencePoint::ApplyTransformation(
	const FVector& TrackedPosition,
	const FQuat& TrackedRotation) const
{
	//Should use GetRelativeTransform as this returns leftTransform * inverse(rightTransform) where as
	//Transform.Inverse() simply inverts components separately and thus can not be used to undo transformations
	return FTransform(TrackedRotation, TrackedPosition).GetRelativeTransform(FPhysicalObjectTrackingUtility::FixTrackerTransform(TrackerCalibrationTransform));
}

FTransform UPhysicalObjectTrackingReferencePoint::GetTrackerReferenceSpaceTransform(const FTransform& TrackerCurrentTransform) const
{
	/* Math formulas to calculate tracker transformation relative to the reference space
	 * using the offsets between the transformations of the base station at calibration and current time in SteamVR space.
	 *
	 * So = Steam Origin Calibration	= 0, 0,			0, 0,  0
	 * A = BaseStation Calibration 		= 10, 12		0, 90, 0
	 * B = Tracker Calibration Spot 	= 5, 3			0,  0, 0
	 * C = A - B						= 5, 9			0, 90, 0
	 * Oc = B							= 5, 3
	 *
	 * //Calculate back.
	 *
	 * Soc = Steam Origin Current 		= 5, 0, 		90, 0, 0
	 * D = BaseStation Current			= 15, 12		90, 90, 0
	 * E = Tracker Current				= 10, 8 		90, 0, 0
	 * F = A - D 						= -5, 0			-90, 0, 0
	 *
	 * G' = E - B						= 5, 5			90, 0, 0
	 * T' = G' + F						= 0, 5, 		0, 0, 0
	 *
	 */

	//1. For every base station calculate the offset transformation between the current transformation and the transformation at calibration.
	//2. Average the offset transformations.
	//3. Get the offset transformation between the current transformation of the tracker and the transformation at calibration.
	//4. Calculate the Tracker transformation by adding the offset transformation of the tracker and the offset transformation of the base stations.

	//If not all base stations have their calibration transform mapped to a device id instead of serial id string,
	//try to map the base station ids that have not been found yet. //TODO: can not be done because of const.
	/*if(!HasMappedAllBaseStations())
	{
		MapBaseStationIds();
	}*/

	TArray<int32> currentBaseStationIds{};
	FPhysicalObjectTrackingUtility::GetAllTrackingReferenceDeviceIds(currentBaseStationIds);

	FVector averagedBaseStationOffsetTranslation = FVector::ZeroVector;
	FVector averagedBaseStationOffsetRotation = FVector::ZeroVector;
	int32 numBaseStationSamples = 0;
	for(const auto baseStation : BaseStationIdToCalibrationTransform)
	{
		if(currentBaseStationIds.Contains(baseStation.Key))	//Only sample the base station if it is currently connected (valid)
		{
			FVector currentBaseStationPosition;
			FQuat currentBaseStationRotation;
			if(FPhysicalObjectTrackingUtility::GetTrackedDevicePositionAndRotation(baseStation.Key, currentBaseStationPosition, currentBaseStationRotation))
			{
				//1. Calculate the offset transformation between the current transformation and the transformation at calibration.
				//Should use GetRelativeTransform as this returns leftTransform * inverse(rightTransform) where as
				//Transform.Inverse() simply inverts components separately and thus can not be used to undo transformations. (check function declaration)
				const FTransform offset = FTransform(currentBaseStationRotation, currentBaseStationPosition).GetRelativeTransform(baseStation.Value);
				averagedBaseStationOffsetTranslation += offset.GetTranslation();
				averagedBaseStationOffsetRotation += offset.GetRotation().Euler(); 
				++numBaseStationSamples;
			}
		}
	}

	const FTransform fixedTrackerTransform = FPhysicalObjectTrackingUtility::FixTrackerTransform(TrackerCurrentTransform);

	//Should not happen as the base stations used should be the same as the ones at calibration 
	//and at least 1 needs to be connected for tracking. So should be able to map at least one serial id to a device id
	//(Serial Id stay the same, while device ids can change in between sessions).
	if(numBaseStationSamples == 0)
	{
		return ApplyTransformation(fixedTrackerTransform.GetLocation(), fixedTrackerTransform.GetRotation());
	}

	//2. Average the offset transformations.
	averagedBaseStationOffsetTranslation /= numBaseStationSamples;
	averagedBaseStationOffsetRotation /= numBaseStationSamples;
	const FTransform baseStationOffset(FQuat::MakeFromEuler(averagedBaseStationOffsetRotation), averagedBaseStationOffsetTranslation);

	//3. Get the offset transformation for the tracker.
	const FTransform trackerOffset = fixedTrackerTransform.GetRelativeTransform(FPhysicalObjectTrackingUtility::FixTrackerTransform(TrackerCalibrationTransform));	
	return trackerOffset * baseStationOffset;
}

bool UPhysicalObjectTrackingReferencePoint::GetBaseStationReferenceSpaceTransform(const FString& BaseStationSerialId, FTransform& WorldTransform) const
{
	if(!BaseStationSerialId.IsEmpty())
	{
		if(const FTransform* baseStationTransform = BaseStationCalibrationTransforms.Find(BaseStationSerialId))
		{
			WorldTransform = baseStationTransform->GetRelativeTransform(FPhysicalObjectTrackingUtility::FixTrackerTransform(TrackerCalibrationTransform));
			return true;
		}
	}

	WorldTransform = FTransform::Identity;
	return false;
}

void UPhysicalObjectTrackingReferencePoint::UpdateRuntimeDataIfNeeded()
{
	MapBaseStationIds();
}

bool UPhysicalObjectTrackingReferencePoint::HasMappedAllBaseStations() const
{
	//TODO: might not be entirely correct if for some reason the base station Id changes mid session?
	return BaseStationIdToCalibrationTransform.Num() == BaseStationCalibrationTransforms.Num() && !BaseStationCalibrationTransforms.IsEmpty();
}

bool UPhysicalObjectTrackingReferencePoint::MapBaseStationIds()
{
	if(HasMappedAllBaseStations())
	{
		return true;
	}

	for (const auto& baseStation : BaseStationCalibrationTransforms)
	{
		int32 baseStationId = -1;
		if (FPhysicalObjectTrackingUtility::FindDeviceIdFromSerialId(baseStation.Key, baseStationId))
		{
			BaseStationIdToCalibrationTransform.Add(baseStationId, baseStation.Value);
		}
	}

	return HasMappedAllBaseStations();
}

void UPhysicalObjectTrackingReferencePoint::PostLoad()
{
	Super::PostLoad();
	UpdateRuntimeDataIfNeeded();
}

void UPhysicalObjectTrackingReferencePoint::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	UpdateRuntimeDataIfNeeded();
}

void UPhysicalObjectTrackingReferencePoint::PostInitProperties()
{
	Super::PostInitProperties();
	UpdateRuntimeDataIfNeeded();
}

void UPhysicalObjectTrackingReferencePoint::PostReinitProperties()
{
	Super::PostReinitProperties();
	UpdateRuntimeDataIfNeeded();
}
