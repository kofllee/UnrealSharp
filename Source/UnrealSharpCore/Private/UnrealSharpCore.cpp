#include "UnrealSharpCore.h"
#include "CoreMinimal.h"
#include "CSManager.h"
#include "CSDotnetUtilties.h"
#include "CSInstallationUtilities.h"
#include "DotNet/CSDotNetInstaller.h"
#include "Misc/App.h"
#include "Logging/StructuredLog.h"
#include "Properties/CSPropertyGeneratorManager.h"
#include "Modules/ModuleManager.h"

#if defined(__APPLE__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpragma-once-outside-header"
#endif
#pragma once
#if defined(__APPLE__)
#pragma clang diagnostic pop
#endif

#define LOCTEXT_NAMESPACE "FUnrealSharpCoreModule"

DEFINE_LOG_CATEGORY(LogUnrealSharp);

void FUnrealSharpCoreModule::StartupModule()
{
#if WITH_EDITOR
	// Keep the same Editor process paused during plugin loading. The modal
	// installer closes before the CLR and the managed bindings are initialized.
	if (!UnrealSharp::InstallationUtilities::IsDotNetSdkInstalled())
	{
		if (FApp::IsUnattended() || !UnrealSharp::DotNetInstaller::InstallSdkDuringStartup())
		{
			UE_LOGFMT(LogUnrealSharp, Error, ".NET 10 SDK installation failed or was cancelled.");
			if (!FApp::IsUnattended()) { FPlatformMisc::RequestExit(false); }
			return;
		}
	}
	if (!UnrealSharp::DotNetUtilities::VerifyCSharpEnvironment() ||
		!UnrealSharp::DotNetUtilities::BuildUserSolution())
	{
		UE_LOGFMT(LogUnrealSharp, Error, "C# environment verification or compilation failed.");
		return;
	}
#endif
	
	if (!DotNetRuntimeHost.InitializeManagedRuntime())
	{
		return;
	}
	
	UCSManager::Get().Initialize();
}

void FUnrealSharpCoreModule::ShutdownModule()
{
	FCSPropertyGeneratorManager::Shutdown();
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FUnrealSharpCoreModule, UnrealSharpCore)