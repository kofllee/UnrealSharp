#include "CSDotnetUtilties.h"

#include "CSBuildUtilties.h"
#include "CSDialogUtilities.h"
#include "CSInstallationUtilities.h"
#include "CSPathsUtilities.h"
#include "CSProjectUtilities.h"
#include "UnrealSharpUtils.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"

static TAutoConsoleVariable<int32> CVarSimulateNoDotNetSDK(
	TEXT("UnrealSharp.SimulateNoDotNetSDK"),
	0,
	TEXT("Simulate an environment where no .NET SDK is installed. This is useful for testing the UnrealSharp installation experience. Note that this will not affect UnrealSharp's ability to find a bundled .NET runtime, so it can be used to test both installed and non-installed scenarios."),
	ECVF_Default);

const TCHAR* UnrealSharp::DotNetUtilities::GetHostFxrLibraryName()
{
#if defined(_WIN32)
	return TEXT(HOSTFXR_WINDOWS);
#elif defined(__APPLE__)
	return TEXT(HOSTFXR_MAC);
#else
	return TEXT(HOSTFXR_LINUX);
#endif
}

const TCHAR* UnrealSharp::DotNetUtilities::GetCoreClrLibraryName()
{
#if defined(_WIN32)
	return TEXT(CORECLR_WINDOWS);
#elif defined(__APPLE__)
	return TEXT(CORECLR_MAC);
#else
	return TEXT(CORECLR_LINUX);
#endif
}

FString UnrealSharp::DotNetUtilities::GetDotNetDirectory()
{
#if PLATFORM_WINDOWS
	const TCHAR* Executable = TEXT("dotnet.exe");
#else
	const TCHAR* Executable = TEXT("dotnet");
#endif
	TArray<FString> Roots;
	Roots.Add(FPlatformMisc::GetEnvironmentVariable(TEXT("DOTNET_ROOT")));
#if PLATFORM_WINDOWS
	Roots.Add(FPlatformMisc::GetEnvironmentVariable(TEXT("DOTNET_ROOT_X64")));
#endif
	FPlatformMisc::GetEnvironmentVariable(TEXT("PATH")).ParseIntoArray(
		Roots, FPlatformMisc::GetPathVarDelimiter());
#if PLATFORM_WINDOWS
	Roots.Add(FPaths::Combine(FPlatformMisc::GetEnvironmentVariable(TEXT("ProgramFiles")), TEXT("dotnet")));
	Roots.Add(FPaths::Combine(FPlatformMisc::GetEnvironmentVariable(TEXT("LOCALAPPDATA")), TEXT("Microsoft"), TEXT("dotnet")));
#elif PLATFORM_MAC
	Roots.Add(TEXT("/usr/local/share/dotnet"));
	Roots.Add(TEXT("/opt/homebrew/opt/dotnet/libexec"));
	Roots.Add(FPaths::Combine(FPlatformMisc::GetEnvironmentVariable(TEXT("HOME")), TEXT(".dotnet")));
#else
	Roots.Add(TEXT("/usr/share/dotnet"));
	Roots.Add(TEXT("/usr/local/share/dotnet"));
	Roots.Add(FPaths::Combine(FPlatformMisc::GetEnvironmentVariable(TEXT("HOME")), TEXT(".dotnet")));
#endif
	FString RuntimeFallback;
	for (const FString& Candidate : Roots)
	{
		const FString Root = Candidate.TrimStartAndEnd();
		if (Root.IsEmpty() || !FPaths::FileExists(FPaths::Combine(Root, Executable)))
		{
			continue;
		}
		const FString Normalized = Root.Replace(TEXT("\\"), TEXT("/"));
		if (Normalized.Contains(TEXT("/Binaries/ThirdParty/DotNet/"), ESearchCase::IgnoreCase))
		{
			continue;
		}
		if (RuntimeFallback.IsEmpty()) { RuntimeFallback = Root; }
		TArray<FString> SdkVersions;
		IFileManager::Get().FindFiles(SdkVersions,
			*FPaths::Combine(Root, TEXT("sdk"), TEXT("10.*")), false, true);
		for (const FString& Version : SdkVersions)
		{
			if (Version.StartsWith(TEXT("10.")) &&
				IsVersionGreaterOrEqual(Version, TEXT("10.0.201")) &&
				FPaths::FileExists(FPaths::Combine(Root, TEXT("sdk"), Version, TEXT("MSBuild.dll"))))
			{
				return Root;
			}
		}
	}
	return RuntimeFallback;
}

FString UnrealSharp::DotNetUtilities::GetDotNetExecutablePath()
{
#if defined(_WIN32)
	return FPaths::Combine(GetDotNetDirectory(), TEXT("dotnet.exe"));
#else
	return FPaths::Combine(GetDotNetDirectory(), TEXT("dotnet"));
#endif
}

FString UnrealSharp::DotNetUtilities::GetLatestHostFxrPath(const FString& DotNetRoot)
{
	if (DotNetRoot.IsEmpty())
	{
		return FString();
	}

	const FString FxrRoot = FPaths::Combine(DotNetRoot, TEXT("host"), TEXT("fxr"));

	TArray<FString> VersionFolders;
	IFileManager::Get().FindFiles(VersionFolders, *FPaths::Combine(FxrRoot, TEXT("*")), false, true);

	FString HighestVersion;
	for (const FString& Folder : VersionFolders)
	{
		if (HighestVersion.IsEmpty() || IsVersionHigher(Folder, HighestVersion))
		{
			HighestVersion = Folder;
		}
	}

	if (HighestVersion.IsEmpty() || !IsVersionGreaterOrEqual(HighestVersion, TEXT(DOTNET_MAJOR_VERSION)))
	{
		return FString();
	}

	const FString HostFxrPath = FPaths::Combine(FxrRoot, HighestVersion, GetHostFxrLibraryName());
	return FPaths::FileExists(HostFxrPath) ? HostFxrPath : FString();
}

FString UnrealSharp::DotNetUtilities::GetRuntimeConfigPath(const FString& AssemblyPath)
{
	FString RuntimeConfigPath = AssemblyPath;
	RuntimeConfigPath.RemoveFromEnd(TEXT(".dll"));
	return RuntimeConfigPath + TEXT(DOTNET_RUNTIME_CONFIG_SUFFIX);
}

bool UnrealSharp::DotNetUtilities::IsSelfContainedDirectory(const FString& Directory)
{
	if (Directory.IsEmpty())
	{
		return false;
	}

	return FPaths::FileExists(FPaths::Combine(Directory, GetHostFxrLibraryName()))
		&& FPaths::FileExists(FPaths::Combine(Directory, GetCoreClrLibraryName()));
}

bool UnrealSharp::DotNetUtilities::IsSharedFrameworkRoot(const FString& DotNetRoot)
{
	if (DotNetRoot.IsEmpty())
	{
		return false;
	}

	return FPaths::DirectoryExists(FPaths::Combine(DotNetRoot, TEXT("shared"), TEXT(DOTNET_SHARED_FRAMEWORK_NAME)))
		&& FPaths::DirectoryExists(FPaths::Combine(DotNetRoot, TEXT("host"), TEXT("fxr")));
}

bool UnrealSharp::DotNetUtilities::IsSelfContainedRuntimeConfig(const FString& RuntimeConfigPath)
{
	FString Contents;
	if (!FFileHelper::LoadFileToString(Contents, *RuntimeConfigPath))
	{
		UE_LOGFMT(LogUnrealSharpUtilities, Warning, "Could not read runtime config at: {0}", RuntimeConfigPath);
		return false;
	}

	return Contents.Contains(TEXT("includedFrameworks"));
}

#if WITH_EDITOR
bool UnrealSharp::DotNetUtilities::VerifyCSharpEnvironment()
{
	FString DotNetInstallationPath = GetDotNetDirectory();
	if (!InstallationUtilities::IsDotNetSdkInstalled())
	{
		FString DialogText = FString::Printf(TEXT("UnrealSharp can't be initialized. An installation of .NET %s SDK can't be found on your system."), TEXT(DOTNET_MAJOR_VERSION));
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(DialogText));
		return false;
	}

	FString UnrealSharpLibraryPath = Paths::GetUnrealSharpPluginsPath();
	if (!FPaths::FileExists(UnrealSharpLibraryPath))
	{
		FString FullPath = FPaths::ConvertRelativePathToFull(UnrealSharpLibraryPath);
		FString DialogText = FString::Printf(TEXT(
			"The bindings library could not be found at the following location:\n%s\n\n"
			"Most likely, the bindings library failed to build due to invalid generated glue."
		), *FullPath);

		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(DialogText));
		return false;
	}

	return true;
}

bool UnrealSharp::DotNetUtilities::BuildUserSolution()
{
	TArray<FString> ProjectPaths;
	Project::GetAllProjectPaths(ProjectPaths);

	if (ProjectPaths.IsEmpty())
	{
		return true;
	}

	if (FCSUnrealSharpUtils::IsStandalonePIE() || FApp::IsUnattended())
	{
		return true;
	}

	return Build::BuildUserSolution(Dialogs::MakeOkCancelDialogOnError());
}
#endif

FString& UnrealSharp::DotNetUtilities::GetManagedBinaries()
{
	static FString ManagedBinaries = FPaths::Combine(TEXT("Binaries"), TEXT("Managed"), TEXT(DOTNET_DISPLAY_NAME));
	return ManagedBinaries;
}

bool UnrealSharp::DotNetUtilities::ParseDotNetVersion(const FString& VersionString, int32& OutMajor, int32& OutMinor, int32& OutPatch)
{
	TArray<FString> Parts;
	VersionString.ParseIntoArray(Parts, TEXT("."));

	if (Parts.Num() < 3)
	{
		return false;
	}

	OutMajor = FCString::Atoi(*Parts[0]);
	OutMinor = FCString::Atoi(*Parts[1]);
	OutPatch = FCString::Atoi(*Parts[2]);
	return true;
}

bool UnrealSharp::DotNetUtilities::IsVersionGreaterOrEqual(const FString& Version, const FString& MinVersion)
{
	int32 Major, Minor, Patch;
	int32 MinMajor, MinMinor, MinPatch;

	if (!ParseDotNetVersion(Version, Major, Minor, Patch) || !ParseDotNetVersion(MinVersion, MinMajor, MinMinor, MinPatch))
	{
		return false;
	}

	if (Major != MinMajor)
	{
		return Major > MinMajor;
	}

	if (Minor != MinMinor)
	{
		return Minor > MinMinor;
	}

	return Patch >= MinPatch;
}

bool UnrealSharp::DotNetUtilities::IsVersionHigher(const FString& A, const FString& B)
{
	int32 MajorA, MinorA, PatchA;
	int32 MajorB, MinorB, PatchB;

	if (!ParseDotNetVersion(A, MajorA, MinorA, PatchA) || !ParseDotNetVersion(B, MajorB, MinorB, PatchB))
	{
		return false;
	}

	if (MajorA != MajorB)
	{
		return MajorA > MajorB;
	}

	if (MinorA != MinorB)
	{
		return MinorA > MinorB;
	}

	return PatchA > PatchB;
}
