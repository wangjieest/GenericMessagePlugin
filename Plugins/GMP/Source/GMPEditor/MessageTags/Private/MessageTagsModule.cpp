// Copyright Epic Games, Inc. All Rights Reserved.

#include "MessageTagsModule.h"
#include "Misc/ConfigCacheIni.h"
#include "HAL/IConsoleManager.h"
#include "UObject/Package.h"

FSimpleMulticastDelegate IMessageTagsModule::OnMessageTagTreeChanged;
FSimpleMulticastDelegate IMessageTagsModule::OnTagSettingsChanged;

class FMessageTagsModule : public IMessageTagsModule
{
	// Begin IModuleInterface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	// End IModuleInterface
};

IMPLEMENT_MODULE( FMessageTagsModule, MessageTags )
DEFINE_LOG_CATEGORY(LogMessageTags);

void FMessageTagsModule::StartupModule()
{
	// This will force initialization
	UMessageTagsManager::Get();
}

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
int32 MessageTagPrintReportOnShutdown = 0;
static FAutoConsoleVariableRef CVarMessageTagPrintReportOnShutdown(TEXT("MessageTags.PrintReportOnShutdown"), MessageTagPrintReportOnShutdown, TEXT("Print message tag replication report on shutdown"), ECVF_Default );
#endif


void FMessageTagsModule::ShutdownModule()
{
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	if (MessageTagPrintReportOnShutdown)
	{
		UMessageTagsManager::Get().PrintReplicationFrequencyReport();
	}
#endif

	UMessageTagsManager::SingletonManager = nullptr;
}
