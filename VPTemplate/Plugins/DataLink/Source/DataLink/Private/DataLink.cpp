// Copyright Epic Games, Inc. All Rights Reserved.

#include "DataLink.h"

#include "TCPMessaging.h"
#include "ObjectTrackingDataLink.h"


DEFINE_LOG_CATEGORY(LogDataLink)

#define LOCTEXT_NAMESPACE "FDataLinkModule"

void FDataLinkModule::StartupModule()
{
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module
	MessagingService = MakeShared<FTCPMessaging>();

#if WITH_EDITOR
	if (GIsEditor)
	{
		IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("DataLinkConnect"),
			TEXT("Connect to a socket specified by -Endpoint:xxxx.xxxx.xxxx.xxxx:yyyy"),
			FConsoleCommandWithArgsDelegate::CreateRaw(this, &FDataLinkModule::HandleConnectCommand),
			0);

		IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("DataLinkSend"),
			TEXT("Send a message to the connected socket. First connect to a socket using DataLinkConnect"),
			FConsoleCommandWithArgsDelegate::CreateRaw(this, &FDataLinkModule::HandleSendCommand),
			0);
	}
#endif

	ObjectTrackingDataLink = MakeUnique<FObjectTrackingDataLink>(MessagingService);

}

void FDataLinkModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.
}

void FDataLinkModule::HandleConnectCommand(const TArray<FString>& Arguments) const
{
	FIPv4Endpoint remoteEndpoint(FIPv4Address::InternalLoopback, 5000);

	if(const FString* endpointArg = 
		Arguments.FindByPredicate([](const FString& argument) { return argument.StartsWith("-Endpoint:");}))
	{
		const FString endpointStr = (*endpointArg).RightChop(10);
		FIPv4Endpoint::Parse(endpointStr, remoteEndpoint);
	}

	MessagingService->ConnectToSocket(remoteEndpoint, UINT32_MAX);

}

void FDataLinkModule::HandleSendCommand(const TArray<FString>& Arguments) const
{
	if(Arguments.IsEmpty())
	{
		return;
	}

	//Create a byte array of all the arguments, inserting spaces between the arguments.
	const TSharedRef<TArray<uint8>> messageData = MakeShared<TArray<uint8>>();
	for(const auto& argument : Arguments)
	{
		const ANSICHAR* messageUTF8 = TCHAR_TO_UTF8(*argument);
		
		for(auto currentPos = messageUTF8; *currentPos != '\0'; currentPos++)
		{
			messageData->Add(static_cast<uint8>(*currentPos));
		}
		messageData->Add(static_cast<uint8>(' '));
	}

    MessagingService->Send(messageData);
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FDataLinkModule, DataLink)