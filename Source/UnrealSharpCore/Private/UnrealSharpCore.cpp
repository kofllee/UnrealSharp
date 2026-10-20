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
		if (FApp::IsUnattended())
		{
			UE_LOGFMT(LogUnrealSharp, Error, ".NET 10 SDK is required for an unattended Editor session.");
			return;
		}

		const UnrealSharp::DotNetInstaller::EInstallSdkResult Result =
			UnrealSharp::DotNetInstaller::InstallSdkDuringStartup();
		if (Result != UnrealSharp::DotNetInstaller::EInstallSdkResult::Success)
		{
			if (Result == UnrealSharp::DotNetInstaller::EInstallSdkResult::Cancelled)
			{
				UE_LOGFMT(LogUnrealSharp, Display, ".NET 10 SDK setup cancelled. Requesting Editor exit.");
			}
			else
			{
				UE_LOGFMT(LogUnrealSharp, Error, ".NET 10 SDK setup could not be completed.");
			}
			// The Editor is still loading modules. RequestExit(false) attempts a
			// graceful teardown of subsystems that may not have finished
			// initializing (for example, ContentBrowserData), which can crash.
			// The installer has already closed and stopped its child process.
			// Exit immediately rather than tearing down a half-started Editor.
			FPlatformMisc::RequestExit(true);
			return;
		}
	}
	// Pin the SDK resolved by native code for every managed build subprocess,
	// including UAT scripts started later in this Editor session.
	const FString SdkRoot = UnrealSharp::DotNetUtilities::GetDotNetDirectory();
	if (SdkRoot.IsEmpty())
	{
		UE_LOGFMT(LogUnrealSharp, Error, "Unable to resolve a compatible .NET 10 SDK after setup.");
		return;
	}
	FPlatformMisc::SetEnvironmentVar(TEXT("UNREALSHARP_DOTNET_ROOT"), *SdkRoot);
	FPlatformMisc::SetEnvironmentVar(TEXT("DOTNET_ROOT"), *SdkRoot);
#if PLATFORM_WINDOWS
	FPlatformMisc::SetEnvironmentVar(TEXT("DOTNET_ROOT_X64"), *SdkRoot);
#endif
	UE_LOGFMT(LogUnrealSharp, Display, "Using .NET 10 SDK at: {0}", SdkRoot);
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