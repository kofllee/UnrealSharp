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
	// GetDotNetDirectory only returns a complete, compatible SDK root in Editor.
	return !DotNetUtilities::GetDotNetDirectory().IsEmpty();
}