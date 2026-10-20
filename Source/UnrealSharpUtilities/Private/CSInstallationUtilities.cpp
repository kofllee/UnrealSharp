#include "CSInstallationUtilities.h"

#include "CSDotnetUtilties.h"
#include "CSPathsUtilities.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"

static TAutoConsoleVariable<int32> CVarSimulateInstalledBuild(
	TEXT("UnrealSharp.SimulateInstalledBuild"),
	0,
	TEXT("If true, simulates UnrealSharp being installed by building user assemblies to the plugin directory instead of the project directory. Used for testing the installed build experience without needing to do an actual install."),
	ECVF_Default);

bool UnrealSharp::InstallationUtilities::IsUnrealSharpInstalled()
{
#if WITH_EDITOR
	if (CVarSimulateInstalledBuild.GetValueOnGameThread() != 0)
	{
		return true;
	}
#endif
	
	FString InstalledFlagFile = FPaths::Combine(Paths::GetUserAssemblyDirectory(), TEXT("UnrealSharpBuild.flag"));
	bool bFlagExists = FPaths::FileExists(InstalledFlagFile);
	return bFlagExists;
}

bool UnrealSharp::InstallationUtilities::IsDotNetSdkInstalled()
{
	const FString Root = DotNetUtilities::GetDotNetDirectory();
	if (Root.IsEmpty()) { return false; }
	TArray<FString> SdkVersions;
	IFileManager::Get().FindFiles(SdkVersions, *FPaths::Combine(Root, TEXT("sdk"), TEXT("10.*")), false, true);
	for (const FString& SdkVersion : SdkVersions)
	{
		if (!SdkVersion.StartsWith(TEXT("10.")) ||
			!DotNetUtilities::IsVersionGreaterOrEqual(SdkVersion, TEXT("10.0.201")) ||
			!FPaths::FileExists(FPaths::Combine(Root, TEXT("sdk"), SdkVersion, TEXT("MSBuild.dll"))))
		{
			continue;
		}
		TArray<FString> RuntimeVersions;
		const FString FrameworkRoot = FPaths::Combine(Root, TEXT("shared"), TEXT(DOTNET_SHARED_FRAMEWORK_NAME));
		IFileManager::Get().FindFiles(RuntimeVersions, *FPaths::Combine(FrameworkRoot, TEXT("10.*")), false, true);
		for (const FString& RuntimeVersion : RuntimeVersions)
		{
			if (RuntimeVersion.StartsWith(TEXT("10.")) &&
				FPaths::FileExists(FPaths::Combine(FrameworkRoot, RuntimeVersion, DotNetUtilities::GetCoreClrLibraryName())) &&
				!DotNetUtilities::GetLatestHostFxrPath(Root).IsEmpty())
			{
				return true;
			}
		}
	}
	return false;
}