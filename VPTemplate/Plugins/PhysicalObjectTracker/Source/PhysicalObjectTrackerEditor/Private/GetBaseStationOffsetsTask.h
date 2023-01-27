#pragma once
#include "TickableEditorObject.h"
#include "TrackerTransformHistory.h"

class FGetBaseStationOffsetsTask : public FTickableEditorObject
{

	static constexpr int SampleSizeSeconds = 5;
	static constexpr int SamplesPerSecond = 10;


public:
	FGetBaseStationOffsetsTask(
		int32 InTargetTrackerId, 
		int32 InTargetNumBaseStationOffsets,
		const FTransform& InTargetTrackerNeutralOffsetToSteamVROrigin,
		const TMap<int32, FTransform>& InCalibratedBaseStationOffsetzs);

	virtual void Tick(float DeltaTime) override;

	bool IsComplete() const;
	TMap<int32, FTransform>& GetResults();

	FORCEINLINE TStatId GetStatId() const { RETURN_QUICK_DECLARE_CYCLE_STAT(DetectTrackerShakeTask, STATGROUP_ThreadPoolAsyncTasks); }
private:

	void TakeBaseStationSamples();
	bool HasCompleteBaseStationsHistory();
	void BuildBaseStationResults();

	float SampleDeltaTimeAccumulator{ 0.0f };
	bool HasAcquiredTransforms{ false };

	TMap<int32, FTrackerTransformHistory> BaseStationOffsets;
	TMap<int32, FTransform> BaseStationResults;

	const int32 TargetTrackerId;
	const int32 TargetNumBaseStationOffsets;
	//The offset between the SteamVR origin and Led Volume Origin (0,0,0) when the target tracker is at the Led Volume Origin (neutral position).
	const FTransform TargetTrackerNeutralOffsetToSteamVROrigin;
	const TMap<int32, FTransform> CalibratedBaseStationOffsets;

};